/**
 * editor.h – Code editor view
 *
 * Gap buffer + caret + selection + vertical scroll.
 */

#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "highlight.h"
#include "vk_context.h"
#include "theme.h"

struct VkDraw;
struct LspClient;

typedef enum UndoKind {
    UNDO_INSERT = 1,
    UNDO_DELETE = 2,
} UndoKind;

typedef struct UndoRec {
    UndoKind kind;
    size_t   pos;
    char    *text;
    size_t   len;
    unsigned group;   /* records sharing a group undo/redo together */
} UndoRec;

/* Growable stack of undo/redo records. Grows on demand, never discards. */
typedef struct UndoStack {
    UndoRec *items;
    int      len;
    int      cap;
} UndoStack;

typedef struct Editor {
    char  *buf;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;     /* exclusive */

    size_t caret;       /* logical index */
    size_t sel_anchor;  /* selection other end; equal to caret => no selection */
    bool   selecting;   /* mouse drag in progress */

    float scroll_y;
    float scroll_x;
    float view_w;
    float view_h;
    float view_x;
    float view_y;

    float preferred_col; /* for up/down navigation */

    float pad_x;
    float pad_y;
    float gutter_w;
    bool  show_line_numbers;
    float line_height;

    Font *font;

    uint8_t text_color[4];
    uint8_t caret_color[4];
    uint8_t sel_color[4];
    uint8_t bg[4];

    UndoStack undo;
    UndoStack redo;
    bool    undo_grouping; /* coalesce consecutive inserts */
    unsigned undo_group_seq;
    unsigned undo_group_cur;
    int      undo_group_depth;

    char path[1024];   /* current file, empty if untitled */
    bool dirty;

    Highlighter hl;
    bool hl_dirty;

    /* Visual improvements */
    bool  soft_wrap;
    bool  sb_dragging;
    float sb_drag_start_y;
    float sb_drag_start_scroll;

    bool  sb_x_dragging;
    float sb_x_drag_start_x;
    float sb_x_drag_start_scroll;

    char  find_query[256];
    bool  find_case;
    bool  find_active;

    struct LspClient *lsp;
    bool              lsp_dirty;
    uint64_t          lsp_dirty_ms;
} Editor;

bool editor_create(Editor *ed, Font *font, const char *initial);
void editor_destroy(Editor *ed);

size_t editor_length(const Editor *ed);
void   editor_get_text(const Editor *ed, char *out, size_t out_cap);

bool editor_has_selection(const Editor *ed);
void editor_get_selection(const Editor *ed, size_t *start, size_t *end);
void editor_clear_selection(Editor *ed);
void editor_delete_selection(Editor *ed);

void editor_insert(Editor *ed, const char *utf8, size_t nbytes);
void editor_backspace(Editor *ed);
void editor_delete(Editor *ed);           /* forward delete (Delete key) */
void editor_copy(Editor *ed);
void editor_cut(Editor *ed);
void editor_paste(Editor *ed);
void editor_undo(Editor *ed);
void editor_redo(Editor *ed);

bool editor_load_file(Editor *ed, const char *path);
bool editor_save_file(Editor *ed, const char *path); /* path NULL => ed->path */
void editor_set_text(Editor *ed, const char *text);

/** Find next/prev occurrence of query. Selects match and scrolls into view.
 *  Returns true if found. case_sensitive = false folds A-Z. */
bool editor_find_next(Editor *ed, const char *query, bool case_sensitive);
bool editor_find_prev(Editor *ed, const char *query, bool case_sensitive);
/** Replace current selection if it matches query, then find next. */
bool editor_replace_next(Editor *ed, const char *query, const char *replacement, bool case_sensitive);
/** Replace all occurrences. Returns count. */
int  editor_replace_all(Editor *ed, const char *query, const char *replacement, bool case_sensitive);

/** Count matches of query in editor. Writes 1-based index of active match to out_current if matched. */
int  editor_count_matches(const Editor *ed, const char *query, bool case_sensitive, int *out_current);
void editor_set_find_query(Editor *ed, const char *query, bool case_sensitive);

/** Soft wrap setting */
void editor_set_soft_wrap(Editor *ed, bool enable);
bool editor_get_soft_wrap(const Editor *ed);

/** Matching bracket lookup */
bool editor_find_matching_bracket(const Editor *ed, size_t pos, size_t *out_match);

/** 1-based line number. Clamps to valid range. */
void editor_goto_line(Editor *ed, int line_1based);
int  editor_line_count(const Editor *ed);
int  editor_total_rows(const Editor *ed);
float editor_max_content_width(const Editor *ed);
int  editor_current_line(const Editor *ed);
int  editor_current_col(const Editor *ed);

void editor_set_draw_context(struct VkDraw *d);
void editor_set_lsp(Editor *ed, struct LspClient *lsp);
void editor_flush_lsp_sync(Editor *ed);
void editor_caret_pixel_pos(const Editor *ed, float *out_x, float *out_y);
bool editor_line_col_at_point(const Editor *ed, float px, float py, int *out_line, int *out_col);
size_t editor_current_word_prefix(const Editor *ed, char *out_prefix, size_t out_cap);
size_t editor_current_word(const Editor *ed, char *out_word, size_t out_cap);
size_t editor_pos_from_line_col(const Editor *ed, int line_1based, int col_1based);
bool   editor_apply_range_edit(Editor *ed, int start_line, int start_col, int end_line, int end_col, const char *new_text);
void editor_insert_completion(Editor *ed, const char *completion);
void editor_apply_theme(Editor *ed, const Theme *th);
void editor_handle_event(Editor *ed, const void *sdl_event);
void editor_draw(Editor *ed, VkContext *ctx, float x, float y, float w, float h);

#endif /* EDITOR_H */
