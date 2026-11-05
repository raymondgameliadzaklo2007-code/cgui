#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

/**
 * lsp.c – Language Server Protocol (LSP) client implementation for cgui
 */

#include "lsp.h"
#include "app.h"
#include "editor.h"
#include "theme.h"
#include "tab_bar.h"
#include "vk_draw.h"
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <SDL3/SDL.h>

/* ------------------------------------------------------------------ */
/* Minimal, Robust JSON & String Helpers                              */
/* ------------------------------------------------------------------ */

static const char *json_skip_ws(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        p++;
    return p;
}

static const char *json_find_property(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    size_t klen = strlen(needle);

    const char *p = json;
    while ((p = strstr(p, needle)) != NULL) {
        /* Check if key is a real property name followed by colon */
        const char *after = json_skip_ws(p + klen);
        if (*after == ':')
            return json_skip_ws(after + 1);
        p += klen;
    }
    return NULL;
}

static bool json_parse_str(const char *val, char *out, size_t out_cap)
{
    if (!val || *val != '\"') return false;
    val++;
    size_t o = 0;
    while (*val && *val != '\"') {
        if (*val == '\\' && *(val + 1)) {
            val++;
            switch (*val) {
            case 'n': if (o + 1 < out_cap) out[o++] = '\n'; break;
            case 't': if (o + 1 < out_cap) out[o++] = '\t'; break;
            case 'r': if (o + 1 < out_cap) out[o++] = '\r'; break;
            case '\"': if (o + 1 < out_cap) out[o++] = '\"'; break;
            case '\\': if (o + 1 < out_cap) out[o++] = '\\'; break;
            default:   if (o + 1 < out_cap) out[o++] = *val; break;
            }
        } else {
            if (o + 1 < out_cap) out[o++] = *val;
        }
        val++;
    }
    out[o] = '\0';
    return true;
}

static int json_parse_int(const char *val)
{
    if (!val) return 0;
    val = json_skip_ws(val);
    return atoi(val);
}

static void escape_json_string(const char *in, char *out, size_t out_cap)
{
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < out_cap; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\"') {
            out[o++] = '\\'; out[o++] = '\"';
        } else if (c == '\\') {
            out[o++] = '\\'; out[o++] = '\\';
        } else if (c == '\n') {
            out[o++] = '\\'; out[o++] = 'n';
        } else if (c == '\r') {
            out[o++] = '\\'; out[o++] = 'r';
        } else if (c == '\t') {
            out[o++] = '\\'; out[o++] = 't';
        } else if (c >= 32) {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void path_to_uri(const char *path, char *out_uri, size_t cap)
{
    if (strncmp(path, "file://", 7) == 0) {
        snprintf(out_uri, cap, "%s", path);
        return;
    }
    char abs[LSP_MAX_PATH];
    if (path[0] != '/' && realpath(path, abs))
        snprintf(out_uri, cap, "file://%s", abs);
    else
        snprintf(out_uri, cap, "file://%s", path);
}

static void uri_to_path(const char *uri, char *out_path, size_t cap)
{
    if (strncmp(uri, "file://", 7) == 0)
        snprintf(out_path, cap, "%s", uri + 7);
    else
        snprintf(out_path, cap, "%s", uri);
}

/* ------------------------------------------------------------------ */
/* Transport: Read / Write JSON-RPC Messages                          */
/* ------------------------------------------------------------------ */

static bool lsp_send_payload(LspClient *lsp, const char *payload)
{
    if (!lsp || lsp->in_pipe < 0) return false;
    size_t len = strlen(payload);
    char header[128];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);

    ssize_t nw1 = write(lsp->in_pipe, header, (size_t)hlen);
    ssize_t nw2 = write(lsp->in_pipe, payload, len);
    return (nw1 == hlen && nw2 == (ssize_t)len);
}

/* ------------------------------------------------------------------ */
/* Lifecycle & Handshake                                              */
/* ------------------------------------------------------------------ */

