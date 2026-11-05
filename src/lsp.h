/**
 * lsp.h – Language Server Protocol (LSP) client for cgui
 *
 * Implements JSON-RPC 2.0 communication over stdio with language servers
 * (e.g. clangd, rust-analyzer, pyright, gopls, lua-language-server).
 *
 * Supports:
 *   - Lifecycle: initialize, initialized, shutdown, exit
 *   - Document sync: didOpen, didChange, didSave, didClose
 *   - Diagnostics: textDocument/publishDiagnostics (gutter indicators & squiggles)
 *   - Autocompletion: textDocument/completion (floating popup menu)
 *   - Go to Definition: textDocument/definition (jump to symbol declaration)
 *   - Hover: textDocument/hover (floating type signature & docs)
 *   - Formatting: textDocument/formatting
 */

#ifndef CGUI_LSP_H
#define CGUI_LSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define LSP_MAX_DIAGNOSTICS 128
#define LSP_MAX_COMPLETIONS 64
#define LSP_MAX_PATH 1024
#define LSP_BUFFER_SIZE 65536

typedef enum LspState {
    LSP_STATE_IDLE = 0,
    LSP_STATE_STARTING,
    LSP_STATE_INITIALIZED,
    LSP_STATE_RUNNING,
    LSP_STATE_STOPPED,
    LSP_STATE_ERROR
} LspState;

typedef enum LspSeverity {
    LSP_SEV_ERROR = 1,
    LSP_SEV_WARNING = 2,
    LSP_SEV_INFO = 3,
    LSP_SEV_HINT = 4
} LspSeverity;

typedef struct LspDiagnostic {
    int         start_line;   /* 1-based */
    int         start_col;    /* 1-based */
    int         end_line;     /* 1-based */
    int         end_col;      /* 1-based */
    LspSeverity severity;
    char        message[256];
    char        source[32];
} LspDiagnostic;

typedef enum LspCompletionKind {
    LSP_COMP_TEXT = 1,
    LSP_COMP_METHOD = 2,
    LSP_COMP_FUNCTION = 3,
    LSP_COMP_CONSTRUCTOR = 4,
    LSP_COMP_FIELD = 5,
    LSP_COMP_VARIABLE = 6,
    LSP_COMP_CLASS = 7,
    LSP_COMP_INTERFACE = 8,
    LSP_COMP_MODULE = 9,
    LSP_COMP_PROPERTY = 10,
    LSP_COMP_UNIT = 11,
    LSP_COMP_VALUE = 12,
    LSP_COMP_ENUM = 13,
    LSP_COMP_KEYWORD = 14,
    LSP_COMP_SNIPPET = 15,
    LSP_COMP_COLOR = 16,
    LSP_COMP_FILE = 17,
    LSP_COMP_REFERENCE = 18,
    LSP_COMP_FOLDER = 19,
    LSP_COMP_CONSTANT = 21,
    LSP_COMP_STRUCT = 22,
    LSP_COMP_TYPE_PARAMETER = 25
} LspCompletionKind;

typedef struct LspCompletionItem {
    char              label[96];
    char              detail[128];
    char              documentation[256];
    char              insert_text[96];
    LspCompletionKind kind;
} LspCompletionItem;

typedef struct LspHover {
    bool active;
    char content[512];
    int  line;
    int  col;
    float screen_x;
    float screen_y;
} LspHover;

typedef struct LspLocation {
    bool valid;
    char path[LSP_MAX_PATH];
    int  line; /* 1-based */
    int  col;  /* 1-based */
} LspLocation;

typedef struct LspClient {
    LspState state;
    pid_t    pid;
    int      in_pipe;   /* write end of child stdin */
    int      out_pipe;  /* read end of child stdout */

    int      next_id;
    int      doc_version;
    char     root_path[LSP_MAX_PATH];
    char     server_cmd[256];

    /* Incoming message assembly buffer */
    char    *rx_buf;
    size_t   rx_len;
    size_t   rx_cap;

    /* Current active file diagnostics */
    char          diag_path[LSP_MAX_PATH];
    LspDiagnostic diagnostics[LSP_MAX_DIAGNOSTICS];
    int           diagnostic_count;

    /* Completion results */
    LspCompletionItem completions[LSP_MAX_COMPLETIONS];
    int               completion_count;
    int               completion_req_id;
    bool              completion_pending;
    int               completion_line;
    int               completion_col;

    /* Hover result */
    LspHover hover;
    int      hover_req_id;

    /* Definition result */
    LspLocation definition;
    int         definition_req_id;

    /* Formatting */
    int  format_req_id;
    bool format_pending;
} LspClient;

/* Forward declaration */
struct App;
struct Editor;
struct VkContext;
union SDL_Event;

/** Lifecycle */
bool lsp_init(LspClient *lsp, const char *server_cmd, const char *root_path);
void lsp_shutdown(LspClient *lsp);
void lsp_destroy(LspClient *lsp);

/** Main loop tick: drains I/O and processes JSON-RPC messages */
void lsp_update(struct App *app);

/** Document synchronization */
void lsp_doc_did_open(LspClient *lsp, const char *path, const char *text, size_t len);
void lsp_doc_did_change(LspClient *lsp, const char *path, const char *text, size_t len);
void lsp_doc_did_save(LspClient *lsp, const char *path);
void lsp_doc_did_close(LspClient *lsp, const char *path);

/** Requests */
void lsp_request_completion(LspClient *lsp, const char *path, int line, int col);
void lsp_request_hover(LspClient *lsp, const char *path, int line, int col);
void lsp_request_definition(LspClient *lsp, const char *path, int line, int col);
void lsp_request_format(LspClient *lsp, const char *path);

/** UI Queries and Drawing */
int  lsp_diagnostics_for_line(const LspClient *lsp, int line, const LspDiagnostic **out_first);
const char *lsp_completion_kind_name(LspCompletionKind kind);
const char *lsp_completion_kind_badge(LspCompletionKind kind);

/** Overlay rendering and input */
void lsp_draw_ui(struct App *app, struct VkContext *ctx);
bool lsp_handle_event(struct App *app, const union SDL_Event *ev);

#endif /* CGUI_LSP_H */
