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

void lsp_request_signature_help(LspClient *lsp, const char *path, int line, int col)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->sig_help_req_id = id;

    char req[1536];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/signatureHelp\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d}}}",
             id, uri, line > 0 ? line - 1 : 0, col > 0 ? col - 1 : 0);
    lsp_send_payload(lsp, req);
}

void lsp_request_references(LspClient *lsp, const char *path, int line, int col, const char *symbol)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->reference_req_id = id;
    if (symbol) {
        snprintf(lsp->reference_symbol, sizeof(lsp->reference_symbol), "%s", symbol);
    } else {
        lsp->reference_symbol[0] = '\0';
    }

    char req[1536];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/references\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d},"
             "\"context\":{\"includeDeclaration\":true}}}",
             id, uri, line > 0 ? line - 1 : 0, col > 0 ? col - 1 : 0);
    lsp_send_payload(lsp, req);
}

void lsp_request_rename(LspClient *lsp, const char *path, int line, int col, const char *old_name, const char *new_name)
{
    if (!lsp || lsp->state != LSP_STATE_RUNNING || !path || !path[0] || !new_name || !new_name[0]) return;

    char uri[LSP_MAX_PATH];
    path_to_uri(path, uri, sizeof(uri));

    int id = lsp->next_id++;
    lsp->rename_req_id = id;
    lsp->rename_pending = true;
    snprintf(lsp->rename_old, sizeof(lsp->rename_old), "%s", old_name ? old_name : "");
    snprintf(lsp->rename_new, sizeof(lsp->rename_new), "%s", new_name);

    char esc_new[128];
    escape_json_string(new_name, esc_new, sizeof(esc_new));

    char req[1536];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/rename\",\"params\":{"
             "\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d},"
             "\"newName\":\"%s\"}}",
             id, uri, line > 0 ? line - 1 : 0, col > 0 ? col - 1 : 0, esc_new);
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

static void handle_signature_help_response(LspClient *lsp, const char *msg, App *app)
{
    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) {
        lsp->sig_help.active = false;
        if (app) app->sig_help_open = false;
        return;
    }

    const char *sigs = json_find_property(result, "signatures");
    if (!sigs) sigs = json_object_get(result, "signatures");
    if (!sigs || *sigs != '[') {
        lsp->sig_help.active = false;
        if (app) app->sig_help_open = false;
        return;
    }

    const char *p = json_skip_ws(sigs + 1);
    if (*p != '{') {
        lsp->sig_help.active = false;
        if (app) app->sig_help_open = false;
        return;
    }

    memset(&lsp->sig_help, 0, sizeof(lsp->sig_help));

    const char *act_par_p = json_find_property(result, "activeParameter");
    if (act_par_p) {
        lsp->sig_help.active_param = json_parse_int(act_par_p);
    }

    const char *lbl_p = json_find_property(p, "label");
    if (lbl_p) {
        json_parse_str(lbl_p, lsp->sig_help.label, sizeof(lsp->sig_help.label));
    }

    const char *doc_p = json_find_property(p, "documentation");
    if (doc_p) {
        if (*doc_p == '\"') {
            json_parse_str(doc_p, lsp->sig_help.doc, sizeof(lsp->sig_help.doc));
        } else if (*doc_p == '{') {
            const char *val = json_object_get(doc_p, "value");
            if (val) json_parse_str(val, lsp->sig_help.doc, sizeof(lsp->sig_help.doc));
        }
        clean_hover_markdown(lsp->sig_help.doc);
    }

    /* Parameters */
    const char *params = json_find_property(p, "parameters");
    if (params && *params == '[') {
        const char *param_it = params + 1;
        while (*param_it && *param_it != ']' && lsp->sig_help.param_count < 16) {
            param_it = json_skip_ws(param_it);
            if (*param_it != '{') break;
            LspSignatureParam *sp = &lsp->sig_help.params[lsp->sig_help.param_count];
            sp->start_offset = -1;
            sp->end_offset = -1;

            const char *plbl = json_find_property(param_it, "label");
            if (plbl) {
                if (*plbl == '\"') {
                    json_parse_str(plbl, sp->label, sizeof(sp->label));
                    const char *found = strstr(lsp->sig_help.label, sp->label);
                    if (found) {
                        sp->start_offset = (int)(found - lsp->sig_help.label);
                        sp->end_offset = sp->start_offset + (int)strlen(sp->label);
                    }
                } else if (*plbl == '[') {
                    const char *offs = plbl + 1;
                    sp->start_offset = json_parse_int(offs);
                    offs = strchr(offs, ',');
                    if (offs) sp->end_offset = json_parse_int(offs + 1);
                    if (sp->start_offset >= 0 && sp->end_offset > sp->start_offset &&
                        (size_t)sp->end_offset <= strlen(lsp->sig_help.label)) {
                        size_t l = (size_t)(sp->end_offset - sp->start_offset);
                        if (l >= sizeof(sp->label)) l = sizeof(sp->label) - 1;
                        memcpy(sp->label, lsp->sig_help.label + sp->start_offset, l);
                        sp->label[l] = '\0';
                    }
                }
            }

            lsp->sig_help.param_count++;

            int depth = 0;
            while (*param_it) {
                if (*param_it == '{') depth++;
                else if (*param_it == '}') {
                    depth--;
                    if (depth == 0) { param_it++; break; }
                }
                param_it++;
            }
            param_it = json_skip_ws(param_it);
            if (*param_it == ',') param_it++;
        }
    }

    if (lsp->sig_help.label[0]) {
        lsp->sig_help.active = true;
        if (app) {
            app->sig_help_open = true;
            Editor *ed = app_ed(app);
            if (ed) {
                float cx = 0, cy = 0;
                editor_caret_pixel_pos(ed, &cx, &cy);
                app->sig_help_x = cx;
                app->sig_help_y = cy;
            }
        }
    } else {
        lsp->sig_help.active = false;
        if (app) app->sig_help_open = false;
    }
}