bool lsp_init(LspClient *lsp, const char *server_cmd, const char *root_path)
{
    if (!lsp) return false;
    memset(lsp, 0, sizeof(*lsp));

    lsp->in_pipe = -1;
    lsp->out_pipe = -1;
    lsp->pid = -1;
    lsp->next_id = 1;
    lsp->doc_version = 1;

    snprintf(lsp->server_cmd, sizeof(lsp->server_cmd), "%s",
             server_cmd && server_cmd[0] ? server_cmd : "clangd");
    if (root_path && root_path[0])
        snprintf(lsp->root_path, sizeof(lsp->root_path), "%s", root_path);
    else
        getcwd(lsp->root_path, sizeof(lsp->root_path));

    lsp->rx_cap = LSP_BUFFER_SIZE;
    lsp->rx_buf = malloc(lsp->rx_cap);
    if (!lsp->rx_buf) return false;
    lsp->rx_len = 0;

    /* Create bidirectional pipes */
    int to_child[2];
    int from_child[2];
    if (pipe(to_child) < 0) { free(lsp->rx_buf); return false; }
    if (pipe(from_child) < 0) {
        close(to_child[0]); close(to_child[1]);
        free(lsp->rx_buf); return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        free(lsp->rx_buf);
        return false;
    }

    if (pid == 0) {
        /* Child process: setup stdin/stdout and exec */
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);

        /* Redirect stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);

        char *argv[] = { (char *)lsp->server_cmd, "--background-index", NULL };
        execvp(argv[0], argv);
        _exit(127);
    }

    /* Parent process */
    close(to_child[0]);
    close(from_child[1]);

    lsp->pid = pid;
    lsp->in_pipe = to_child[1];
    lsp->out_pipe = from_child[0];

    /* Set out_pipe non-blocking so read does not pause the frame loop */
    int flags = fcntl(lsp->out_pipe, F_GETFL, 0);
    fcntl(lsp->out_pipe, F_SETFL, flags | O_NONBLOCK);

    lsp->state = LSP_STATE_STARTING;

    /* Send initialize request */
    char root_uri[LSP_MAX_PATH];
    path_to_uri(lsp->root_path, root_uri, sizeof(root_uri));

    char init_req[2048];
    snprintf(init_req, sizeof(init_req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
             "\"processId\":%d,"
             "\"rootUri\":\"%s\","
             "\"capabilities\":{"
             "\"textDocument\":{"
             "\"synchronization\":{\"dynamicRegistration\":false,\"willSave\":false,\"willSaveWaitUntil\":false,\"didSave\":true},"
             "\"completion\":{\"completionItem\":{\"snippetSupport\":false,\"documentationFormat\":[\"plaintext\"]}},"
             "\"hover\":{\"contentFormat\":[\"plaintext\"]},"
             "\"definition\":{\"dynamicRegistration\":false},"
             "\"formatting\":{\"dynamicRegistration\":false}"
             "}}}}",
             lsp->next_id++, (int)getpid(), root_uri);

    lsp_send_payload(lsp, init_req);
    return true;
}

void lsp_shutdown(LspClient *lsp)
{
    if (!lsp || lsp->state == LSP_STATE_STOPPED) return;
    if (lsp->in_pipe >= 0) {
        char req[128];
        snprintf(req, sizeof(req),
                 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\"}",
                 lsp->next_id++);
        lsp_send_payload(lsp, req);

        const char *exit_msg = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";
        lsp_send_payload(lsp, exit_msg);
    }
    lsp->state = LSP_STATE_STOPPED;
}

void lsp_destroy(LspClient *lsp)
{
    if (!lsp) return;
    lsp_shutdown(lsp);

    if (lsp->in_pipe >= 0) { close(lsp->in_pipe); lsp->in_pipe = -1; }
    if (lsp->out_pipe >= 0) { close(lsp->out_pipe); lsp->out_pipe = -1; }

    if (lsp->pid > 0) {
        kill(lsp->pid, SIGTERM);
        int status;
        waitpid(lsp->pid, &status, WNOHANG);
        lsp->pid = -1;
    }

    free(lsp->rx_buf);
    lsp->rx_buf = NULL;
    lsp->rx_len = 0;
    lsp->rx_cap = 0;
}

/* ------------------------------------------------------------------ */
/* Document Synchronization                                           */
/* ------------------------------------------------------------------ */

void lsp_doc_did_open(LspClient *lsp, const char *path, const char *text, size_t len)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    size_t esc_cap = len * 2 + 512;
    char *esc = malloc(esc_cap);
    if (!esc) return;
    escape_json_string(text ? text : "", esc, esc_cap);

    size_t payload_cap = esc_cap + 1024;
    char *payload = malloc(payload_cap);
    if (payload) {
        snprintf(payload, payload_cap,
                 "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
                 "\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"c\",\"version\":%d,\"text\":\"%s\"}}}",
                 uri, lsp->doc_version++, esc);
        lsp_send_payload(lsp, payload);
        free(payload);
    }
    free(esc);
}

