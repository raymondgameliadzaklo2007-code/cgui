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
#include <strings.h>
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

static const char *json_skip_value(const char *p)
{
    p = json_skip_ws(p);
    if (!*p) return p;
    if (*p == '\"') {
        p++;
        while (*p) {
            if (*p == '\\' && *(p + 1)) p += 2;
            else if (*p == '\"') { p++; break; }
            else p++;
        }
        return p;
    }
    if (*p == '{') {
        p++;
        int depth = 1;
        while (*p && depth > 0) {
            p = json_skip_ws(p);
            if (*p == '\"') {
                p = json_skip_value(p);
            } else if (*p == '{') {
                depth++; p++;
            } else if (*p == '}') {
                depth--; p++;
            } else {
                p++;
            }
        }
        return p;
    }
    if (*p == '[') {
        p++;
        int depth = 1;
        while (*p && depth > 0) {
            p = json_skip_ws(p);
            if (*p == '\"') {
                p = json_skip_value(p);
            } else if (*p == '[') {
                depth++; p++;
            } else if (*p == ']') {
                depth--; p++;
            } else {
                p++;
            }
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        p++;
    return p;
}

static const char *json_object_get(const char *obj, const char *key)
{
    if (!obj || !key) return NULL;
    obj = json_skip_ws(obj);
    if (*obj != '{') return NULL;
    const char *p = obj + 1;
    size_t klen = strlen(key);

    while (*p && *p != '}') {
        p = json_skip_ws(p);
        if (*p == '\"') {
            const char *kstart = p + 1;
            const char *kend = strchr(kstart, '\"');
            if (!kend) break;
            size_t len = (size_t)(kend - kstart);
            p = json_skip_ws(kend + 1);
            if (*p != ':') break;
            p = json_skip_ws(p + 1);

            if (len == klen && memcmp(kstart, key, klen) == 0) {
                return p;
            }
            p = json_skip_value(p);
            p = json_skip_ws(p);
            if (*p == ',') p++;
        } else {
            p++;
        }
    }
    return NULL;
}

static void clean_hover_markdown(char *str)
{
    if (!str || !*str) return;
    char out[512];
    size_t o = 0;
    size_t i = 0;

    while (str[i] && o + 1 < sizeof(out)) {
        if (strncmp(str + i, "```", 3) == 0) {
            i += 3;
            while (str[i] && str[i] != '\n') i++;
            if (str[i] == '\n') i++;
            continue;
        }
        if (str[i] == '`') {
            i++;
            continue;
        }
        out[o++] = str[i++];
    }
    out[o] = '\0';

    char *start = out;
    while (*start == '\n' || *start == '\r' || *start == ' ') start++;
    char *end = start + strlen(start);
    while (end > start && (*(end - 1) == '\n' || *(end - 1) == '\r' || *(end - 1) == ' ')) end--;
    *end = '\0';
    memmove(str, start, (size_t)(end - start + 1));
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

static void lsp_ensure_buf_capacity(char **buf, size_t *cap, size_t needed)
{
    if (needed > *cap) {
        size_t ncap = *cap ? *cap : 65536;
        while (ncap < needed) ncap *= 2;
        char *nb = realloc(*buf, ncap);
        if (nb) { *buf = nb; *cap = ncap; }
    }
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

    lsp->tx_cap = LSP_BUFFER_SIZE;
    lsp->tx_buf = malloc(lsp->tx_cap);
    lsp->esc_cap = LSP_BUFFER_SIZE;
    lsp->esc_buf = malloc(lsp->esc_cap);

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

    free(lsp->tx_buf);
    lsp->tx_buf = NULL;
    lsp->tx_cap = 0;
    free(lsp->esc_buf);
    lsp->esc_buf = NULL;
    lsp->esc_cap = 0;
}

/* ------------------------------------------------------------------ */
/* Document Synchronization                                           */
/* ------------------------------------------------------------------ */

void lsp_doc_did_open(LspClient *lsp, const char *path, const char *text, size_t len)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    size_t esc_needed = len * 2 + 512;
    lsp_ensure_buf_capacity(&lsp->esc_buf, &lsp->esc_cap, esc_needed);
    if (!lsp->esc_buf) return;
    escape_json_string(text ? text : "", lsp->esc_buf, lsp->esc_cap);

    size_t payload_needed = esc_needed + 1024;
    lsp_ensure_buf_capacity(&lsp->tx_buf, &lsp->tx_cap, payload_needed);
    if (!lsp->tx_buf) return;

    snprintf(lsp->tx_buf, lsp->tx_cap,
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"c\",\"version\":%d,\"text\":\"%s\"}}}",
             uri, lsp->doc_version++, lsp->esc_buf);
    lsp_send_payload(lsp, lsp->tx_buf);
}

void lsp_doc_did_change(LspClient *lsp, const char *path, const char *text, size_t len)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    size_t esc_needed = len * 2 + 512;
    lsp_ensure_buf_capacity(&lsp->esc_buf, &lsp->esc_cap, esc_needed);
    if (!lsp->esc_buf) return;
    escape_json_string(text ? text : "", lsp->esc_buf, lsp->esc_cap);

    size_t payload_needed = esc_needed + 1024;
    lsp_ensure_buf_capacity(&lsp->tx_buf, &lsp->tx_cap, payload_needed);
    if (!lsp->tx_buf) return;

    snprintf(lsp->tx_buf, lsp->tx_cap,
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\",\"version\":%d},"
             "\"contentChanges\":[{\"text\":\"%s\"}]}}",
             uri, lsp->doc_version++, lsp->esc_buf);
    lsp_send_payload(lsp, lsp->tx_buf);
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

void lsp_filter_completions(LspClient *lsp, const char *prefix)
{
    if (!lsp) return;
    lsp->completion_count = 0;
    if (prefix) {
        snprintf(lsp->completion_prefix, sizeof(lsp->completion_prefix), "%s", prefix);
    } else {
        lsp->completion_prefix[0] = '\0';
    }

    size_t plen = strlen(lsp->completion_prefix);

    /* Phase 1: Exact prefix matches (case-insensitive) */
    for (int i = 0; i < lsp->raw_completion_count && lsp->completion_count < LSP_MAX_COMPLETIONS; ++i) {
        const LspCompletionItem *raw = &lsp->raw_completions[i];
        if (plen == 0 || strncasecmp(raw->label, lsp->completion_prefix, plen) == 0) {
            lsp->completions[lsp->completion_count++] = *raw;
        }
    }

    /* Phase 2: Substring matches (case-insensitive) if room and prefix >= 2 */
    if (plen >= 2 && lsp->completion_count < LSP_MAX_COMPLETIONS) {
        for (int i = 0; i < lsp->raw_completion_count && lsp->completion_count < LSP_MAX_COMPLETIONS; ++i) {
            const LspCompletionItem *raw = &lsp->raw_completions[i];
            if (strncasecmp(raw->label, lsp->completion_prefix, plen) == 0) continue;

            if (strcasestr(raw->label, lsp->completion_prefix) != NULL) {
                lsp->completions[lsp->completion_count++] = *raw;
            }
        }
    }
}

static void handle_completion_response(LspClient *lsp, const char *msg, App *app)
{
    lsp->completion_pending = false;
    lsp->raw_completion_count = 0;
    lsp->completion_count = 0;

    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) return;

    const char *items = result;
    if (*result == '{') {
        const char *sub = json_object_get(result, "items");
        if (sub) items = sub;
    }
    items = json_skip_ws(items);
    if (*items != '[') return;

    const char *p = items + 1;
    while (*p && *p != ']' && lsp->raw_completion_count < LSP_MAX_RAW_COMPLETIONS) {
        p = json_skip_ws(p);
        if (*p != '{') break;

        LspCompletionItem *item = &lsp->raw_completions[lsp->raw_completion_count];
        memset(item, 0, sizeof(*item));

        p++; /* skip '{' */
        while (*p && *p != '}') {
            p = json_skip_ws(p);
            if (*p == '\"') {
                p++;
                const char *kstart = p;
                while (*p && *p != '\"') p++;
                size_t klen = (size_t)(p - kstart);
                if (*p == '\"') p++;
                p = json_skip_ws(p);
                if (*p == ':') p++;
                p = json_skip_ws(p);

                if (klen == 5 && memcmp(kstart, "label", 5) == 0) {
                    json_parse_str(p, item->label, sizeof(item->label));
                    p = json_skip_value(p);
                } else if (klen == 4 && memcmp(kstart, "kind", 4) == 0) {
                    item->kind = (LspCompletionKind)atoi(p);
                    p = json_skip_value(p);
                } else if (klen == 6 && memcmp(kstart, "detail", 6) == 0) {
                    json_parse_str(p, item->detail, sizeof(item->detail));
                    p = json_skip_value(p);
                } else if (klen == 10 && memcmp(kstart, "insertText", 10) == 0) {
                    json_parse_str(p, item->insert_text, sizeof(item->insert_text));
                    p = json_skip_value(p);
                } else if (klen == 13 && memcmp(kstart, "documentation", 13) == 0) {
                    if (*p == '\"') {
                        json_parse_str(p, item->documentation, sizeof(item->documentation));
                    } else if (*p == '{') {
                        const char *val = json_object_get(p, "value");
                        if (val) json_parse_str(val, item->documentation, sizeof(item->documentation));
                    }
                    p = json_skip_value(p);
                } else {
                    p = json_skip_value(p);
                }

                p = json_skip_ws(p);
                if (*p == ',') p++;
            } else {
                p++;
            }
        }
        if (*p == '}') p++;

        if (item->label[0]) {
            if (!item->insert_text[0]) {
                snprintf(item->insert_text, sizeof(item->insert_text), "%s", item->label);
            }
            lsp->raw_completion_count++;
        }

        p = json_skip_ws(p);
        if (*p == ',') p++;
    }

    if (app) {
        Editor *ed = app_ed(app);
        if (ed) {
            char prefix[64] = {0};
            editor_current_word_prefix(ed, prefix, sizeof(prefix));
            lsp_filter_completions(lsp, prefix);

            if (lsp->completion_count > 0) {
                float cx = 0, cy = 0;
                editor_caret_pixel_pos(ed, &cx, &cy);
                app->completion_anchor_x = cx;
                app->completion_anchor_y = cy;
                app->completion_open = true;
                app->completion_sel = 0;
                app->completion_scroll = 0;
            } else {
                app->completion_open = false;
            }
        }
    }
}

static void handle_hover_response(LspClient *lsp, const char *msg, App *app)
{
    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) {
        lsp->hover.active = false;
        if (app) app->hover_open = false;
        return;
    }

    const char *contents = json_object_get(result, "contents");
    if (!contents) {
        contents = json_find_property(result, "contents");
    }
    if (!contents) {
        lsp->hover.active = false;
        if (app) app->hover_open = false;
        return;
    }

    char raw[512] = {0};
    contents = json_skip_ws(contents);
    if (*contents == '{') {
        const char *val = json_object_get(contents, "value");
        if (val) json_parse_str(val, raw, sizeof(raw));
    } else if (*contents == '[') {
        const char *first = json_skip_ws(contents + 1);
        if (*first == '{') {
            const char *val = json_object_get(first, "value");
            if (val) json_parse_str(val, raw, sizeof(raw));
        } else if (*first == '\"') {
            json_parse_str(first, raw, sizeof(raw));
        }
    } else if (*contents == '\"') {
        json_parse_str(contents, raw, sizeof(raw));
    }

    clean_hover_markdown(raw);

    if (raw[0]) {
        snprintf(lsp->hover.content, sizeof(lsp->hover.content), "%s", raw);
        lsp->hover.active = true;
        if (app) {
            app->hover_open = true;
            if (app->hover_x == 0.0f && app->hover_y == 0.0f) {
                Editor *ed = app_ed(app);
                if (ed) {
                    float cx = 0, cy = 0;
                    editor_caret_pixel_pos(ed, &cx, &cy);
                    app->hover_x = cx;
                    app->hover_y = cy;
                }
            }
        }
    } else {
        lsp->hover.active = false;
        if (app) app->hover_open = false;
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
            handle_completion_response(lsp, json, app);
        } else if (id == lsp->hover_req_id) {
            handle_hover_response(lsp, json, app);
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

static void truncate_text_to_width(const Font *font, const char *src, float max_w, char *out, size_t out_cap)
{
    if (!font || !src || out_cap == 0) {
        if (out && out_cap > 0) out[0] = '\0';
        return;
    }
    if (max_w <= 0.0f) {
        out[0] = '\0';
        return;
    }
    float full_w = font_measure(font, src);
    if (full_w <= max_w) {
        snprintf(out, out_cap, "%s", src);
        return;
    }

    const char *ellipsis = "…";
    float ell_w = font_measure(font, ellipsis);
    float avail = max_w - ell_w;
    if (avail <= 0.0f) {
        snprintf(out, out_cap, "…");
        return;
    }

    size_t len = strlen(src);
    size_t best_i = 0;
    float cur_w = 0.0f;
    for (size_t i = 0; i < len; ++i) {
        char tmp[2] = { src[i], '\0' };
        float cw = font_measure(font, tmp);
        if (cur_w + cw > avail) break;
        cur_w += cw;
        best_i = i + 1;
    }

    if (best_i >= out_cap - 4) best_i = out_cap - 5;
    memcpy(out, src, best_i);
    out[best_i] = '\0';
    strncat(out, ellipsis, out_cap - strlen(out) - 1);
}

void lsp_draw_ui(App *app, VkContext *ctx)
{
    if (!app || !ctx) return;
    LspClient *lsp = &app->lsp;
    const Theme *th = theme_current();

    /* 1. Autocompletion Floating Popup */
    if (app->completion_open && lsp->completion_count > 0) {
        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;

        int visible_rows = lsp->completion_count > 8 ? 8 : lsp->completion_count;
        int scroll = app->completion_scroll;
        if (app->completion_sel < scroll) scroll = app->completion_sel;
        if (app->completion_sel >= scroll + visible_rows) scroll = app->completion_sel - visible_rows + 1;
        if (scroll < 0) scroll = 0;
        app->completion_scroll = scroll;

        /* Dynamically measure visible items to calculate optimal popup width */
        float max_lbl_w = 0.0f;
        float max_det_w = 0.0f;
        for (int r = 0; r < visible_rows; ++r) {
            int idx = scroll + r;
            if (idx >= lsp->completion_count) break;
            const LspCompletionItem *it = &lsp->completions[idx];
            float lw = font_measure(&app->font, it->label);
            if (lw > max_lbl_w) max_lbl_w = lw;
            if (it->detail[0]) {
                float dw = font_measure(&app->font, it->detail);
                if (dw > max_det_w) max_det_w = dw;
            }
        }

        /* Desired width: badge (42px) + label + gap + detail + padding */
        float desired_w = 46.0f + max_lbl_w + (max_det_w > 0.0f ? max_det_w + 32.0f : 24.0f);
        float w = desired_w;
        if (w < 340.0f) w = 340.0f;
        if (w > 560.0f) w = 560.0f;
        if (w > screen_w - 24.0f) w = screen_w - 24.0f;

        float row_h = app->font.line_height + 4.0f;
        float h = (float)visible_rows * row_h + 8.0f;

        float x = app->completion_anchor_x;
        float y = app->completion_anchor_y + 4.0f;

        if (x + w > screen_w - 12.0f) x = screen_w - w - 12.0f;
        if (y + h > screen_h - 32.0f) y = app->completion_anchor_y - h - 4.0f;
        if (x < 12.0f) x = 12.0f;
        if (y < 32.0f) y = 32.0f;

        /* Background & border */
        uint8_t bg[4] = { 28, 30, 36, 250 };
        uint8_t border[4] = { 65, 70, 80, 255 };
        vk_draw_rounded_rect(&app->draw, x, y, w, h, 6.0f, 1.0f, bg);
        vk_draw_rounded_rect(&app->draw, x, y, w, 1.0f, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x, y + h - 1.0f, w, 1.0f, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x, y, 1.0f, h, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x + w - 1.0f, y, 1.0f, h, 0.0f, 1.0f, border);

        float content_x = x + 42.0f;
        float right_edge = x + w - 10.0f;
        float avail_w = right_edge - content_x;

        float row_y = y + 4.0f;
        for (int r = 0; r < visible_rows; ++r) {
            int idx = scroll + r;
            if (idx >= lsp->completion_count) break;

            const LspCompletionItem *it = &lsp->completions[idx];
            bool selected = (idx == app->completion_sel);

            if (selected) {
                uint8_t sel_bg[4] = { 60, 110, 200, 200 };
                vk_draw_rounded_rect(&app->draw, x + 4.0f, row_y, w - 8.0f, row_h, 4.0f, 1.0f, sel_bg);
            }

            /* Badge icon */
            const char *badge = lsp_completion_kind_badge(it->kind);
            uint8_t badge_c[4] = { 130, 180, 250, 255 };
            font_draw(&app->font, ctx, x + 8.0f, row_y + app->font.ascent + 2.0f, badge, badge_c);

            /* Format label and detail with strict width bounds */
            char draw_label[128];
            char draw_detail[128];

            if (it->detail[0]) {
                float max_d_w = avail_w * 0.45f;
                truncate_text_to_width(&app->font, it->detail, max_d_w, draw_detail, sizeof(draw_detail));
                float actual_d_w = font_measure(&app->font, draw_detail);

                float max_l_w = avail_w - actual_d_w - 12.0f;
                truncate_text_to_width(&app->font, it->label, max_l_w, draw_label, sizeof(draw_label));

                uint8_t label_c[4] = { 240, 240, 240, 255 };
                font_draw(&app->font, ctx, content_x, row_y + app->font.ascent + 2.0f, draw_label, label_c);

                uint8_t dim_c[4] = { 150, 155, 165, 210 };
                float detail_x = right_edge - actual_d_w;
                font_draw(&app->font, ctx, detail_x, row_y + app->font.ascent + 2.0f, draw_detail, dim_c);
            } else {
                truncate_text_to_width(&app->font, it->label, avail_w, draw_label, sizeof(draw_label));
                uint8_t label_c[4] = { 240, 240, 240, 255 };
                font_draw(&app->font, ctx, content_x, row_y + app->font.ascent + 2.0f, draw_label, label_c);
            }

            row_y += row_h;
        }
    }

    /* 2. Hover Tooltip Card */
    if (app->hover_open && lsp->hover.active && lsp->hover.content[0]) {
        float x = app->hover_x + 12.0f;
        float y = app->hover_y + 12.0f;

        /* Measure multiline bounds */
        int line_count = 1;
        float max_line_w = 0.0f;
        const char *cur = lsp->hover.content;
        while (*cur) {
            const char *nl = strchr(cur, '\n');
            size_t n = nl ? (size_t)(nl - cur) : strlen(cur);
            char tmp[256];
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, cur, n);
            tmp[n] = 0;
            float lw = font_measure(&app->font, tmp);
            if (lw > max_line_w) max_line_w = lw;
            if (!nl) break;
            cur = nl + 1;
            line_count++;
        }

        float w = max_line_w + 24.0f;
        if (w < 180.0f) w = 180.0f;
        if (w > 560.0f) w = 560.0f;

        float h = (float)line_count * app->font.line_height + 16.0f;

        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;
        if (x + w > screen_w - 12.0f) x = screen_w - w - 12.0f;
        if (y + h > screen_h - 32.0f) y = app->hover_y - h - 12.0f;
        if (x < 12.0f) x = 12.0f;
        if (y < 32.0f) y = 32.0f;

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
            app->completion_req_pending = false;
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
                    editor_insert_completion(ed, it->insert_text);
                }
            }
            app->completion_open = false;
            return true;
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        app->completion_open = false;
        app->completion_req_pending = false;
    }

    return false;
}