static void handle_references_response(LspClient *lsp, const char *msg, App *app)
{
    lsp->reference_count = 0;
    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) return;

    if (*result != '[') return;
    const char *p = result + 1;

    while (*p && *p != ']' && lsp->reference_count < LSP_MAX_REFERENCES) {
        p = json_skip_ws(p);
        if (*p != '{') break;

        LspReferenceItem *ref = &lsp->references[lsp->reference_count];
        memset(ref, 0, sizeof(*ref));

        const char *uri_p = json_find_property(p, "uri");
        if (uri_p) {
            char uri[LSP_MAX_PATH];
            json_parse_str(uri_p, uri, sizeof(uri));
            uri_to_path(uri, ref->path, sizeof(ref->path));
            const char *slash = strrchr(ref->path, '/');
            snprintf(ref->filename, sizeof(ref->filename), "%s", slash ? slash + 1 : ref->path);
        }

        const char *range = json_find_property(p, "range");
        if (range) {
            const char *start = json_find_property(range, "start");
            if (start) {
                const char *sl = json_find_property(start, "line");
                const char *sc = json_find_property(start, "character");
                ref->line = sl ? json_parse_int(sl) + 1 : 1;
                ref->col  = sc ? json_parse_int(sc) + 1 : 1;
            }
        }

        ref->preview[0] = '\0';
        if (app && ref->path[0]) {
            Editor *ed_match = NULL;
            for (int t = 0; t < app->tab_count; ++t) {
                if (strcmp(app->tabs[t].path, ref->path) == 0) {
                    ed_match = &app->tabs[t];
                    break;
                }
            }
            if (ed_match) {
                size_t ppos = editor_pos_from_line_col(ed_match, ref->line, 1);
                size_t len = editor_length(ed_match);
                size_t o = 0;
                while (ppos < len && (ed_match->buf ? false : false)) ppos++;
                while (ppos < len && o + 1 < sizeof(ref->preview)) {
                    /* Read line chars */
                    char c = '\0';
                    if (ed_match->gap_start && ppos < ed_match->gap_start) {
                        c = ed_match->buf[ppos];
                    } else if (ed_match->buf) {
                        c = ed_match->buf[ppos + (ed_match->gap_end - ed_match->gap_start)];
                    }
                    if (c == '\n' || c == '\r') break;
                    ref->preview[o++] = c;
                    ppos++;
                }
                ref->preview[o] = '\0';
            } else {
                FILE *f = fopen(ref->path, "r");
                if (f) {
                    char linebuf[512];
                    int cur_l = 1;
                    while (fgets(linebuf, sizeof(linebuf), f)) {
                        if (cur_l == ref->line) {
                            char *s = linebuf;
                            while (*s == ' ' || *s == '\t') s++;
                            char *nl = strpbrk(s, "\r\n");
                            if (nl) *nl = '\0';
                            snprintf(ref->preview, sizeof(ref->preview), "%s", s);
                            break;
                        }
                        cur_l++;
                    }
                    fclose(f);
                }
            }
        }

        if (ref->path[0]) {
            lsp->reference_count++;
        }

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

    if (app && lsp->reference_count > 0) {
        close_overlays(app);
        app->ref_picker_open = true;
        app->ref_picker_sel = 0;
        app->ref_picker_scroll = 0;
    }
}