void lsp_doc_did_change(LspClient *lsp, const char *path, const char *text, size_t len)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    size_t esc_cap = len * 2 + 512;
    char *esc = malloc(esc_cap);
    if (!esc) return;
    escape_json_string(text ? text : "", esc, esc_cap);

    size_t payload_cap = esc_cap + 1024;
    char *payload = malloc(payload_cap);
    if (payload) {
        snprintf(payload, payload_cap,
                 "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
                 "\"textDocument\":{\"uri\":\"%s\",\"version\":%d},"
                 "\"contentChanges\":[{\"text\":\"%s\"}]}}",
                 uri, lsp->doc_version++, esc);
        lsp_send_payload(lsp, payload);
        free(payload);
    }
    free(esc);
}

void lsp_doc_did_save(LspClient *lsp, const char *path)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    char payload[1536];
    snprintf(payload, sizeof(payload),
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"}}}",
             uri);
    lsp_send_payload(lsp, payload);
}

void lsp_doc_did_close(LspClient *lsp, const char *path)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    char payload[1536];
    snprintf(payload, sizeof(payload),
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"}}}",
             uri);
    lsp_send_payload(lsp, payload);
}

/* ------------------------------------------------------------------ */
/* Requests                                                           */
/* ------------------------------------------------------------------ */

void lsp_request_completion(LspClient *lsp, const char *path, int line, int col)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->completion_req_id = id;
    lsp->completion_pending = true;
    lsp->completion_line = line;
    lsp->completion_col = col;

    char req[1536];
    /* LSP positions are 0-based */
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/completion\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d}}}",
             id, uri, line > 0 ? line - 1 : 0, col > 0 ? col - 1 : 0);
    lsp_send_payload(lsp, req);
}

void lsp_request_hover(LspClient *lsp, const char *path, int line, int col)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->hover_req_id = id;

    char req[1536];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/hover\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d}}}",
             id, uri, line > 0 ? line - 1 : 0, col > 0 ? col - 1 : 0);
    lsp_send_payload(lsp, req);
}

void lsp_request_definition(LspClient *lsp, const char *path, int line, int col)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->definition_req_id = id;

    char req[1536];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/definition\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d}}}",
             id, uri, line > 0 ? line - 1 : 0, col > 0 ? col - 1 : 0);
    lsp_send_payload(lsp, req);
}

void lsp_request_format(LspClient *lsp, const char *path)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->format_req_id = id;
    lsp->format_pending = true;

    char req[1536];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/formatting\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}",
             id, uri);
    lsp_send_payload(lsp, req);
}

/* ------------------------------------------------------------------ */
/* Message Handlers                                                   */
/* ------------------------------------------------------------------ */

