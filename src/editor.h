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

struct VkDraw;

#define EDITOR_UNDO_MAX 128

typedef enum UndoKind {
    UNDO_INSERT = 1,
    UNDO_DELETE = 2,
} UndoKind;

typedef struct UndoRec {
    UndoKind kind;
    size_t   pos;
    char    *text;
    size_t   len;
} UndoRec;

typedef struct Editor {
    char  *buf;
    size_t capacity;
    size_t gap_start;
    size_t gap_end;     /* exclusive */

    size_t caret;       /* logical index */
    size_t sel_anchor;  /* selection other end; equal to caret => no selection */
    bool   selecting;   /* mouse drag in progress */

    float scroll_y;
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

    UndoRec undo_stack[EDITOR_UNDO_MAX];
    int     undo_len;
    UndoRec redo_stack[EDITOR_UNDO_MAX];
    int     redo_len;
    bool    undo_grouping; /* coalesce consecutive inserts */

    char path[1024];   /* current file, empty if untitled */
    bool dirty;

    Highlighter hl;
    bool hl_dirty;
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

/** 1-based line number. Clamps to valid range. */
void editor_goto_line(Editor *ed, int line_1based);
int  editor_line_count(const Editor *ed);
int  editor_current_line(const Editor *ed);
int  editor_current_col(const Editor *ed);

void editor_set_draw_context(struct VkDraw *d);
void editor_handle_event(Editor *ed, const void *sdl_event);
void editor_draw(Editor *ed, VkContext *ctx, float x, float y, float w, float h);

#endif /* EDITOR_H */