static void handle_rename_response(LspClient *lsp, const char *msg, App *app)
{
    lsp->rename_pending = false;
    const char *result = json_find_property(msg, "result");
    if (!result || strncmp(result, "null", 4) == 0) return;

    LspTextEdit edits[LSP_MAX_RENAME_EDITS];
    int edit_count = 0;

    const char *changes = json_find_property(result, "changes");
    if (changes && *changes == '{') {
        const char *p = changes + 1;
        while (*p && *p != '}' && edit_count < LSP_MAX_RENAME_EDITS) {
            p = json_skip_ws(p);
            if (*p != '\"') break;
            char uri[LSP_MAX_PATH];
            if (!json_parse_str(p, uri, sizeof(uri))) break;
            char path[LSP_MAX_PATH];
            uri_to_path(uri, path, sizeof(path));

            p = strchr(p + 1, '\"');
            if (!p) break;
            p = json_skip_ws(p + 1);
            if (*p == ':') p++;
            p = json_skip_ws(p);

            if (*p == '[') {
                p++;
                while (*p && *p != ']' && edit_count < LSP_MAX_RENAME_EDITS) {
                    p = json_skip_ws(p);
                    if (*p != '{') break;

                    LspTextEdit *te = &edits[edit_count];
                    memset(te, 0, sizeof(*te));
                    snprintf(te->path, sizeof(te->path), "%s", path);

                    const char *range = json_find_property(p, "range");
                    if (range) {
                        const char *st = json_find_property(range, "start");
                        if (st) {
                            const char *sl = json_find_property(st, "line");
                            const char *sc = json_find_property(st, "character");
                            te->start_line = sl ? json_parse_int(sl) + 1 : 1;
                            te->start_col  = sc ? json_parse_int(sc) + 1 : 1;
                        }
                        const char *en = json_find_property(range, "end");
                        if (en) {
                            const char *el = json_find_property(en, "line");
                            const char *ec = json_find_property(en, "character");
                            te->end_line = el ? json_parse_int(el) + 1 : te->start_line;
                            te->end_col  = ec ? json_parse_int(ec) + 1 : te->start_col;
                        }
                    }
                    const char *nt = json_find_property(p, "newText");
                    if (nt) {
                        json_parse_str(nt, te->new_text, sizeof(te->new_text));
                    }

                    edit_count++;

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

            p = json_skip_ws(p);
            if (*p == ']') p++;
            p = json_skip_ws(p);
            if (*p == ',') p++;
        }
    }

    if (edit_count == 0 && app) {
        const char *doc_changes = json_find_property(result, "documentChanges");
        if (doc_changes && *doc_changes == '[') {
            const char *p = doc_changes + 1;
            while (*p && *p != ']' && edit_count < LSP_MAX_RENAME_EDITS) {
                p = json_skip_ws(p);
                if (*p != '{') break;
                char path[LSP_MAX_PATH] = {0};
                const char *td = json_find_property(p, "textDocument");
                if (td) {
                    const char *u = json_find_property(td, "uri");
                    if (u) {
                        char uri[LSP_MAX_PATH];
                        json_parse_str(u, uri, sizeof(uri));
                        uri_to_path(uri, path, sizeof(path));
                    }
                }
                const char *edits_arr = json_find_property(p, "edits");
                if (edits_arr && *edits_arr == '[') {
                    const char *ep = edits_arr + 1;
                    while (*ep && *ep != ']' && edit_count < LSP_MAX_RENAME_EDITS) {
                        ep = json_skip_ws(ep);
                        if (*ep != '{') break;
                        LspTextEdit *te = &edits[edit_count];
                        memset(te, 0, sizeof(*te));
                        snprintf(te->path, sizeof(te->path), "%s", path);
                        const char *range = json_find_property(ep, "range");
                        if (range) {
                            const char *st = json_find_property(range, "start");
                            if (st) {
                                const char *sl = json_find_property(st, "line");
                                const char *sc = json_find_property(st, "character");
                                te->start_line = sl ? json_parse_int(sl) + 1 : 1;
                                te->start_col  = sc ? json_parse_int(sc) + 1 : 1;
                            }
                            const char *en = json_find_property(range, "end");
                            if (en) {
                                const char *el = json_find_property(en, "line");
                                const char *ec = json_find_property(en, "character");
                                te->end_line = el ? json_parse_int(el) + 1 : te->start_line;
                                te->end_col  = ec ? json_parse_int(ec) + 1 : te->start_col;
                            }
                        }
                        const char *nt = json_find_property(ep, "newText");
                        if (nt) json_parse_str(nt, te->new_text, sizeof(te->new_text));
                        edit_count++;

                        int depth = 0;
                        while (*ep) {
                            if (*ep == '{') depth++;
                            else if (*ep == '}') {
                                depth--;
                                if (depth == 0) { ep++; break; }
                            }
                            ep++;
                        }
                        ep = json_skip_ws(ep);
                        if (*ep == ',') ep++;
                    }
                }

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
    }

    if (edit_count == 0 || !app) return;

    /* Sort edits descending: highest line and column first so character indices remain valid */
    for (int i = 0; i < edit_count - 1; ++i) {
        for (int j = i + 1; j < edit_count; ++j) {
            bool swap = false;
            int c = strcmp(edits[i].path, edits[j].path);
            if (c < 0) swap = true;
            else if (c == 0) {
                if (edits[i].start_line < edits[j].start_line) swap = true;
                else if (edits[i].start_line == edits[j].start_line && edits[i].start_col < edits[j].start_col) swap = true;
            }
            if (swap) {
                LspTextEdit tmp = edits[i];
                edits[i] = edits[j];
                edits[j] = tmp;
            }
        }
    }

    for (int i = 0; i < edit_count; ++i) {
        LspTextEdit *te = &edits[i];
        Editor *ed_target = NULL;
        for (int t = 0; t < app->tab_count; ++t) {
            if (strcmp(app->tabs[t].path, te->path) == 0) {
                ed_target = &app->tabs[t];
                break;
            }
        }
        if (!ed_target) {
            if (tab_add(app, te->path, NULL)) {
                ed_target = &app->tabs[app->tab_count - 1];
            }
        }
        if (ed_target) {
            if (editor_apply_range_edit(ed_target, te->start_line, te->start_col, te->end_line, te->end_col, te->new_text)) {
                ed_target->dirty = true;
            }
        }
    }

    for (int t = 0; t < app->tab_count; ++t) {
        Editor *ed = &app->tabs[t];
        if (ed->path[0]) {
            editor_flush_lsp_sync(ed);
        }
    }
    update_window_title(app);
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
        } else if (id == lsp->sig_help_req_id) {
            handle_signature_help_response(lsp, json, app);
        } else if (id == lsp->reference_req_id) {
            handle_references_response(lsp, json, app);
        } else if (id == lsp->rename_req_id) {
            handle_rename_response(lsp, json, app);
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

    /* 3. Signature Help Card */
    if (app->sig_help_open && lsp->sig_help.active && lsp->sig_help.label[0]) {
        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;

        float full_lbl_w = font_measure(&app->font, lsp->sig_help.label);
        float w = full_lbl_w + 32.0f;
        if (w < 280.0f) w = 280.0f;
        if (w > screen_w - 24.0f) w = screen_w - 24.0f;

        bool has_doc = lsp->sig_help.doc[0] != '\0';
        float line_h = app->font.line_height;
        float h = line_h * (has_doc ? 2.3f : 1.3f) + 14.0f;

        float x = app->sig_help_x;
        float y = app->sig_help_y - h - app->font.ascent - 8.0f;
        if (y < 32.0f) y = app->sig_help_y + 16.0f;
        if (x + w > screen_w - 12.0f) x = screen_w - w - 12.0f;
        if (x < 12.0f) x = 12.0f;

        uint8_t bg[4] = { 24, 26, 32, 250 };
        uint8_t border[4] = { 70, 80, 100, 255 };
        vk_draw_rounded_rect(&app->draw, x, y, w, h, 6.0f, 1.0f, bg);
        vk_draw_rounded_rect(&app->draw, x, y, w, 1.0f, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x, y + h - 1.0f, w, 1.0f, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x, y, 1.0f, h, 0.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, x + w - 1.0f, y, 1.0f, h, 0.0f, 1.0f, border);

        float text_y = y + app->font.ascent + 8.0f;
        int ap = lsp->sig_help.active_param;

        if (ap >= 0 && ap < lsp->sig_help.param_count &&
            lsp->sig_help.params[ap].start_offset >= 0 &&
            lsp->sig_help.params[ap].end_offset > lsp->sig_help.params[ap].start_offset) {
            int s_off = lsp->sig_help.params[ap].start_offset;
            int e_off = lsp->sig_help.params[ap].end_offset;
            size_t total_len = strlen(lsp->sig_help.label);

            char pre[256] = {0};
            char mid[128] = {0};
            char post[256] = {0};

            if (s_off > 0) {
                size_t l = (size_t)s_off < sizeof(pre) ? (size_t)s_off : sizeof(pre) - 1;
                memcpy(pre, lsp->sig_help.label, l);
                pre[l] = '\0';
            }
            if (e_off > s_off) {
                size_t l = (size_t)(e_off - s_off) < sizeof(mid) ? (size_t)(e_off - s_off) : sizeof(mid) - 1;
                memcpy(mid, lsp->sig_help.label + s_off, l);
                mid[l] = '\0';
            }
            if ((size_t)e_off < total_len) {
                size_t l = (total_len - (size_t)e_off) < sizeof(post) ? (total_len - (size_t)e_off) : sizeof(post) - 1;
                memcpy(post, lsp->sig_help.label + e_off, l);
                post[l] = '\0';
            }

            float cur_x = x + 14.0f;
            if (pre[0]) {
                font_draw(&app->font, ctx, cur_x, text_y, pre, th->fg);
                cur_x += font_measure(&app->font, pre);
            }
            if (mid[0]) {
                uint8_t hl_c[4] = { 250, 205, 90, 255 };
                float mw = font_measure(&app->font, mid);
                uint8_t hl_bg[4] = { 250, 205, 90, 45 };
                vk_draw_rounded_rect(&app->draw, cur_x - 3.0f, text_y - app->font.ascent - 2.0f, mw + 6.0f, app->font.line_height + 4.0f, 3.0f, 1.0f, hl_bg);
                font_draw(&app->font, ctx, cur_x, text_y, mid, hl_c);
                cur_x += mw;
            }
            if (post[0]) {
                font_draw(&app->font, ctx, cur_x, text_y, post, th->fg);
            }
        } else {
            font_draw(&app->font, ctx, x + 14.0f, text_y, lsp->sig_help.label, th->fg);
        }

        if (has_doc) {
            uint8_t dim[4] = { 160, 165, 175, 220 };
            font_draw(&app->font, ctx, x + 14.0f, text_y + line_h + 2.0f, lsp->sig_help.doc, dim);
        }
    }

    /* 4. References Picker Modal */
    if (app->ref_picker_open && lsp->reference_count > 0) {
        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;

        float pw = screen_w * 0.65f;
        if (pw < 520.0f) pw = 520.0f;
        if (pw > screen_w - 40.0f) pw = screen_w - 40.0f;
        float ph = screen_h * 0.70f;
        if (ph < 360.0f) ph = 360.0f;
        if (ph > screen_h - 40.0f) ph = screen_h - 40.0f;

        float px = (screen_w - pw) * 0.5f;
        float py = (screen_h - ph) * 0.5f;

        uint8_t scrim[4] = { 10, 10, 14, 110 };
        vk_draw_rounded_rect(&app->draw, 0, 0, screen_w, screen_h, 0.0f, 1.0f, scrim);

        const uint8_t *panel_bg = th->overlay_bg;
        const uint8_t *border   = th->overlay_edge;
        const uint8_t *row_sel  = th->overlay_sel;
        const uint8_t *accent   = th->caret;

        vk_draw_rounded_rect(&app->draw, px - 1.0f, py - 1.0f, pw + 2.0f, ph + 2.0f, 10.0f, 1.0f, border);
        vk_draw_rounded_rect(&app->draw, px, py, pw, ph, 9.0f, 1.0f, panel_bg);

        char title[256];
        snprintf(title, sizeof(title), "References: '%s'  (%d matches)  [Enter = Jump, Esc = Close]",
                 lsp->reference_symbol[0] ? lsp->reference_symbol : "symbol", lsp->reference_count);
        font_draw(&app->font, ctx, px + 20.0f, py + 30.0f, title, th->fg);

        float row_h = app->font.line_height + 8.0f;
        float list_top = py + 52.0f;
        int visible = (int)((ph - 80.0f) / row_h);
        if (visible < 1) visible = 1;

        if (app->ref_picker_sel < app->ref_picker_scroll)
            app->ref_picker_scroll = app->ref_picker_sel;
        if (app->ref_picker_sel >= app->ref_picker_scroll + visible)
            app->ref_picker_scroll = app->ref_picker_sel - visible + 1;
        if (app->ref_picker_scroll < 0) app->ref_picker_scroll = 0;

        for (int r = 0; r < visible; ++r) {
            int idx = app->ref_picker_scroll + r;
            if (idx >= lsp->reference_count) break;

            const LspReferenceItem *ref = &lsp->references[idx];
            float ry = list_top + (float)r * row_h;

            if (idx == app->ref_picker_sel) {
                vk_draw_rounded_rect(&app->draw, px + 10.0f, ry, pw - 20.0f, row_h, 5.0f, 1.0f, row_sel);
                vk_draw_rounded_rect(&app->draw, px + 12.0f, ry + 4.0f, 3.0f, row_h - 8.0f, 1.5f, 1.0f, accent);
            }

            char loc[160];
            snprintf(loc, sizeof(loc), "%s:%d", ref->filename, ref->line);
            float baseline = ry + (row_h + app->font.ascent + app->font.descent) * 0.5f;

            uint8_t loc_c[4] = { 100, 175, 255, 255 };
            font_draw(&app->font, ctx, px + 24.0f, baseline, loc, loc_c);

            float loc_w = font_measure(&app->font, loc);
            float snippet_x = px + 24.0f + loc_w + 16.0f;
            if (snippet_x < px + 180.0f) snippet_x = px + 180.0f;

            float max_snip_w = (px + pw - 24.0f) - snippet_x;
            if (ref->preview[0] && max_snip_w > 50.0f) {
                char draw_snip[160];
                truncate_text_to_width(&app->font, ref->preview, max_snip_w, draw_snip, sizeof(draw_snip));
                font_draw(&app->font, ctx, snippet_x, baseline, draw_snip, th->fg);
            }
        }

        char status[128];
        snprintf(status, sizeof(status), "%d / %d references", app->ref_picker_sel + 1, lsp->reference_count);
        font_draw(&app->font, ctx, px + 20.0f, py + ph - 16.0f, status, th->gutter_fg);
    }

    /* 5. Rename Symbol Inline Dialog */
    if (app->rename_open) {
        float screen_w = (float)ctx->swapchain_extent.width;
        float screen_h = (float)ctx->swapchain_extent.height;

        float pw = 420.0f;
        float ph = 88.0f;
        float px = (screen_w - pw) * 0.5f;
        float py = screen_h * 0.28f;

        uint8_t scrim[4] = { 10, 10, 14, 90 };
        vk_draw_rounded_rect(&app->draw, 0, 0, screen_w, screen_h, 0.0f, 1.0f, scrim);

        const uint8_t *panel_bg = th->overlay_bg;
        const uint8_t *accent   = th->caret;

        vk_draw_rounded_rect(&app->draw, px - 1.0f, py - 1.0f, pw + 2.0f, ph + 2.0f, 10.0f, 1.0f, accent);
        vk_draw_rounded_rect(&app->draw, px, py, pw, ph, 9.0f, 1.0f, panel_bg);

        char title[128];
        snprintf(title, sizeof(title), "Rename Symbol: '%s'", app->rename_old_name);
        font_draw(&app->font, ctx, px + 16.0f, py + 24.0f, title, th->fg);

        float ib_x = px + 16.0f;
        float ib_y = py + 34.0f;
        float ib_w = pw - 32.0f;
        float ib_h = 28.0f;

        uint8_t input_bg[4] = { 18, 20, 24, 255 };
        uint8_t input_edge[4] = { 80, 140, 230, 255 };
        vk_draw_rounded_rect(&app->draw, ib_x, ib_y, ib_w, ib_h, 4.0f, 1.0f, input_bg);
        vk_draw_rounded_rect(&app->draw, ib_x, ib_y, ib_w, 1.0f, 0.0f, 1.0f, input_edge);
        vk_draw_rounded_rect(&app->draw, ib_x, ib_y + ib_h - 1.0f, ib_w, 1.0f, 0.0f, 1.0f, input_edge);
        vk_draw_rounded_rect(&app->draw, ib_x, ib_y, 1.0f, ib_h, 0.0f, 1.0f, input_edge);
        vk_draw_rounded_rect(&app->draw, ib_x + ib_w - 1.0f, ib_y, 1.0f, ib_h, 0.0f, 1.0f, input_edge);

        float text_base = ib_y + (ib_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, ib_x + 8.0f, text_base, app->rename_new_name, th->fg);

        /* Caret indicator */
        char pre_caret[128] = {0};
        if (app->rename_cursor > 0 && (size_t)app->rename_cursor < sizeof(pre_caret)) {
            memcpy(pre_caret, app->rename_new_name, (size_t)app->rename_cursor);
            pre_caret[app->rename_cursor] = '\0';
        }
        float caret_x = ib_x + 8.0f + font_measure(&app->font, pre_caret);
        vk_draw_rounded_rect(&app->draw, caret_x, ib_y + 4.0f, 2.0f, ib_h - 8.0f, 1.0f, 1.0f, accent);

        font_draw(&app->font, ctx, px + 16.0f, py + ph - 10.0f, "Enter = Apply Rename, Esc = Cancel", th->gutter_fg);
    }
}

/* ------------------------------------------------------------------ */
/* Overlay Event Handling                                             */
/* ------------------------------------------------------------------ */

bool lsp_handle_event(App *app, const SDL_Event *ev)
{
    if (!app || !ev) return false;
    LspClient *lsp = &app->lsp;

    /* 1. Rename Symbol Prompt Input */
    if (app->rename_open) {
        if (ev->type == SDL_EVENT_KEY_DOWN) {
            SDL_Keycode k = ev->key.key;
            if (k == SDLK_ESCAPE) {
                app->rename_open = false;
                return true;
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                if (app->rename_new_name[0] && strcmp(app->rename_old_name, app->rename_new_name) != 0) {
                    Editor *ed = app_ed(app);
                    if (ed && ed->path[0]) {
                        lsp_request_rename(lsp, ed->path, editor_current_line(ed), editor_current_col(ed),
                                           app->rename_old_name, app->rename_new_name);
                    }
                }
                app->rename_open = false;
                return true;
            } else if (k == SDLK_BACKSPACE) {
                if (app->rename_cursor > 0) {
                    memmove(&app->rename_new_name[app->rename_cursor - 1],
                            &app->rename_new_name[app->rename_cursor],
                            (size_t)(app->rename_len - app->rename_cursor + 1));
                    app->rename_cursor--;
                    app->rename_len--;
                }
                return true;
            } else if (k == SDLK_DELETE) {
                if (app->rename_cursor < app->rename_len) {
                    memmove(&app->rename_new_name[app->rename_cursor],
                            &app->rename_new_name[app->rename_cursor + 1],
                            (size_t)(app->rename_len - app->rename_cursor));
                    app->rename_len--;
                }
                return true;
            } else if (k == SDLK_LEFT) {
                if (app->rename_cursor > 0) app->rename_cursor--;
                return true;
            } else if (k == SDLK_RIGHT) {
                if (app->rename_cursor < app->rename_len) app->rename_cursor++;
                return true;
            } else if (k == SDLK_HOME) {
                app->rename_cursor = 0;
                return true;
            } else if (k == SDLK_END) {
                app->rename_cursor = app->rename_len;
                return true;
            }
            return true;
        } else if (ev->type == SDL_EVENT_TEXT_INPUT) {
            const char *t = ev->text.text;
            size_t tlen = strlen(t);
            if ((size_t)app->rename_len + tlen < sizeof(app->rename_new_name)) {
                memmove(&app->rename_new_name[app->rename_cursor + tlen],
                        &app->rename_new_name[app->rename_cursor],
                        (size_t)(app->rename_len - app->rename_cursor + 1));
                memcpy(&app->rename_new_name[app->rename_cursor], t, tlen);
                app->rename_cursor += (int)tlen;
                app->rename_len += (int)tlen;
            }
            return true;
        } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            float screen_w = (float)app->vk.swapchain_extent.width;
            float screen_h = (float)app->vk.swapchain_extent.height;
            float pw = 420.0f;
            float ph = 88.0f;
            float px = (screen_w - pw) * 0.5f;
            float py = screen_h * 0.28f;
            float mx = (float)ev->button.x;
            float my = (float)ev->button.y;
            if (mx < px || mx >= px + pw || my < py || my >= py + ph) {
                app->rename_open = false;
            }
            return true;
        }
        return true;
    }

    /* 2. References Picker Input */
    if (app->ref_picker_open && lsp->reference_count > 0) {
        if (ev->type == SDL_EVENT_KEY_DOWN) {
            SDL_Keycode k = ev->key.key;
            if (k == SDLK_ESCAPE) {
                app->ref_picker_open = false;
                return true;
            } else if (k == SDLK_UP) {
                if (app->ref_picker_sel > 0) app->ref_picker_sel--;
                return true;
            } else if (k == SDLK_DOWN) {
                if (app->ref_picker_sel + 1 < lsp->reference_count) app->ref_picker_sel++;
                return true;
            } else if (k == SDLK_PAGEUP) {
                app->ref_picker_sel -= 8;
                if (app->ref_picker_sel < 0) app->ref_picker_sel = 0;
                return true;
            } else if (k == SDLK_PAGEDOWN) {
                app->ref_picker_sel += 8;
                if (app->ref_picker_sel >= lsp->reference_count)
                    app->ref_picker_sel = lsp->reference_count - 1;
                return true;
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                if (app->ref_picker_sel >= 0 && app->ref_picker_sel < lsp->reference_count) {
                    const LspReferenceItem *ref = &lsp->references[app->ref_picker_sel];
                    int target_tab = -1;
                    for (int t = 0; t < app->tab_count; ++t) {
                        if (strcmp(app->tabs[t].path, ref->path) == 0) {
                            target_tab = t;
                            break;
                        }
                    }
                    if (target_tab >= 0) {
                        app->tab_active = target_tab;
                    } else {
                        tab_add(app, ref->path, NULL);
                    }
                    Editor *ed = app_ed(app);
                    if (ed) {
                        editor_goto_line(ed, ref->line);
                        size_t p = editor_pos_from_line_col(ed, ref->line, ref->col);
                        ed->caret = p;
                        ed->sel_anchor = p;
                    }
                    update_window_title(app);
                }
                app->ref_picker_open = false;
                return true;
            }
        } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
            app->ref_picker_sel -= (int)ev->wheel.y * 2;
            if (app->ref_picker_sel < 0) app->ref_picker_sel = 0;
            if (app->ref_picker_sel >= lsp->reference_count)
                app->ref_picker_sel = lsp->reference_count - 1;
            return true;
        } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_LEFT) {
            float screen_w = (float)app->vk.swapchain_extent.width;
            float screen_h = (float)app->vk.swapchain_extent.height;
            float pw = screen_w * 0.65f;
            if (pw < 520.0f) pw = 520.0f;
            if (pw > screen_w - 40.0f) pw = screen_w - 40.0f;
            float ph = screen_h * 0.70f;
            if (ph < 360.0f) ph = 360.0f;
            if (ph > screen_h - 40.0f) ph = screen_h - 40.0f;
            float px = (screen_w - pw) * 0.5f;
            float py = (screen_h - ph) * 0.5f;
            float mx = (float)ev->button.x;
            float my = (float)ev->button.y;

            if (mx < px || mx >= px + pw || my < py || my >= py + ph) {
                app->ref_picker_open = false;
                return true;
            }

            float row_h = app->font.line_height + 8.0f;
            float list_top = py + 52.0f;
            if (my >= list_top) {
                int clicked_idx = app->ref_picker_scroll + (int)((my - list_top) / row_h);
                if (clicked_idx >= 0 && clicked_idx < lsp->reference_count) {
                    app->ref_picker_sel = clicked_idx;
                    const LspReferenceItem *ref = &lsp->references[clicked_idx];
                    int target_tab = -1;
                    for (int t = 0; t < app->tab_count; ++t) {
                        if (strcmp(app->tabs[t].path, ref->path) == 0) {
                            target_tab = t;
                            break;
                        }
                    }
                    if (target_tab >= 0) {
                        app->tab_active = target_tab;
                    } else {
                        tab_add(app, ref->path, NULL);
                    }
                    Editor *ed = app_ed(app);
                    if (ed) {
                        editor_goto_line(ed, ref->line);
                        size_t p = editor_pos_from_line_col(ed, ref->line, ref->col);
                        ed->caret = p;
                        ed->sel_anchor = p;
                    }
                    update_window_title(app);
                    app->ref_picker_open = false;
                }
            }
            return true;
        }
        return true;
    }

    /* 3. Signature Help Dismiss */
    if (app->sig_help_open) {
        if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE) {
            app->sig_help_open = false;
            lsp->sig_help.active = false;
            return true;
        }
    }

    /* 4. Hover Dismiss */
    if (app->hover_open) {
        if (ev->type == SDL_EVENT_KEY_DOWN ||
            ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            app->hover_open = false;
            lsp->hover.active = false;
        }
    }

    /* 5. Autocompletion Popup */
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