static void handle_publish_diagnostics(LspClient *lsp, const char *msg)
{
    const char *params = json_find_property(msg, "params");
    if (!params) return;

    const char *uri_p = json_find_property(params, "uri");
    char uri[LSP_MAX_PATH];
    if (uri_p && json_parse_str(uri_p, uri, sizeof(uri))) {
        uri_to_path(uri, lsp->diag_path, sizeof(lsp->diag_path));
    }

    lsp->diagnostic_count = 0;
    const char *diags = json_find_property(params, "diagnostics");
    if (!diags || *diags != '[') return;

    const char *p = diags + 1;
    while (*p && *p != ']' && lsp->diagnostic_count < LSP_MAX_DIAGNOSTICS) {
        p = json_skip_ws(p);
        if (*p != '{') break;

        LspDiagnostic *d = &lsp->diagnostics[lsp->diagnostic_count];
        memset(d, 0, sizeof(*d));
        d->severity = LSP_SEV_ERROR;

        const char *range = json_find_property(p, "range");
        if (range) {
            const char *start = json_find_property(range, "start");
            if (start) {
                const char *sl = json_find_property(start, "line");
                const char *sc = json_find_property(start, "character");
                d->start_line = sl ? json_parse_int(sl) + 1 : 1;
                d->start_col  = sc ? json_parse_int(sc) + 1 : 1;
            }
            const char *end = json_find_property(range, "end");
            if (end) {
                const char *el = json_find_property(end, "line");
                const char *ec = json_find_property(end, "character");
                d->end_line = el ? json_parse_int(el) + 1 : d->start_line;
                d->end_col  = ec ? json_parse_int(ec) + 1 : d->start_col + 1;
            }
        }

        const char *sev = json_find_property(p, "severity");
        if (sev) d->severity = (LspSeverity)json_parse_int(sev);

        const char *msg_str = json_find_property(p, "message");
        if (msg_str) json_parse_str(msg_str, d->message, sizeof(d->message));

        const char *src_str = json_find_property(p, "source");
        if (src_str) json_parse_str(src_str, d->source, sizeof(d->source));

        lsp->diagnostic_count++;

        /* Advance to next diagnostic item */
        int depth = 0;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) { p++; break; }
            }
            p++;
        }
        p = json_skip_ws(p);
        if (*p == ',') p++;
    }
}

static void handle_completion_response(LspClient *lsp, const char *msg)
{
    lsp->completion_pending = false;
    lsp->completion_count = 0;

    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) return;

    const char *items = result;
    if (*result == '{') {
        const char *sub = json_find_property(result, "items");
        if (sub) items = sub;
    }
    if (*items != '[') return;

    const char *p = items + 1;
    while (*p && *p != ']' && lsp->completion_count < LSP_MAX_COMPLETIONS) {
        p = json_skip_ws(p);
        if (*p != '{') break;

        LspCompletionItem *item = &lsp->completions[lsp->completion_count];
        memset(item, 0, sizeof(*item));

        const char *label = json_find_property(p, "label");
        if (label) json_parse_str(label, item->label, sizeof(item->label));

        const char *kind = json_find_property(p, "kind");
        if (kind) item->kind = (LspCompletionKind)json_parse_int(kind);

        const char *detail = json_find_property(p, "detail");
        if (detail) json_parse_str(detail, item->detail, sizeof(item->detail));

        const char *insert = json_find_property(p, "insertText");
        if (insert)
            json_parse_str(insert, item->insert_text, sizeof(item->insert_text));
        else
            snprintf(item->insert_text, sizeof(item->insert_text), "%s", item->label);

        const char *doc = json_find_property(p, "documentation");
        if (doc) {
            if (*doc == '{') {
                const char *val = json_find_property(doc, "value");
                if (val) json_parse_str(val, item->documentation, sizeof(item->documentation));
            } else {
                json_parse_str(doc, item->documentation, sizeof(item->documentation));
            }
        }

        if (item->label[0])
            lsp->completion_count++;

        int depth = 0;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) { p++; break; }
            }
            p++;
        }
        p = json_skip_ws(p);
        if (*p == ',') p++;
    }
}

static void handle_hover_response(LspClient *lsp, const char *msg)
{
    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) {
        lsp->hover.active = false;
        return;
    }

    const char *contents = json_find_property(result, "contents");
    if (!contents) {
        lsp->hover.active = false;
        return;
    }

    char raw[512] = {0};
    if (*contents == '{') {
        const char *val = json_find_property(contents, "value");
        if (val) json_parse_str(val, raw, sizeof(raw));
    } else if (*contents == '[') {
        const char *first = json_skip_ws(contents + 1);
        if (*first == '{') {
            const char *val = json_find_property(first, "value");
            if (val) json_parse_str(val, raw, sizeof(raw));
        } else if (*first == '\"') {
            json_parse_str(first, raw, sizeof(raw));
        }
    } else if (*contents == '\"') {
        json_parse_str(contents, raw, sizeof(raw));
    }

    if (raw[0]) {
        snprintf(lsp->hover.content, sizeof(lsp->hover.content), "%s", raw);
        lsp->hover.active = true;
    } else {
        lsp->hover.active = false;
    }
}

static void handle_definition_response(LspClient *lsp, const char *msg, App *app)
{
    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) return;

    const char *loc = result;
    if (*result == '[') {
        loc = json_skip_ws(result + 1);
    }
    if (*loc != '{') return;

    const char *uri_p = json_find_property(loc, "uri");
    if (!uri_p) uri_p = json_find_property(loc, "targetUri");
    if (!uri_p) return;

    char uri[LSP_MAX_PATH];
    json_parse_str(uri_p, uri, sizeof(uri));
    uri_to_path(uri, lsp->definition.path, sizeof(lsp->definition.path));

    const char *range = json_find_property(loc, "range");
    if (!range) range = json_find_property(loc, "targetSelectionRange");

    if (range) {
        const char *start = json_find_property(range, "start");
        if (start) {
            const char *sl = json_find_property(start, "line");
            const char *sc = json_find_property(start, "character");
            lsp->definition.line = sl ? json_parse_int(sl) + 1 : 1;
            lsp->definition.col  = sc ? json_parse_int(sc) + 1 : 1;
        }
    }
    lsp->definition.valid = true;

    /* Jump to location in editor */
    if (app && lsp->definition.valid && lsp->definition.path[0]) {
        int target_tab = -1;
        for (int i = 0; i < app->tab_count; ++i) {
            if (strcmp(app->tabs[i].path, lsp->definition.path) == 0) {
                target_tab = i;
                break;
            }
        }
        if (target_tab >= 0) {
            app->tab_active = target_tab;
        } else {
            tab_add(app, lsp->definition.path, NULL);
        }
        Editor *ed = app_ed(app);
        if (ed) {
            editor_goto_line(ed, lsp->definition.line);
        }
    }
}

static void process_lsp_message(LspClient *lsp, const char *json, App *app)
{
    const char *id_p = json_find_property(json, "id");
    int id = id_p ? json_parse_int(id_p) : -1;

    const char *method_p = json_find_property(json, "method");
    char method[64] = {0};
    if (method_p) json_parse_str(method_p, method, sizeof(method));

    /* Initialize response */
    if (lsp->state == LSP_STATE_STARTING && id > 0) {
        const char *result = json_find_property(json, "result");
        if (result) {
            lsp->state = LSP_STATE_RUNNING;
            /* Send initialized notification */
            const char *init_notif = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
            lsp_send_payload(lsp, init_notif);

            /* Open current active editor file if present */
            if (app) {
                Editor *ed = app_ed(app);
                if (ed && ed->path[0]) {
                    size_t len = editor_length(ed);
                    char *text = malloc(len + 1);
                    if (text) {
                        editor_get_text(ed, text, len + 1);
                        lsp_doc_did_open(lsp, ed->path, text, len);
                        free(text);
                    }
                }
            }
        }
        return;
    }

    /* Diagnostics notification */
    if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
        handle_publish_diagnostics(lsp, json);
        return;
    }

    /* Requests matched by ID */
    if (id > 0) {
        if (id == lsp->completion_req_id) {
            handle_completion_response(lsp, json);
        } else if (id == lsp->hover_req_id) {
            handle_hover_response(lsp, json);
        } else if (id == lsp->definition_req_id) {
            handle_definition_response(lsp, json, app);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main Loop Tick                                                     */
/* ------------------------------------------------------------------ */

void lsp_update(App *app)
{
    if (!app) return;
    LspClient *lsp = &app->lsp;
    if (lsp->state == LSP_STATE_STOPPED || lsp->out_pipe < 0) return;

    /* Drain available data from pipe */
    while (true) {
        if (lsp->rx_len + 4096 >= lsp->rx_cap) {
            size_t ncap = lsp->rx_cap * 2;
            char *nbuf = realloc(lsp->rx_buf, ncap);
            if (!nbuf) break;
            lsp->rx_buf = nbuf;
            lsp->rx_cap = ncap;
        }

        ssize_t n = read(lsp->out_pipe, lsp->rx_buf + lsp->rx_len, 4096);
        if (n > 0) {
            lsp->rx_len += (size_t)n;
            lsp->rx_buf[lsp->rx_len] = '\0';
        } else {
            break;
        }
    }

    /* Parse all complete framed messages */
    while (lsp->rx_len > 0) {
        const char *cl = strstr(lsp->rx_buf, "Content-Length:");
        if (!cl) break;

        const char *body_start = strstr(cl, "\r\n\r\n");
        if (!body_start) break;
        body_start += 4;

        int content_len = atoi(cl + 15);
        if (content_len <= 0) break;

        size_t header_len = (size_t)(body_start - lsp->rx_buf);
        if (lsp->rx_len < header_len + (size_t)content_len) {
            /* Full payload not yet received */
            break;
        }

        /* Extract message */
        char *json = malloc((size_t)content_len + 1);
        if (json) {
            memcpy(json, body_start, (size_t)content_len);
            json[content_len] = '\0';
            process_lsp_message(lsp, json, app);
            free(json);
        }

        /* Slide remaining buffer */
        size_t consumed = header_len + (size_t)content_len;
        size_t remain = lsp->rx_len - consumed;
        if (remain > 0)
            memmove(lsp->rx_buf, lsp->rx_buf + consumed, remain);
        lsp->rx_len = remain;
        lsp->rx_buf[lsp->rx_len] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Visual Badges & Queries                                            */
/* ------------------------------------------------------------------ */

int lsp_diagnostics_for_line(const LspClient *lsp, int line, const LspDiagnostic **out_first)
{
    if (!lsp || lsp->diagnostic_count == 0 || line <= 0) return 0;
    int count = 0;
    const LspDiagnostic *first = NULL;
    for (int i = 0; i < lsp->diagnostic_count; ++i) {
        if (lsp->diagnostics[i].start_line <= line && line <= lsp->diagnostics[i].end_line) {
            if (!first) first = &lsp->diagnostics[i];
            count++;
        }
    }
    if (out_first) *out_first = first;
    return count;
}

const char *lsp_completion_kind_name(LspCompletionKind kind)
{
    switch (kind) {
    case LSP_COMP_FUNCTION: case LSP_COMP_METHOD: return "Function";
    case LSP_COMP_VARIABLE: case LSP_COMP_FIELD:  return "Variable";
    case LSP_COMP_CLASS:    case LSP_COMP_STRUCT: return "Type";
    case LSP_COMP_KEYWORD:                        return "Keyword";
    case LSP_COMP_SNIPPET:                        return "Snippet";
    case LSP_COMP_MODULE:                         return "Module";
    case LSP_COMP_CONSTANT:                       return "Constant";
    default:                                      return "Item";
    }
}

const char *lsp_completion_kind_badge(LspCompletionKind kind)
{
    switch (kind) {
    case LSP_COMP_FUNCTION: case LSP_COMP_METHOD: return "fn";
    case LSP_COMP_VARIABLE: case LSP_COMP_FIELD:  return "var";
    case LSP_COMP_CLASS:    case LSP_COMP_STRUCT: return "type";
    case LSP_COMP_KEYWORD:                        return "kw";
    case LSP_COMP_SNIPPET:                        return "snip";
    case LSP_COMP_MODULE:                         return "mod";
    case LSP_COMP_CONSTANT:                       return "const";
    default:                                      return "sym";
    }
}

/* ------------------------------------------------------------------ */
/* Overlay Rendering: Completion Popup & Hover Tooltip               */
/* ------------------------------------------------------------------ */

void lsp_draw_ui(App *app, VkContext *ctx)
{
    if (!app || !ctx) return;
    LspClient *lsp = &app->lsp;
    const Theme *th = theme_current();

    /* 1. Autocompletion Floating Popup */
    if (app->completion_open && lsp->completion_count > 0) {
        float x = app->completion_anchor_x;
        float y = app->completion_anchor_y + 4.0f;
        float w = 360.0f;
        float row_h = app->font.line_height + 4.0f;

        int visible_rows = lsp->completion_count > 8 ? 8 : lsp->completion_count;
        float h = (float)visible_rows * row_h + 8.0f;

        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;

        if (x + w > screen_w - 12.0f) x = screen_w - w - 12.0f;
        if (y + h > screen_h - 32.0f) y = app->completion_anchor_y - h - 4.0f;
        if (x < 12.0f) x = 12.0f;
        if (y < 32.0f) y = 32.0f;

        /* Background & border */
        uint8_t bg[4] = { 32, 34, 40, 245 };
        uint8_t border[4] = { 70, 75, 85, 255 };
        vk_draw_rounded_rect(&app->draw, x, y, w, h, 6.0f, 1.0f, bg);
        vk_draw_rounded_rect(&app->draw, x, y, w, 1.0f, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x, y + h - 1.0f, w, 1.0f, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x, y, 1.0f, h, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x + w - 1.0f, y, 1.0f, h, 0.0f, 1.0f, border);

        int scroll = app->completion_scroll;
        if (app->completion_sel < scroll) scroll = app->completion_sel;
        if (app->completion_sel >= scroll + visible_rows) scroll = app->completion_sel - visible_rows + 1;
        app->completion_scroll = scroll;

        float row_y = y + 4.0f;
        for (int r = 0; r < visible_rows; ++r) {
            int idx = scroll + r;
            if (idx >= lsp->completion_count) break;

            const LspCompletionItem *it = &lsp->completions[idx];
            bool selected = (idx == app->completion_sel);

            if (selected) {
                uint8_t sel_bg[4] = { 60, 110, 200, 180 };
                vk_draw_rounded_rect(&app->draw, x + 4.0f, row_y, w - 8.0f, row_h, 4.0f, 1.0f, sel_bg);
            }

            /* Badge icon */
            const char *badge = lsp_completion_kind_badge(it->kind);
            uint8_t badge_c[4] = { 130, 180, 250, 255 };
            font_draw(&app->font, ctx, x + 10.0f, row_y + app->font.ascent + 2.0f, badge, badge_c);

            /* Label */
            uint8_t label_c[4] = { 240, 240, 240, 255 };
            font_draw(&app->font, ctx, x + 50.0f, row_y + app->font.ascent + 2.0f, it->label, label_c);

            /* Detail text */
            if (it->detail[0]) {
                uint8_t dim_c[4] = { 150, 155, 165, 200 };
                char short_detail[128];
                snprintf(short_detail, sizeof(short_detail), "%s", it->detail);
                float dw = font_measure(&app->font, short_detail);
                if (x + w - dw - 12.0f > x + 180.0f)
                    font_draw(&app->font, ctx, x + w - dw - 12.0f, row_y + app->font.ascent + 2.0f, short_detail, dim_c);
            }

            row_y += row_h;
        }
    }

    /* 2. Hover Tooltip Card */
    if (app->hover_open && lsp->hover.active && lsp->hover.content[0]) {
        float x = app->hover_x + 12.0f;
        float y = app->hover_y + 12.0f;
        float max_w = 460.0f;
        float text_w = font_measure(&app->font, lsp->hover.content);
        float w = text_w > max_w ? max_w : (text_w + 24.0f);
        if (w < 180.0f) w = 180.0f;

        float h = app->font.line_height * 2.0f + 16.0f;

        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;
        if (x + w > screen_w - 12.0f) x = screen_w - w - 12.0f;
        if (y + h > screen_h - 32.0f) y = app->hover_y - h - 12.0f;

        uint8_t bg[4] = { 26, 28, 34, 250 };
        uint8_t edge[4] = { 65, 70, 80, 255 };
        vk_draw_rounded_rect(&app->draw, x, y, w, h, 6.0f, 1.0f, bg);
        vk_draw_rounded_rect(&app->draw, x, y, w, 1.0f, 0.0f, 1.0f, edge);
        vk_draw_rounded_rect(&app->draw, x, y + h - 1.0f, w, 1.0f, 0.0f, 1.0f, edge);
        vk_draw_rounded_rect(&app->draw, x, y, 1.0f, h, 0.0f, 1.0f, edge);
        vk_draw_rounded_rect(&app->draw, x + w - 1.0f, y, 1.0f, h, 0.0f, 1.0f, edge);

        font_draw(&app->font, ctx, x + 12.0f, y + app->font.ascent + 8.0f, lsp->hover.content, th->fg);
    }
}

/* ------------------------------------------------------------------ */
/* Overlay Event Handling                                             */
/* ------------------------------------------------------------------ */

bool lsp_handle_event(App *app, const SDL_Event *ev)
{
    if (!app || !ev) return false;
    LspClient *lsp = &app->lsp;

    /* If hover is open, dismiss on any key or mouse press */
    if (app->hover_open) {
        if (ev->type == SDL_EVENT_KEY_DOWN ||
            ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            app->hover_open = false;
            lsp->hover.active = false;
        }
    }

    if (!app->completion_open || lsp->completion_count == 0)
        return false;

    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode key = ev->key.key;

        if (key == SDLK_ESCAPE) {
            app->completion_open = false;
            return true;
        }

        if (key == SDLK_UP) {
            if (app->completion_sel > 0)
                app->completion_sel--;
            else
                app->completion_sel = lsp->completion_count - 1;
            return true;
        }

        if (key == SDLK_DOWN) {
            if (app->completion_sel + 1 < lsp->completion_count)
                app->completion_sel++;
            else
                app->completion_sel = 0;
            return true;
        }

        if (key == SDLK_PAGEUP) {
            app->completion_sel -= 6;
            if (app->completion_sel < 0) app->completion_sel = 0;
            return true;
        }

        if (key == SDLK_PAGEDOWN) {
            app->completion_sel += 6;
            if (app->completion_sel >= lsp->completion_count)
                app->completion_sel = lsp->completion_count - 1;
            return true;
        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_TAB) {
            if (app->completion_sel >= 0 && app->completion_sel < lsp->completion_count) {
                const LspCompletionItem *it = &lsp->completions[app->completion_sel];
                Editor *ed = app_ed(app);
                if (ed) {
                    /* Insert item insert_text into editor */
                    editor_insert(ed, it->insert_text, strlen(it->insert_text));
                }
            }
            app->completion_open = false;
            return true;
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        app->completion_open = false;
    }

    return false;
}
