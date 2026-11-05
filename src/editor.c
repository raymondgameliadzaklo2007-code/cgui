/**
 * editor.c – Gap buffer, selection, scroll, draw
 */

#include "editor.h"
#include "vk_draw.h"
#include "highlight.h"
#include "theme.h"
#include "lsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <SDL3/SDL.h>

static VkDraw *g_editor_draw = NULL;

void editor_set_draw_context(VkDraw *d)
{
    g_editor_draw = d;
}

void editor_set_lsp(Editor *ed, struct LspClient *lsp)
{
    if (ed) ed->lsp = lsp;
}

/* ------------------------------------------------------------------ */
/* Gap buffer                                                          */
/* ------------------------------------------------------------------ */

static size_t logical_len(const Editor *ed)
{
    return ed->capacity - (ed->gap_end - ed->gap_start);
}

static char char_at(const Editor *ed, size_t i)
{
    if (i >= logical_len(ed)) return '\0';
    if (i < ed->gap_start) return ed->buf[i];
    return ed->buf[i + (ed->gap_end - ed->gap_start)];
}

static void ensure_gap(Editor *ed, size_t need)
{
    size_t gap = ed->gap_end - ed->gap_start;
    if (gap >= need) return;

    size_t len = logical_len(ed);
    size_t new_cap = ed->capacity < 64 ? 64 : ed->capacity;
    while (new_cap - len < need) new_cap *= 2;

    char *nbuf = malloc(new_cap);
    if (!nbuf) return;

    memcpy(nbuf, ed->buf, ed->gap_start);
    size_t after = ed->capacity - ed->gap_end;
    memcpy(nbuf + new_cap - after, ed->buf + ed->gap_end, after);

    free(ed->buf);
    ed->buf = nbuf;
    ed->gap_end = new_cap - after;
    ed->capacity = new_cap;
}

static void move_gap(Editor *ed, size_t pos)
{
    size_t len = logical_len(ed);
    if (pos > len) pos = len;

    if (pos < ed->gap_start) {
        size_t n = ed->gap_start - pos;
        memmove(ed->buf + ed->gap_end - n, ed->buf + pos, n);
        ed->gap_start = pos;
        ed->gap_end -= n;
    } else if (pos > ed->gap_start) {
        size_t n = pos - ed->gap_start;
        memmove(ed->buf + ed->gap_start, ed->buf + ed->gap_end, n);
        ed->gap_start += n;
        ed->gap_end += n;
    }
}


/* ------------------------------------------------------------------ */
/* Undo / redo                                                         */
/* ------------------------------------------------------------------ */

static void undo_rec_free(UndoRec *r)
{
    free(r->text);
    r->text = NULL;
    r->len = 0;
}

/** Drop every record but keep the allocation for reuse. */
static void undo_stack_clear(UndoStack *s)
{
    for (int i = 0; i < s->len; ++i) undo_rec_free(&s->items[i]);
    s->len = 0;
}

static void undo_stack_free(UndoStack *s)
{
    undo_stack_clear(s);
    free(s->items);
    s->items = NULL;
    s->cap = 0;
}

/** Make room for one more record, doubling the capacity when full. */
static bool undo_stack_reserve(UndoStack *s)
{
    if (s->len < s->cap) return true;
    if (s->cap > INT_MAX / 2) return false;
    int cap = s->cap ? s->cap * 2 : 64;
    UndoRec *items = realloc(s->items, (size_t)cap * sizeof(*items));
    if (!items) return false;
    s->items = items;
    s->cap = cap;
    return true;
}

static void undo_push(Editor *ed, UndoKind kind, size_t pos, const char *text, size_t len)
{
    /* Clear redo on new edit */
    undo_stack_clear(&ed->redo);

    unsigned group = ed->undo_group_depth > 0 ? ed->undo_group_cur : ++ed->undo_group_seq;

    /* Coalesce sequential inserts at growing caret (typing). Never merge
     * into or out of a multi-record group (auto-indent, move line, …). */
    if (kind == UNDO_INSERT && ed->undo.len > 0 && ed->undo_group_depth == 0) {
        UndoRec *top = &ed->undo.items[ed->undo.len - 1];
        bool top_shared = ed->undo.len > 1 &&
                          ed->undo.items[ed->undo.len - 2].group == top->group;
        if (top->kind == UNDO_INSERT && top->pos + top->len == pos && !top_shared &&
            !(len == 1 && text[0] == '\n')) {
            char *n = realloc(top->text, top->len + len + 1);
            if (n) {
                memcpy(n + top->len, text, len);
                top->len += len;
                n[top->len] = '\0';
                top->text = n;
                return;
            }
        }
    }

    if (!undo_stack_reserve(&ed->undo)) {
        fprintf(stderr, "warning: out of memory, edit not recorded for undo\n");
        return;
    }

    UndoRec *r = &ed->undo.items[ed->undo.len++];
    r->kind = kind;
    r->group = group;
    r->pos = pos;
    r->len = len;
    r->text = malloc(len + 1);
    if (r->text) {
        memcpy(r->text, text, len);
        r->text[len] = '\0';
    } else {
        r->len = 0;
        ed->undo.len--;
    }
}

static void undo_begin(Editor *ed)
{
    if (ed->undo_group_depth++ == 0) ed->undo_group_cur = ++ed->undo_group_seq;
}

static void undo_end(Editor *ed)
{
    if (ed->undo_group_depth > 0) ed->undo_group_depth--;
}

/* ------------------------------------------------------------------ */
/* Raw buffer mutations. Every change to the text goes through these   */
/* two so the highlighter always hears about it (ts_tree_edit).        */
/* ------------------------------------------------------------------ */

/** Row / byte column of a logical offset. */
static HlPoint point_at(const Editor *ed, size_t pos)
{
    HlPoint p = { 0, 0 };
    size_t len = logical_len(ed);
    if (pos > len) pos = len;
    size_t col = 0;
    for (size_t i = 0; i < pos; ++i) {
        if (char_at(ed, i) == '\n') { p.row++; col = 0; }
        else col++;
    }
    p.column = (uint32_t)col;
    return p;
}

static HlPoint point_advance(HlPoint p, const char *text, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\n') { p.row++; p.column = 0; }
        else p.column++;
    }
    return p;
}

static bool buf_insert(Editor *ed, size_t pos, const char *text, size_t len)
{
    if (len == 0) return true;
    size_t total = logical_len(ed);
    if (pos > total) pos = total;
    move_gap(ed, pos);
    ensure_gap(ed, len);
    if (ed->gap_end - ed->gap_start < len) return false; /* out of memory */

    HlPoint sp = point_at(ed, pos);
    highlight_edit(&ed->hl, pos, pos, pos + len, sp, sp, point_advance(sp, text, len));

    memcpy(ed->buf + ed->gap_start, text, len);
    ed->gap_start += len;
    ed->dirty = true;
    ed->hl_dirty = true;
    ed->lsp_dirty = true;
    ed->lsp_dirty_ms = SDL_GetTicks();
    return true;
}

static void buf_delete(Editor *ed, size_t pos, size_t n)
{
    size_t total = logical_len(ed);
    if (pos > total) pos = total;
    if (pos + n > total) n = total - pos;
    if (n == 0) return;

    HlPoint sp = point_at(ed, pos);
    HlPoint ep = sp;
    for (size_t i = 0; i < n; ++i) {
        if (char_at(ed, pos + i) == '\n') { ep.row++; ep.column = 0; }
        else ep.column++;
    }
    highlight_edit(&ed->hl, pos, pos + n, pos, sp, ep, sp);

    move_gap(ed, pos);
    ed->gap_end += n;
    ed->dirty = true;
    ed->hl_dirty = true;
    ed->lsp_dirty = true;
    ed->lsp_dirty_ms = SDL_GetTicks();
}

static char *copy_range(const Editor *ed, size_t a, size_t n)
{
    char *tmp = malloc(n + 1);
    if (!tmp) return NULL;
    for (size_t i = 0; i < n; ++i) tmp[i] = char_at(ed, a + i);
    tmp[n] = 0;
    return tmp;
}

/** Delete [a, a+n) with an undo record. */
static void delete_recorded(Editor *ed, size_t a, size_t n)
{
    if (n == 0) return;
    char *tmp = copy_range(ed, a, n);
    if (tmp) {
        undo_push(ed, UNDO_DELETE, a, tmp, n);
        free(tmp);
    }
    buf_delete(ed, a, n);
}

/** Insert with an undo record (no selection handling, caret untouched). */
static void insert_recorded(Editor *ed, size_t pos, const char *text, size_t len)
{
    if (len == 0) return;
    undo_push(ed, UNDO_INSERT, pos, text, len);
    buf_insert(ed, pos, text, len);
}

static void apply_delete_range(Editor *ed, size_t pos, size_t n)
{
    buf_delete(ed, pos, n);
    ed->caret = pos;
    ed->sel_anchor = pos;
}

static void apply_insert_at(Editor *ed, size_t pos, const char *text, size_t len)
{
    buf_insert(ed, pos, text, len);
    ed->caret = pos + len;
    ed->sel_anchor = ed->caret;
}

/* ------------------------------------------------------------------ */
/* Lines / geometry                                                    */
/* ------------------------------------------------------------------ */

static size_t line_start(const Editor *ed, size_t pos)
{
    size_t len = logical_len(ed);
    if (pos > len) pos = len;
    while (pos > 0) {
        if (char_at(ed, pos - 1) == '\n') break;
        pos--;
    }
    return pos;
}

static size_t next_line_start(const Editor *ed, size_t pos)
{
    size_t len = logical_len(ed);
    while (pos < len) {
        char c = char_at(ed, pos++);
        if (c == '\n') break;
    }
    return pos;
}

static size_t line_col(const Editor *ed, size_t pos)
{
    return pos - line_start(ed, pos);
}

/** Offset of the '\n' ending the line containing pos (or len). */
static size_t line_end(const Editor *ed, size_t pos)
{
    size_t len = logical_len(ed);
    while (pos < len && char_at(ed, pos) != '\n') pos++;
    return pos;
}

static size_t count_lines(const Editor *ed)
{
    size_t len = logical_len(ed);
    size_t lines = 1;
    for (size_t i = 0; i < len; ++i) {
        if (char_at(ed, i) == '\n') lines++;
    }
    return lines;
}

typedef struct WrapRow {
    size_t start;
    size_t end;
    bool   continuation;
} WrapRow;

static float char_width(const Editor *ed, char ch)
{
    if (!ed->font) return 8.0f;
    unsigned char c = (unsigned char)ch;
    if (c >= 32 && c < 128 && ed->font->glyphs[c].present)
        return ed->font->glyphs[c].advance;
    return ed->font->space_advance;
}

static float get_wrap_width(const Editor *ed)
{
    float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
    float max_w = ed->view_w - ed->pad_x * 2.0f - gutter - 24.0f;
    return max_w > 100.0f ? max_w : 100.0f;
}

static int wrap_logical_line(const Editor *ed, size_t ls, size_t le, float max_w, WrapRow *out_rows, int max_out)
{
    if (!ed->soft_wrap || max_w <= 0.0f || le <= ls) {
        if (max_out > 0) {
            out_rows[0].start = ls;
            out_rows[0].end = le;
            out_rows[0].continuation = false;
        }
        return 1;
    }

    int count = 0;
    size_t cur = ls;
    float x = 0.0f;
    size_t last_break = ls;
    size_t row_start = ls;

    while (cur < le) {
        char c = char_at(ed, cur);
        float w = char_width(ed, c);

        if (x + w > max_w && cur > row_start) {
            size_t break_at = (last_break > row_start) ? last_break : cur;
            if (count < max_out) {
                out_rows[count].start = row_start;
                out_rows[count].end = break_at;
                out_rows[count].continuation = (count > 0);
            }
            count++;
            row_start = break_at;
            while (row_start < le && char_at(ed, row_start) == ' ') row_start++;
            cur = row_start;
            x = 0.0f;
            last_break = row_start;
            continue;
        }

        if (c == ' ' || c == '\t') {
            last_break = cur + 1;
        }

        x += w;
        cur++;
    }

    if (row_start <= le) {
        if (count < max_out) {
            out_rows[count].start = row_start;
            out_rows[count].end = le;
            out_rows[count].continuation = (count > 0);
        }
        count++;
    }

    return count > 0 ? count : 1;
}

int editor_total_rows(const Editor *ed)
{
    if (!ed) return 0;
    if (!ed->soft_wrap) return (int)count_lines(ed);

    float max_w = get_wrap_width(ed);
    size_t len = logical_len(ed);
    size_t ls = 0;
    int total = 0;

    while (ls <= len) {
        size_t le = line_end(ed, ls);
        total += wrap_logical_line(ed, ls, le, max_w, NULL, 0);
        if (le >= len) break;
        ls = le + 1;
    }
    return total > 0 ? total : 1;
}

float editor_max_content_width(const Editor *ed)
{
    if (!ed || !ed->font) return 0.0f;
    size_t len = logical_len(ed);
    size_t ls = 0;
    float max_line_w = 0.0f;

    while (ls <= len) {
        size_t le = line_end(ed, ls);
        float line_w = 0.0f;
        for (size_t i = ls; i < le; ++i) {
            line_w += char_width(ed, char_at(ed, i));
        }
        if (line_w > max_line_w) {
            max_line_w = line_w;
        }
        if (le >= len) break;
        ls = le + 1;
    }
    return max_line_w;
}

/** Map pixel position inside the view to a logical caret index. */
static size_t index_from_point(const Editor *ed, float px, float py)
{
    if (!ed->font) return 0;

    float local_y = py - ed->view_y - ed->pad_y + ed->scroll_y;
    if (local_y < 0) local_y = 0;
    int target_row = (int)(local_y / ed->line_height);

    float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
    float local_x = px - ed->view_x - ed->pad_x - gutter + (ed->soft_wrap ? 0.0f : ed->scroll_x);
    if (local_x < 0) local_x = 0;

    size_t len = logical_len(ed);

    if (!ed->soft_wrap) {
        size_t ls = 0;
        for (int i = 0; i < target_row; ++i) {
            size_t nls = next_line_start(ed, ls);
            if (nls == ls) break;
            ls = nls;
        }
        size_t i = ls;
        float x = 0.0f;
        while (i < len) {
            char c = char_at(ed, i);
            if (c == '\n') break;
            float w = char_width(ed, c);
            if (x + w * 0.5f >= local_x) break;
            x += w;
            i++;
        }
        return i;
    }

    /* Soft wrap enabled */
    float max_w = get_wrap_width(ed);
    size_t ls = 0;
    int cur_row = 0;

    while (ls <= len) {
        size_t le = line_end(ed, ls);
        WrapRow rows[64];
        int nr = wrap_logical_line(ed, ls, le, max_w, rows, 64);
        if (cur_row + nr > target_row || le >= len) {
            int r_idx = target_row - cur_row;
            if (r_idx < 0) r_idx = 0;
            if (r_idx >= nr) r_idx = nr - 1;
            size_t i = rows[r_idx].start;
            float x = 0.0f;
            while (i < rows[r_idx].end) {
                char c = char_at(ed, i);
                float w = char_width(ed, c);
                if (x + w * 0.5f >= local_x) break;
                x += w;
                i++;
            }
            return i;
        }
        cur_row += nr;
        if (le >= len) break;
        ls = le + 1;
    }
    return len;
}

static void caret_pixel_pos(const Editor *ed, size_t pos, float *out_x, float *out_y)
{
    size_t len = logical_len(ed);
    if (pos > len) pos = len;

    float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
    float text_left = ed->view_x + ed->pad_x + gutter - (ed->soft_wrap ? 0.0f : ed->scroll_x);

    if (!ed->soft_wrap) {
        size_t ls = line_start(ed, pos);
        size_t line = 0;
        size_t p = 0;
        while (p < ls) {
            if (char_at(ed, p) == '\n') line++;
            p++;
        }
        float x = text_left;
        for (size_t i = ls; i < pos; ++i) {
            x += char_width(ed, char_at(ed, i));
        }
        float y = ed->view_y + ed->pad_y + (ed->font ? ed->font->ascent : 14.0f)
                + (float)line * ed->line_height - ed->scroll_y;
        *out_x = x;
        *out_y = y;
        return;
    }

    /* Soft wrap enabled: find exact visual row */
    float max_w = get_wrap_width(ed);
    size_t target_ls = line_start(ed, pos);
    size_t target_le = line_end(ed, pos);

    size_t ls = 0;
    int visual_row = 0;

    while (ls < target_ls) {
        size_t le = line_end(ed, ls);
        visual_row += wrap_logical_line(ed, ls, le, max_w, NULL, 0);
        ls = next_line_start(ed, ls);
    }

    WrapRow rows[64];
    int nr = wrap_logical_line(ed, target_ls, target_le, max_w, rows, 64);
    int row_idx = 0;
    for (int r = 0; r < nr; ++r) {
        if (pos >= rows[r].start && (pos <= rows[r].end || r == nr - 1)) {
            row_idx = r;
            break;
        }
    }

    float x = text_left;
    for (size_t i = rows[row_idx].start; i < pos && i < rows[row_idx].end; ++i) {
        x += char_width(ed, char_at(ed, i));
    }

    float y = ed->view_y + ed->pad_y + (ed->font ? ed->font->ascent : 14.0f)
            + (float)(visual_row + row_idx) * ed->line_height - ed->scroll_y;

    *out_x = x;
    *out_y = y;
}

void editor_caret_pixel_pos(const Editor *ed, float *out_x, float *out_y)
{
    if (!ed || !out_x || !out_y) return;
    caret_pixel_pos(ed, ed->caret, out_x, out_y);
}

bool editor_line_col_at_point(const Editor *ed, float px, float py, int *out_line, int *out_col)
{
    if (!ed || !out_line || !out_col) return false;
    float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
    if (px < ed->view_x + gutter || px >= ed->view_x + ed->view_w ||
        py < ed->view_y || py >= ed->view_y + ed->view_h)
        return false;

    size_t idx = index_from_point(ed, px, py);
    size_t len = logical_len(ed);
    if (idx >= len) return false;

    char c = char_at(ed, idx);
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
        return false;

    int line = 1, col = 1;
    for (size_t i = 0; i < idx; ++i) {
        if (char_at(ed, i) == '\n') { line++; col = 1; }
        else col++;
    }
    *out_line = line;
    *out_col = col;
    return true;
}

void editor_insert_completion(Editor *ed, const char *completion)
{
    if (!ed || !completion) return;
    size_t p = ed->caret;
    while (p > 0) {
        char c = char_at(ed, p - 1);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            p--;
        } else {
            break;
        }
    }
    if (p < ed->caret) {
        ed->sel_anchor = p;
        editor_delete_selection(ed);
    }
    editor_insert(ed, completion, strlen(completion));
}

size_t editor_current_word_prefix(const Editor *ed, char *out_prefix, size_t out_cap)
{
    if (!ed || !out_prefix || out_cap == 0) return 0;
    size_t p = ed->caret;
    while (p > 0) {
        char c = char_at(ed, p - 1);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            p--;
        } else {
            break;
        }
    }
    size_t len = ed->caret - p;
    if (len >= out_cap) len = out_cap - 1;
    for (size_t i = 0; i < len; ++i)
        out_prefix[i] = char_at(ed, p + i);
    out_prefix[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/* Selection                                                           */
/* ------------------------------------------------------------------ */

bool editor_has_selection(const Editor *ed)
{
    return ed && ed->sel_anchor != ed->caret;
}

void editor_get_selection(const Editor *ed, size_t *start, size_t *end)
{
    if (!ed) return;
    if (ed->caret < ed->sel_anchor) {
        *start = ed->caret;
        *end = ed->sel_anchor;
    } else {
        *start = ed->sel_anchor;
        *end = ed->caret;
    }
}

void editor_clear_selection(Editor *ed)
{
    if (!ed) return;
    ed->sel_anchor = ed->caret;
}

void editor_delete_selection(Editor *ed)
{
    if (!editor_has_selection(ed)) return;
    size_t a, b;
    editor_get_selection(ed, &a, &b);
    delete_recorded(ed, a, b - a);
    ed->caret = a;
    ed->sel_anchor = a;
}

static void set_caret(Editor *ed, size_t pos, bool extend_sel)
{
    size_t len = logical_len(ed);
    if (pos > len) pos = len;
    ed->caret = pos;
    if (!extend_sel) {
        ed->sel_anchor = pos;
    }
    ed->preferred_col = (float)line_col(ed, pos);
}

static void ensure_caret_visible(Editor *ed)
{
    if (!ed->font || ed->view_h <= 0) return;

    float cx, cy;
    caret_pixel_pos(ed, ed->caret, &cx, &cy);

    float top = ed->view_y + ed->pad_y;
    float bot = ed->view_y + ed->view_h - ed->pad_y;
    float caret_top = cy - ed->font->ascent;
    float caret_bot = cy - ed->font->descent;

    if (caret_top < top) {
        ed->scroll_y -= (top - caret_top);
    } else if (caret_bot > bot) {
        ed->scroll_y += (caret_bot - bot);
    }

    float content_h = (float)editor_total_rows(ed) * ed->line_height + ed->pad_y * 2.0f;
    float max_scroll = fmaxf(0.0f, content_h - ed->view_h);
    if (ed->scroll_y < 0) ed->scroll_y = 0;
    if (ed->scroll_y > max_scroll) ed->scroll_y = max_scroll;

    if (!ed->soft_wrap) {
        float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
        float visible_left = ed->view_x + ed->pad_x + gutter;
        float visible_right = ed->view_x + ed->view_w - 24.0f; /* leave room for vertical scrollbar */

        if (cx < visible_left + 16.0f) {
            ed->scroll_x -= (visible_left + 16.0f - cx);
        } else if (cx > visible_right - 16.0f) {
            ed->scroll_x += (cx - (visible_right - 16.0f));
        }

        float max_content_w = editor_max_content_width(ed);
        float visible_w = fmaxf(100.0f, ed->view_w - ed->pad_x * 2.0f - gutter - 24.0f);
        float max_scroll_x = fmaxf(0.0f, max_content_w - visible_w + 40.0f);
        if (ed->scroll_x < 0) ed->scroll_x = 0;
        if (ed->scroll_x > max_scroll_x) ed->scroll_x = max_scroll_x;
    } else {
        ed->scroll_x = 0.0f;
    }
}

/* ------------------------------------------------------------------ */
/* Public lifecycle                                                    */
/* ------------------------------------------------------------------ */

void editor_apply_theme(Editor *ed, const Theme *th)
{
    if (!ed) return;
    if (!th) th = theme_current();
    if (!th) return;
    memcpy(ed->text_color, th->fg, 4);
    memcpy(ed->caret_color, th->caret, 4);
    memcpy(ed->sel_color, th->selection, 4);
    memcpy(ed->bg, th->bg, 4);
}

bool editor_create(Editor *ed, Font *font, const char *initial)
{
    memset(ed, 0, sizeof(*ed));
    ed->font = font;
    ed->pad_x = 16.0f;
    ed->pad_y = 12.0f;
    ed->show_line_numbers = true;
    ed->gutter_w = 48.0f;
    ed->line_height = font ? font->line_height : 18.0f;

    editor_apply_theme(ed, theme_current());

    const char *init = initial ? initial : "";
    size_t n = strlen(init);
    ed->capacity = n + 64;
    ed->buf = calloc(1, ed->capacity);
    if (!ed->buf) return false;
    memcpy(ed->buf, init, n);
    ed->gap_start = n;
    ed->gap_end = ed->capacity;
    ed->caret = n;
    ed->sel_anchor = n;
    ed->hl_dirty = true;
    if (!highlight_init(&ed->hl)) {
        fprintf(stderr, "warning: syntax highlighter failed to init\n");
    }
    return true;
}

void editor_destroy(Editor *ed)
{
    if (!ed) return;
    undo_stack_free(&ed->undo);
    undo_stack_free(&ed->redo);
    highlight_destroy(&ed->hl);
    free(ed->buf);
    memset(ed, 0, sizeof(*ed));
}

size_t editor_length(const Editor *ed)
{
    return logical_len(ed);
}

void editor_get_text(const Editor *ed, char *out, size_t out_cap)
{
    if (!out || out_cap == 0) return;
    size_t len = logical_len(ed);
    if (len >= out_cap) len = out_cap - 1;
    size_t first = ed->gap_start < len ? ed->gap_start : len;
    memcpy(out, ed->buf, first);
    size_t rest = len - first;
    memcpy(out + first, ed->buf + ed->gap_end, rest);
    out[len] = '\0';
}

void editor_insert(Editor *ed, const char *utf8, size_t nbytes)
{
    if (!utf8 || nbytes == 0) return;
    bool had_sel = editor_has_selection(ed);
    if (had_sel) undo_begin(ed);
    editor_delete_selection(ed);
    size_t pos = ed->caret;
    insert_recorded(ed, pos, utf8, nbytes);
    if (had_sel) undo_end(ed);
    ed->caret = pos + nbytes;
    ed->sel_anchor = ed->caret;
    ed->preferred_col = (float)line_col(ed, ed->caret);
    ensure_caret_visible(ed);
}

/* UTF-8 aware stepping (never split a multi-byte character). */
static bool is_cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

static size_t prev_char(const Editor *ed, size_t pos)
{
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && is_cont(char_at(ed, pos))) pos--;
    return pos;
}

static size_t next_char(const Editor *ed, size_t pos)
{
    size_t len = logical_len(ed);
    if (pos >= len) return len;
    pos++;
    while (pos < len && is_cont(char_at(ed, pos))) pos++;
    return pos;
}

static bool is_pair(char open, char close)
{
    return (open == '(' && close == ')') || (open == '[' && close == ']') ||
           (open == '{' && close == '}') || (open == '"' && close == '"') ||
           (open == '\'' && close == '\'');
}

void editor_backspace(Editor *ed)
{
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
        ed->preferred_col = (float)line_col(ed, ed->caret);
        ensure_caret_visible(ed);
        return;
    }
    if (ed->caret == 0) return;
    size_t pos = prev_char(ed, ed->caret);
    size_t n = ed->caret - pos;
    /* Empty auto-closed pair: "(|)" -> delete both */
    if (n == 1 && is_pair(char_at(ed, pos), char_at(ed, ed->caret)))
        n = 2;
    delete_recorded(ed, pos, n);
    ed->caret = pos;
    ed->sel_anchor = pos;
    ed->preferred_col = (float)line_col(ed, ed->caret);
    ensure_caret_visible(ed);
}

void editor_delete(Editor *ed)
{
    if (editor_has_selection(ed)) {
        editor_backspace(ed);
        return;
    }
    size_t len = logical_len(ed);
    if (ed->caret >= len) return;
    size_t end = next_char(ed, ed->caret);
    delete_recorded(ed, ed->caret, end - ed->caret);
    ed->sel_anchor = ed->caret;
    ed->preferred_col = (float)line_col(ed, ed->caret);
    ensure_caret_visible(ed);
}

void editor_copy(Editor *ed)
{
    if (!editor_has_selection(ed)) return;
    size_t a, b;
    editor_get_selection(ed, &a, &b);
    size_t n = b - a;
    char *tmp = malloc(n + 1);
    if (!tmp) return;
    for (size_t i = 0; i < n; ++i) tmp[i] = char_at(ed, a + i);
    tmp[n] = '\0';
    SDL_SetClipboardText(tmp);
    free(tmp);
}

void editor_cut(Editor *ed)
{
    if (!editor_has_selection(ed)) return;
    editor_copy(ed);
    editor_delete_selection(ed);
    ensure_caret_visible(ed);
}

void editor_paste(Editor *ed)
{
    char *t = SDL_GetClipboardText();
    if (!t) return;
    if (t[0]) editor_insert(ed, t, strlen(t));
    SDL_free(t);
}


static void move_left(Editor *ed, bool extend)
{
    if (!extend && editor_has_selection(ed)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        set_caret(ed, a, false);
        ensure_caret_visible(ed);
        return;
    }
    if (ed->caret > 0) set_caret(ed, prev_char(ed, ed->caret), extend);
    ensure_caret_visible(ed);
}

static void move_right(Editor *ed, bool extend)
{
    if (!extend && editor_has_selection(ed)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        set_caret(ed, b, false);
        ensure_caret_visible(ed);
        return;
    }
    if (ed->caret < logical_len(ed)) set_caret(ed, next_char(ed, ed->caret), extend);
    ensure_caret_visible(ed);
}

static void move_vertical(Editor *ed, int dir, bool extend)
{
    if (!ed->soft_wrap) {
        size_t col = (size_t)ed->preferred_col;
        size_t ls = line_start(ed, ed->caret);

        size_t target_ls;
        if (dir < 0) {
            if (ls == 0) {
                set_caret(ed, 0, extend);
                ensure_caret_visible(ed);
                return;
            }
            target_ls = line_start(ed, ls - 1);
        } else {
            target_ls = next_line_start(ed, ed->caret);
            if (target_ls >= logical_len(ed) && char_at(ed, logical_len(ed) > 0 ? logical_len(ed) - 1 : 0) != '\n'
                && target_ls == logical_len(ed)) {
                /* stay / go end */
            }
        }

        size_t len = logical_len(ed);
        size_t p = target_ls;
        size_t c = 0;
        while (p < len && c < col) {
            if (char_at(ed, p) == '\n') break;
            p++;
            c++;
        }
        set_caret(ed, p, extend);
        ed->preferred_col = (float)col; /* keep preferred column */
        ensure_caret_visible(ed);
        return;
    }

    /* Soft wrap mode: move caret by visual row */
    float cx, cy;
    caret_pixel_pos(ed, ed->caret, &cx, &cy);
    float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
    float target_x = ed->view_x + ed->pad_x + gutter + ed->preferred_col;
    float target_y = cy + (float)dir * ed->line_height;

    float pref = ed->preferred_col;
    size_t target_idx = index_from_point(ed, target_x, target_y);
    set_caret(ed, target_idx, extend);
    ed->preferred_col = pref;
    ensure_caret_visible(ed);
}

/* ------------------------------------------------------------------ */
/* Word / line helpers                                                 */
/* ------------------------------------------------------------------ */

#define INDENT_UNIT "    "
#define INDENT_LEN  4

static bool is_word(char c)
{
    unsigned char u = (unsigned char)c;
    return u == '_' || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') ||
           (u >= 'A' && u <= 'Z') || u >= 0x80;
}

static bool is_blank(char c) { return c == ' ' || c == '\t'; }

static size_t word_left(const Editor *ed, size_t pos)
{
    while (pos > 0 && is_blank(char_at(ed, pos - 1))) pos--;
    if (pos > 0 && char_at(ed, pos - 1) == '\n') return pos - 1;
    if (pos > 0 && is_word(char_at(ed, pos - 1))) {
        while (pos > 0 && is_word(char_at(ed, pos - 1))) pos--;
    } else {
        while (pos > 0) {
            char c = char_at(ed, pos - 1);
            if (is_word(c) || is_blank(c) || c == '\n') break;
            pos--;
        }
    }
    return pos;
}

static size_t word_right(const Editor *ed, size_t pos)
{
    size_t len = logical_len(ed);
    while (pos < len && is_blank(char_at(ed, pos))) pos++;
    if (pos < len && char_at(ed, pos) == '\n') return pos + 1;
    if (pos < len && is_word(char_at(ed, pos))) {
        while (pos < len && is_word(char_at(ed, pos))) pos++;
    } else {
        while (pos < len) {
            char c = char_at(ed, pos);
            if (is_word(c) || is_blank(c) || c == '\n') break;
            pos++;
        }
    }
    return pos;
}

/** Lines covered by the selection (or caret line): [*ls, *le) without final '\n'. */
static void selected_lines(const Editor *ed, size_t *ls, size_t *le)
{
    size_t a = ed->caret, b = ed->caret;
    if (editor_has_selection(ed)) editor_get_selection(ed, &a, &b);
    /* Selection ending at column 0 doesn't include that line */
    if (b > a && line_start(ed, b) == b) b--;
    *ls = line_start(ed, a);
    *le = line_end(ed, b);
}

static void delete_word(Editor *ed, bool forward)
{
    if (editor_has_selection(ed)) {
        editor_backspace(ed);
        return;
    }
    size_t a = ed->caret, b = ed->caret;
    if (forward) b = word_right(ed, ed->caret);
    else         a = word_left(ed, ed->caret);
    if (b <= a) return;
    delete_recorded(ed, a, b - a);
    ed->caret = ed->sel_anchor = a;
    ed->preferred_col = (float)line_col(ed, a);
    ensure_caret_visible(ed);
}

static void move_lines(Editor *ed, int count, bool extend)
{
    int dir = count < 0 ? -1 : 1;
    int n = count < 0 ? -count : count;
    float pref = ed->preferred_col;
    for (int i = 0; i < n; ++i) {
        size_t before = ed->caret;
        ed->preferred_col = pref;
        move_vertical(ed, dir, extend);
        if (ed->caret == before) break;
    }
}

static void page_move(Editor *ed, int dir, bool extend)
{
    int lines = (int)(ed->view_h / (ed->line_height > 0 ? ed->line_height : 18.0f)) - 1;
    if (lines < 1) lines = 1;
    ed->scroll_y += (float)(dir * lines) * ed->line_height;
    move_lines(ed, dir * lines, extend);
}

/* Enter: keep indentation; one extra level after '{'; split "{|}" nicely. */
static void insert_newline(Editor *ed)
{
    undo_begin(ed);
    editor_delete_selection(ed);

    size_t pos = ed->caret;
    size_t ls = line_start(ed, pos);
    char buf[512];
    size_t n = 0;
    buf[n++] = '\n';
    size_t indent = 0;
    for (size_t i = ls; i < pos && n < 256; ++i) {
        char c = char_at(ed, i);
        if (!is_blank(c)) break;
        buf[n++] = c;
        indent++;
    }

    /* Last non-blank char before caret on this line */
    char prev = 0;
    for (size_t i = pos; i > ls; --i) {
        char c = char_at(ed, i - 1);
        if (!is_blank(c)) { prev = c; break; }
    }
    char next = char_at(ed, pos);

    size_t caret_off;
    if (prev == '{') {
        memcpy(buf + n, INDENT_UNIT, INDENT_LEN);
        n += INDENT_LEN;
        caret_off = n;
        if (next == '}') {
            buf[n++] = '\n';
            for (size_t i = 0; i < indent; ++i) buf[n++] = char_at(ed, ls + i);
        }
    } else {
        caret_off = n;
    }

    insert_recorded(ed, pos, buf, n);
    undo_end(ed);
    ed->caret = ed->sel_anchor = pos + caret_off;
    ed->preferred_col = (float)line_col(ed, ed->caret);
    ensure_caret_visible(ed);
}

static char closing_for(char c)
{
    switch (c) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    case '"': return '"';
    case '\'': return '\'';
    default: return 0;
    }
}

/* Typed text with auto-closing brackets/quotes. */
static void type_text(Editor *ed, const char *t)
{
    size_t tl = strlen(t);
    if (tl != 1) {
        editor_insert(ed, t, tl);
        return;
    }
    char ch = t[0];
    size_t pos = ed->caret;
    char next = char_at(ed, pos);
    char prev = pos > 0 ? char_at(ed, pos - 1) : 0;
    bool quote = (ch == '"' || ch == '\'');

    /* Wrap selection: (sel) "sel" */
    char close = closing_for(ch);
    if (close && editor_has_selection(ed)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        undo_begin(ed);
        insert_recorded(ed, b, &close, 1);
        insert_recorded(ed, a, &ch, 1);
        undo_end(ed);
        ed->sel_anchor = a + 1;
        ed->caret = b + 1;
        return;
    }

    /* Typing a closer that's already there: step over it */
    if ((ch == ')' || ch == ']' || ch == '}' || quote) && next == ch && !editor_has_selection(ed)) {
        bool step = true;
        if (quote && prev == '\\') step = false;
        if (step) {
            set_caret(ed, pos + 1, false);
            ensure_caret_visible(ed);
            return;
        }
    }

    /* '}' on a whitespace-only line: dedent one level first */
    if (ch == '}' && !editor_has_selection(ed)) {
        size_t ls = line_start(ed, pos);
        bool only_ws = pos > ls;
        for (size_t i = ls; i < pos; ++i)
            if (!is_blank(char_at(ed, i))) { only_ws = false; break; }
        if (only_ws) {
            size_t remove = 0;
            if (char_at(ed, pos - 1) == '\t') remove = 1;
            else while (remove < INDENT_LEN && pos - remove > ls &&
                        char_at(ed, pos - remove - 1) == ' ') remove++;
            undo_begin(ed);
            delete_recorded(ed, pos - remove, remove);
            ed->caret = ed->sel_anchor = pos - remove;
            editor_insert(ed, "}", 1);
            undo_end(ed);
            return;
        }
    }

    if (close && !editor_has_selection(ed)) {
        bool next_ok = next == 0 || next == '\n' || is_blank(next) ||
                       next == ')' || next == ']' || next == '}' ||
                       next == ';' || next == ',';
        bool prev_ok = !quote || !(is_word(prev) || prev == '\\' || prev == ch);
        if (next_ok && prev_ok) {
            char pair[2] = { ch, close };
            insert_recorded(ed, pos, pair, 2);
            ed->caret = ed->sel_anchor = pos + 1;
            ed->preferred_col = (float)line_col(ed, ed->caret);
            ensure_caret_visible(ed);
            return;
        }
    }

    editor_insert(ed, t, 1);
}

static void duplicate_lines(Editor *ed)
{
    size_t ls, le;
    selected_lines(ed, &ls, &le);
    size_t n = le - ls;
    char *block = malloc(n + 2);
    if (!block) return;
    block[0] = '\n';
    for (size_t i = 0; i < n; ++i) block[i + 1] = char_at(ed, ls + i);
    insert_recorded(ed, le, block, n + 1);
    free(block);
    ed->caret += n + 1;
    ed->sel_anchor += n + 1;
    ensure_caret_visible(ed);
}

static void move_line_block(Editor *ed, int dir)
{
    size_t ls, le;
    selected_lines(ed, &ls, &le);
    size_t len = logical_len(ed);
    size_t a, b; /* range to rewrite */
    size_t shift;
    bool up = dir < 0;

    if (up) {
        if (ls == 0) return;
        a = line_start(ed, ls - 1);
        b = le;
        shift = ls - a;
    } else {
        if (le >= len) return;
        a = ls;
        b = line_end(ed, le + 1);
        shift = b - le;
    }

    char *old = copy_range(ed, a, b - a);
    if (!old) return;
    size_t n = b - a;
    char *neu = malloc(n + 1);
    if (!neu) { free(old); return; }

    /* old = X '\n' Y  ->  neu = Y '\n' X, where the split is at the moving boundary */
    size_t split = up ? (ls - 1 - a) : (le - a); /* index of the '\n' to pivot on */
    size_t xl = split, yl = n - split - 1;
    memcpy(neu, old + split + 1, yl);
    neu[yl] = '\n';
    memcpy(neu + yl + 1, old, xl);

    size_t caret = ed->caret, anchor = ed->sel_anchor;
    undo_begin(ed);
    delete_recorded(ed, a, n);
    insert_recorded(ed, a, neu, n);
    undo_end(ed);
    free(old);
    free(neu);

    if (up) { ed->caret = caret - shift; ed->sel_anchor = anchor - shift; }
    else    { ed->caret = caret + shift; ed->sel_anchor = anchor + shift; }
    ensure_caret_visible(ed);
}

static void adjust_after_insert(size_t *x, size_t at, size_t n)  { if (*x >= at) *x += n; }
static void adjust_after_delete(size_t *x, size_t at, size_t n)
{
    if (*x >= at + n) *x -= n;
    else if (*x > at) *x = at;
}

static void toggle_comment(Editor *ed)
{
    size_t ls, le;
    selected_lines(ed, &ls, &le);

    /* Pass 1: are all non-blank lines commented? what's the min indent? */
    bool all_commented = true, any = false;
    size_t min_indent = (size_t)-1;
    for (size_t p = ls; p <= le; ) {
        size_t e = line_end(ed, p);
        size_t i = p;
        while (i < e && is_blank(char_at(ed, i))) i++;
        if (i < e) {
            any = true;
            if (i - p < min_indent) min_indent = i - p;
            if (!(i + 1 < e && char_at(ed, i) == '/' && char_at(ed, i + 1) == '/'))
                all_commented = false;
        }
        p = e + 1;
        if (e >= le) break;
    }
    if (!any) return;

    size_t caret = ed->caret, anchor = ed->sel_anchor;
    undo_begin(ed);
    /* Pass 2 bottom-up so earlier offsets stay valid. Collect line starts. */
    size_t starts[4096];
    size_t count = 0;
    for (size_t p = ls; count < 4096; ) {
        starts[count++] = p;
        size_t e = line_end(ed, p);
        if (e >= le) break;
        p = e + 1;
    }
    for (size_t k = count; k-- > 0; ) {
        size_t p = starts[k];
        size_t e = line_end(ed, p);
        size_t i = p;
        while (i < e && is_blank(char_at(ed, i))) i++;
        if (i >= e) continue; /* blank line */
        if (all_commented) {
            size_t n = 2;
            if (char_at(ed, i + 2) == ' ') n = 3;
            delete_recorded(ed, i, n);
            adjust_after_delete(&caret, i, n);
            adjust_after_delete(&anchor, i, n);
        } else {
            size_t at = p + min_indent;
            insert_recorded(ed, at, "// ", 3);
            adjust_after_insert(&caret, at, 3);
            adjust_after_insert(&anchor, at, 3);
        }
    }
    undo_end(ed);
    ed->caret = caret;
    ed->sel_anchor = anchor;
    ed->preferred_col = (float)line_col(ed, ed->caret);
    ensure_caret_visible(ed);
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */


static void undo_apply_one(Editor *ed, UndoRec *rec, bool undo)
{
    bool insert = (rec->kind == UNDO_INSERT) != undo;
    if (insert) apply_insert_at(ed, rec->pos, rec->text, rec->len);
    else        apply_delete_range(ed, rec->pos, rec->len);
}

static void undo_transfer(Editor *ed, bool undo)
{
    UndoStack *from = undo ? &ed->undo : &ed->redo;
    UndoStack *to = undo ? &ed->redo : &ed->undo;

    if (from->len <= 0) return;
    unsigned group = from->items[from->len - 1].group;
    while (from->len > 0 && from->items[from->len - 1].group == group) {
        UndoRec rec = from->items[--from->len];
        undo_apply_one(ed, &rec, undo);
        if (undo_stack_reserve(to)) {
            to->items[to->len++] = rec; /* takes ownership of text */
        } else {
            undo_rec_free(&rec);
        }
    }
    ed->preferred_col = (float)line_col(ed, ed->caret);
    ensure_caret_visible(ed);
}

void editor_undo(Editor *ed)
{
    if (!ed) return;
    undo_transfer(ed, true);
}

void editor_redo(Editor *ed)
{
    if (!ed) return;
    undo_transfer(ed, false);
}


void editor_set_text(Editor *ed, const char *text)
{
    if (!ed) return;
    const char *src = text ? text : "";
    size_t n = strlen(src);
    free(ed->buf);
    ed->capacity = n + 64;
    ed->buf = calloc(1, ed->capacity);
    if (!ed->buf) {
        ed->capacity = 0;
        return;
    }
    memcpy(ed->buf, src, n);
    ed->gap_start = n;
    ed->gap_end = ed->capacity;
    ed->caret = 0;
    ed->sel_anchor = 0;
    ed->scroll_y = 0;
    ed->scroll_x = 0;
    undo_stack_clear(&ed->undo);
    undo_stack_clear(&ed->redo);
    highlight_reset(&ed->hl);
    ed->hl_dirty = true;
}

bool editor_load_file(Editor *ed, const char *path)
{
    if (!ed || !path || !path[0]) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open failed: %s\n", path);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return false; }
    char *data = malloc((size_t)sz + 1);
    if (!data) { fclose(fp); return false; }
    size_t rd = fread(data, 1, (size_t)sz, fp);
    fclose(fp);
    data[rd] = '\0';
    /* Normalize CRLF -> LF in place */
    size_t w = 0;
    for (size_t r = 0; r < rd; ++r) {
        if (data[r] == '\r') {
            if (r + 1 < rd && data[r + 1] == '\n') continue;
            data[w++] = '\n';
        } else {
            data[w++] = data[r];
        }
    }
    data[w] = '\0';
    editor_set_text(ed, data);
    free(data);
    snprintf(ed->path, sizeof(ed->path), "%s", path);
    ed->dirty = false;
    printf("Loaded %s (%zu bytes)\n", path, w);
    return true;
}

bool editor_save_file(Editor *ed, const char *path)
{
    if (!ed) return false;
    const char *out = path && path[0] ? path : ed->path;
    if (!out || !out[0]) {
        fprintf(stderr, "save: no path\n");
        return false;
    }
    size_t len = editor_length(ed);
    char *data = malloc(len + 1);
    if (!data) return false;
    editor_get_text(ed, data, len + 1);
    FILE *fp = fopen(out, "wb");
    if (!fp) {
        free(data);
        fprintf(stderr, "save failed: %s\n", out);
        return false;
    }
    size_t wr = fwrite(data, 1, len, fp);
    fclose(fp);
    free(data);
    if (wr != len) {
        fprintf(stderr, "save short write: %s\n", out);
        return false;
    }
    snprintf(ed->path, sizeof(ed->path), "%s", out);
    ed->dirty = false;
    printf("Saved %s (%zu bytes)\n", out, len);
    return true;
}


static int fold_char(int c, bool case_sensitive)
{
    if (!case_sensitive && c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static bool match_at(const Editor *ed, size_t pos, const char *query, size_t qlen, bool case_sensitive)
{
    size_t len = logical_len(ed);
    if (pos + qlen > len) return false;
    for (size_t i = 0; i < qlen; ++i) {
        int a = fold_char((unsigned char)char_at(ed, pos + i), case_sensitive);
        int b = fold_char((unsigned char)query[i], case_sensitive);
        if (a != b) return false;
    }
    return true;
}

bool editor_find_next(Editor *ed, const char *query, bool case_sensitive)
{
    if (!ed || !query || !query[0]) return false;
    size_t qlen = strlen(query);
    size_t len = logical_len(ed);
    if (qlen == 0 || qlen > len) return false;

    size_t start = ed->caret;
    /* If we already have this match selected, step past it */
    if (editor_has_selection(ed)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        if (b - a == qlen && match_at(ed, a, query, qlen, case_sensitive))
            start = a + 1;
    }

    for (size_t i = start; i + qlen <= len; ++i) {
        if (match_at(ed, i, query, qlen, case_sensitive)) {
            ed->sel_anchor = i;
            ed->caret = i + qlen;
            ed->preferred_col = (float)line_col(ed, ed->caret);
            ensure_caret_visible(ed);
            return true;
        }
    }
    /* Wrap */
    for (size_t i = 0; i < start && i + qlen <= len; ++i) {
        if (match_at(ed, i, query, qlen, case_sensitive)) {
            ed->sel_anchor = i;
            ed->caret = i + qlen;
            ed->preferred_col = (float)line_col(ed, ed->caret);
            ensure_caret_visible(ed);
            return true;
        }
    }
    return false;
}

bool editor_find_prev(Editor *ed, const char *query, bool case_sensitive)
{
    if (!ed || !query || !query[0]) return false;
    size_t qlen = strlen(query);
    size_t len = logical_len(ed);
    if (qlen == 0 || qlen > len) return false;

    size_t start = ed->caret;
    if (editor_has_selection(ed)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        if (b - a == qlen && match_at(ed, a, query, qlen, case_sensitive))
            start = a; /* search before this match */
    }
    if (start > 0) start--;

    for (size_t i = start + 1; i-- > 0; ) {
        if (i + qlen <= len && match_at(ed, i, query, qlen, case_sensitive)) {
            ed->sel_anchor = i;
            ed->caret = i + qlen;
            ed->preferred_col = (float)line_col(ed, ed->caret);
            ensure_caret_visible(ed);
            return true;
        }
        if (i == 0) break;
    }
    /* Wrap from end */
    if (len >= qlen) {
        for (size_t i = len - qlen + 1; i-- > start; ) {
            if (match_at(ed, i, query, qlen, case_sensitive)) {
                ed->sel_anchor = i;
                ed->caret = i + qlen;
                ed->preferred_col = (float)line_col(ed, ed->caret);
                ensure_caret_visible(ed);
                return true;
            }
            if (i == 0) break;
        }
    }
    return false;
}


bool editor_replace_next(Editor *ed, const char *query, const char *replacement, bool case_sensitive)
{
    if (!ed || !query || !query[0]) return false;
    size_t qlen = strlen(query);
    const char *rep = replacement ? replacement : "";
    size_t rlen = strlen(rep);

    /* If selection matches query, replace it */
    if (editor_has_selection(ed)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        if (b - a == qlen) {
            bool match = true;
            for (size_t i = 0; i < qlen; ++i) {
                int x = (unsigned char)char_at(ed, a + i);
                int y = (unsigned char)query[i];
                if (!case_sensitive) {
                    if (x >= 'A' && x <= 'Z') x = x - 'A' + 'a';
                    if (y >= 'A' && y <= 'Z') y = y - 'A' + 'a';
                }
                if (x != y) { match = false; break; }
            }
            if (match) {
                editor_delete_selection(ed);
                if (rlen) editor_insert(ed, rep, rlen);
            }
        }
    }
    return editor_find_next(ed, query, case_sensitive);
}

int editor_replace_all(Editor *ed, const char *query, const char *replacement, bool case_sensitive)
{
    if (!ed || !query || !query[0]) return 0;
    int count = 0;
    ed->caret = 0;
    ed->sel_anchor = 0;
    while (editor_find_next(ed, query, case_sensitive)) {
        size_t a, b;
        editor_get_selection(ed, &a, &b);
        editor_delete_selection(ed);
        const char *rep = replacement ? replacement : "";
        size_t rlen = strlen(rep);
        if (rlen) editor_insert(ed, rep, rlen);
        count++;
        if (count > 100000) break; /* safety */
    }
    return count;
}

int editor_count_matches(const Editor *ed, const char *query, bool case_sensitive, int *out_current)
{
    if (out_current) *out_current = 0;
    if (!ed || !query || !query[0]) return 0;
    size_t qlen = strlen(query);
    size_t len = logical_len(ed);
    if (qlen == 0 || qlen > len) return 0;

    size_t sel_a = 0, sel_b = 0;
    bool has_sel = editor_has_selection(ed);
    if (has_sel) editor_get_selection(ed, &sel_a, &sel_b);

    int count = 0;
    int current = 0;
    for (size_t i = 0; i + qlen <= len; ++i) {
        if (match_at(ed, i, query, qlen, case_sensitive)) {
            count++;
            if (has_sel && sel_a == i && sel_b == i + qlen) {
                current = count;
            }
        }
    }
    if (out_current) *out_current = current;
    return count;
}

void editor_set_find_query(Editor *ed, const char *query, bool case_sensitive)
{
    if (!ed) return;
    if (query && query[0]) {
        snprintf(ed->find_query, sizeof(ed->find_query), "%s", query);
        ed->find_case = case_sensitive;
        ed->find_active = true;
    } else {
        ed->find_query[0] = 0;
        ed->find_active = false;
    }
}

void editor_set_soft_wrap(Editor *ed, bool enable)
{
    if (ed) {
        ed->soft_wrap = enable;
        if (enable) ed->scroll_x = 0.0f;
    }
}

bool editor_get_soft_wrap(const Editor *ed)
{
    return ed ? ed->soft_wrap : false;
}

static bool is_bracket_char(char c)
{
    return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}

bool editor_find_matching_bracket(const Editor *ed, size_t pos, size_t *out_match)
{
    if (!ed) return false;
    size_t len = logical_len(ed);
    if (pos >= len) return false;
    char ch = char_at(ed, pos);
    if (!is_bracket_char(ch)) return false;

    /* 1. Try Tree-sitter AST */
    if (highlight_find_matching_bracket(&ed->hl, pos, out_match)) {
        if (*out_match < len && is_bracket_char(char_at(ed, *out_match))) {
            return true;
        }
    }

    /* 2. Fallback: lexical depth scan skipping comments and strings */
    char open_ch, close_ch;
    bool forward;
    if (ch == '(')      { open_ch = '('; close_ch = ')'; forward = true; }
    else if (ch == ')') { open_ch = ')'; close_ch = '('; forward = false; }
    else if (ch == '[') { open_ch = '['; close_ch = ']'; forward = true; }
    else if (ch == ']') { open_ch = ']'; close_ch = '['; forward = false; }
    else if (ch == '{') { open_ch = '{'; close_ch = '}'; forward = true; }
    else if (ch == '}') { open_ch = '}'; close_ch = '{'; forward = false; }
    else return false;

    int depth = 0;
    if (forward) {
        for (size_t i = pos; i < len; ++i) {
            HlKind k = highlight_kind_at(&ed->hl, i);
            if (k == HL_STRING || k == HL_COMMENT) continue;
            char c = char_at(ed, i);
            if (c == open_ch) depth++;
            else if (c == close_ch) {
                depth--;
                if (depth == 0) {
                    if (out_match) *out_match = i;
                    return true;
                }
            }
        }
    } else {
        for (size_t i = pos + 1; i-- > 0; ) {
            HlKind k = highlight_kind_at(&ed->hl, i);
            if (k == HL_STRING || k == HL_COMMENT) {
                if (i == 0) break;
                continue;
            }
            char c = char_at(ed, i);
            if (c == open_ch) depth++;
            else if (c == close_ch) {
                depth--;
                if (depth == 0) {
                    if (out_match) *out_match = i;
                    return true;
                }
            }
            if (i == 0) break;
        }
    }

    return false;
}

int editor_line_count(const Editor *ed)
{
    return ed ? (int)count_lines(ed) : 0;
}

int editor_current_line(const Editor *ed)
{
    if (!ed) return 1;
    int line = 1;
    size_t lim = ed->caret;
    size_t len = logical_len(ed);
    if (lim > len) lim = len;
    for (size_t i = 0; i < lim; ++i) {
        if (char_at(ed, i) == '\n') line++;
    }
    return line;
}

int editor_current_col(const Editor *ed)
{
    if (!ed) return 1;
    return (int)line_col(ed, ed->caret) + 1;
}

void editor_goto_line(Editor *ed, int line_1based)
{
    if (!ed) return;
    int total = (int)count_lines(ed);
    if (line_1based < 1) line_1based = 1;
    if (line_1based > total) line_1based = total;

    size_t pos = 0;
    int line = 1;
    size_t len = logical_len(ed);
    while (line < line_1based && pos < len) {
        if (char_at(ed, pos) == '\n') line++;
        pos++;
    }
    /* pos is start of target line (or end) */
    set_caret(ed, pos, false);
    ensure_caret_visible(ed);
}



void editor_handle_event(Editor *ed, const void *ev_ptr)
{
    if (!ed || !ev_ptr) return;
    const SDL_Event *ev = (const SDL_Event *)ev_ptr;

    switch (ev->type) {
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode key = ev->key.key;
        SDL_Keymod mod = ev->key.mod;
        bool shift = (mod & SDL_KMOD_SHIFT) != 0;
        bool ctrl  = (mod & SDL_KMOD_CTRL) != 0;

        bool alt   = (mod & SDL_KMOD_ALT) != 0;
        size_t len = logical_len(ed);

        if (key == SDLK_BACKSPACE) {
            if (ctrl) delete_word(ed, false);
            else      editor_backspace(ed);
        } else if (key == SDLK_DELETE) {
            if (ctrl) delete_word(ed, true);
            else      editor_delete(ed);
        } else if (key == SDLK_LEFT) {
            if (ctrl) { set_caret(ed, word_left(ed, ed->caret), shift); ensure_caret_visible(ed); }
            else move_left(ed, shift);
        } else if (key == SDLK_RIGHT) {
            if (ctrl) { set_caret(ed, word_right(ed, ed->caret), shift); ensure_caret_visible(ed); }
            else move_right(ed, shift);
        } else if (key == SDLK_UP && alt) {
            move_line_block(ed, -1);
        } else if (key == SDLK_DOWN && alt) {
            move_line_block(ed, +1);
        } else if (key == SDLK_UP) {
            move_vertical(ed, -1, shift);
        } else if (key == SDLK_DOWN) {
            move_vertical(ed, +1, shift);
        } else if (key == SDLK_PAGEUP) {
            page_move(ed, -1, shift);
        } else if (key == SDLK_PAGEDOWN) {
            page_move(ed, +1, shift);
        } else if (key == SDLK_HOME && ctrl) {
            set_caret(ed, 0, shift);
            ensure_caret_visible(ed);
        } else if (key == SDLK_END && ctrl) {
            set_caret(ed, len, shift);
            ensure_caret_visible(ed);
        } else if (key == SDLK_HOME) {
            /* Smart home: first non-blank, then column 0 */
            size_t ls = line_start(ed, ed->caret);
            size_t fnb = ls;
            while (fnb < len && is_blank(char_at(ed, fnb))) fnb++;
            set_caret(ed, ed->caret == fnb ? ls : fnb, shift);
            ensure_caret_visible(ed);
        } else if (key == SDLK_END) {
            set_caret(ed, line_end(ed, ed->caret), shift);
            ensure_caret_visible(ed);
        } else if (key == SDLK_A && ctrl) {
            ed->sel_anchor = 0;
            ed->caret = len;
        } else if (key == SDLK_C && ctrl) {
            editor_copy(ed);
        } else if (key == SDLK_X && ctrl) {
            editor_cut(ed);
        } else if (key == SDLK_V && ctrl) {
            editor_paste(ed);
        } else if (key == SDLK_Z && ctrl && shift) {
            editor_redo(ed);
        } else if (key == SDLK_Z && ctrl) {
            editor_undo(ed);
        } else if (key == SDLK_Y && ctrl) {
            editor_redo(ed);
        } else if (key == SDLK_D && ctrl) {
            duplicate_lines(ed);
        } else if (key == SDLK_SLASH && ctrl) {
            toggle_comment(ed);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            insert_newline(ed);
        } else if (key == SDLK_TAB) {
            editor_insert(ed, INDENT_UNIT, INDENT_LEN);
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT: {
        const char *t = ev->text.text;
        if (t && t[0]) type_text(ed, t);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (ev->button.button != SDL_BUTTON_LEFT) break;
        float mx = (float)ev->button.x;
        float my = (float)ev->button.y;

        /* Check vertical scrollbar click */
        float content_h = (float)editor_total_rows(ed) * ed->line_height + ed->pad_y * 2.0f;
        float max_scroll = fmaxf(0.0f, content_h - ed->view_h);
        float sb_hit_w = 16.0f;

        if (max_scroll > 0.0f && mx >= ed->view_x + ed->view_w - sb_hit_w && mx <= ed->view_x + ed->view_w) {
            float sb_y = ed->view_y + 2.0f;
            float sb_h = ed->view_h - 4.0f;
            float thumb_h = fmaxf(24.0f, sb_h * (sb_h / content_h));
            float thumb_y = sb_y + (ed->scroll_y / max_scroll) * (sb_h - thumb_h);

            if (my >= thumb_y && my <= thumb_y + thumb_h) {
                ed->sb_dragging = true;
                ed->sb_drag_start_y = my;
                ed->sb_drag_start_scroll = ed->scroll_y;
            } else {
                float ratio = (my - sb_y - thumb_h * 0.5f) / (sb_h - thumb_h);
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;
                ed->scroll_y = ratio * max_scroll;
                ed->sb_dragging = true;
                ed->sb_drag_start_y = my;
                ed->sb_drag_start_scroll = ed->scroll_y;
            }
            break;
        }

        /* Check horizontal scrollbar click */
        if (!ed->soft_wrap) {
            float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
            float max_content_w = editor_max_content_width(ed);
            float visible_w = fmaxf(100.0f, ed->view_w - ed->pad_x * 2.0f - gutter - 24.0f);
            float max_scroll_x = fmaxf(0.0f, max_content_w - visible_w + 40.0f);
            float sb_hit_h = 16.0f;

            if (max_scroll_x > 0.0f && my >= ed->view_y + ed->view_h - sb_hit_h && my <= ed->view_y + ed->view_h) {
                float sb_x = ed->view_x + gutter + 2.0f;
                float sb_w = ed->view_w - gutter - 20.0f;
                if (sb_w > 40.0f && mx >= sb_x && mx <= sb_x + sb_w) {
                    float full_w = max_content_w + ed->pad_x * 2.0f + gutter + 40.0f;
                    float thumb_w = fmaxf(24.0f, sb_w * (sb_w / full_w));
                    float thumb_x = sb_x + (ed->scroll_x / max_scroll_x) * (sb_w - thumb_w);

                    if (mx >= thumb_x && mx <= thumb_x + thumb_w) {
                        ed->sb_x_dragging = true;
                        ed->sb_x_drag_start_x = mx;
                        ed->sb_x_drag_start_scroll = ed->scroll_x;
                    } else {
                        float ratio = (mx - sb_x - thumb_w * 0.5f) / (sb_w - thumb_w);
                        if (ratio < 0.0f) ratio = 0.0f;
                        if (ratio > 1.0f) ratio = 1.0f;
                        ed->scroll_x = ratio * max_scroll_x;
                        ed->sb_x_dragging = true;
                        ed->sb_x_drag_start_x = mx;
                        ed->sb_x_drag_start_scroll = ed->scroll_x;
                    }
                    break;
                }
            }
        }

        size_t idx = index_from_point(ed, mx, my);
        bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        set_caret(ed, idx, shift);
        ed->selecting = !shift;
        if (!shift) ed->sel_anchor = ed->caret;
        ensure_caret_visible(ed);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (ed->sb_dragging) {
            if ((ev->motion.state & SDL_BUTTON_LMASK) == 0) {
                ed->sb_dragging = false;
                break;
            }
            float content_h = (float)editor_total_rows(ed) * ed->line_height + ed->pad_y * 2.0f;
            float max_scroll = fmaxf(0.0f, content_h - ed->view_h);
            float sb_h = ed->view_h - 4.0f;
            float thumb_h = fmaxf(24.0f, sb_h * (sb_h / content_h));
            float range = sb_h - thumb_h;
            if (range > 0.0f && max_scroll > 0.0f) {
                float dy = (float)ev->motion.y - ed->sb_drag_start_y;
                float new_scroll = ed->sb_drag_start_scroll + (dy / range) * max_scroll;
                if (new_scroll < 0.0f) new_scroll = 0.0f;
                if (new_scroll > max_scroll) new_scroll = max_scroll;
                ed->scroll_y = new_scroll;
            }
            break;
        }
        if (ed->sb_x_dragging) {
            if ((ev->motion.state & SDL_BUTTON_LMASK) == 0) {
                ed->sb_x_dragging = false;
                break;
            }
            float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
            float max_content_w = editor_max_content_width(ed);
            float visible_w = fmaxf(100.0f, ed->view_w - ed->pad_x * 2.0f - gutter - 24.0f);
            float max_scroll_x = fmaxf(0.0f, max_content_w - visible_w + 40.0f);
            float sb_w = ed->view_w - gutter - 20.0f;
            float full_w = max_content_w + ed->pad_x * 2.0f + gutter + 40.0f;
            float thumb_w = fmaxf(24.0f, sb_w * (sb_w / full_w));
            float range = sb_w - thumb_w;
            if (range > 0.0f && max_scroll_x > 0.0f) {
                float dx = (float)ev->motion.x - ed->sb_x_drag_start_x;
                float new_scroll = ed->sb_x_drag_start_scroll + (dx / range) * max_scroll_x;
                if (new_scroll < 0.0f) new_scroll = 0.0f;
                if (new_scroll > max_scroll_x) new_scroll = max_scroll_x;
                ed->scroll_x = new_scroll;
            }
            break;
        }
        if (!ed->selecting) break;
        if ((ev->motion.state & SDL_BUTTON_LMASK) == 0) {
            ed->selecting = false;
            break;
        }
        size_t idx = index_from_point(ed, (float)ev->motion.x, (float)ev->motion.y);
        set_caret(ed, idx, true);
        ensure_caret_visible(ed);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (ev->button.button == SDL_BUTTON_LEFT) {
            ed->selecting = false;
            ed->sb_dragging = false;
            ed->sb_x_dragging = false;
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        float step = ed->line_height * 3.0f;
        bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;

        if (shift && !ed->soft_wrap) {
            ed->scroll_x -= ev->wheel.y * step;
        } else {
            ed->scroll_y -= ev->wheel.y * step;
            if (!ed->soft_wrap && ev->wheel.x != 0.0f) {
                ed->scroll_x += ev->wheel.x * step;
            }
        }

        float content_h = (float)editor_total_rows(ed) * ed->line_height + ed->pad_y * 2.0f;
        float max_scroll = fmaxf(0.0f, content_h - ed->view_h);
        if (ed->scroll_y < 0) ed->scroll_y = 0;
        if (ed->scroll_y > max_scroll) ed->scroll_y = max_scroll;

        if (!ed->soft_wrap) {
            float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
            float max_content_w = editor_max_content_width(ed);
            float visible_w = fmaxf(100.0f, ed->view_w - ed->pad_x * 2.0f - gutter - 24.0f);
            float max_scroll_x = fmaxf(0.0f, max_content_w - visible_w + 40.0f);
            if (ed->scroll_x < 0) ed->scroll_x = 0;
            if (ed->scroll_x > max_scroll_x) ed->scroll_x = max_scroll_x;
        } else {
            ed->scroll_x = 0.0f;
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Draw                                                                */
/* ------------------------------------------------------------------ */

static const char *gap_read(void *payload, size_t byte, size_t *len)
{
    const Editor *ed = payload;
    size_t total = logical_len(ed);
    if (byte >= total) { *len = 0; return NULL; }
    if (byte < ed->gap_start) {
        *len = ed->gap_start - byte;
        return ed->buf + byte;
    }
    size_t phys = byte + (ed->gap_end - ed->gap_start);
    *len = ed->capacity - phys;
    return ed->buf + phys;
}

void editor_flush_lsp_sync(Editor *ed)
{
    if (!ed || !ed->lsp || !ed->lsp_dirty || !ed->path[0]) return;
    size_t len = logical_len(ed);
    char *text = malloc(len + 1);
    if (text) {
        editor_get_text(ed, text, len + 1);
        lsp_doc_did_change(ed->lsp, ed->path, text, len);
        free(text);
    }
    ed->lsp_dirty = false;
}

void editor_draw(Editor *ed, VkContext *ctx, float x, float y, float w, float h)
{
    if (!ed || !ed->font || !ctx) return;

    ed->view_x = x;
    ed->view_y = y;
    ed->view_w = w;
    ed->view_h = h;
    if (ed->hl.ready && ed->hl_dirty) {
        /* Parse straight out of the gap buffer: no full-text copy. */
        highlight_update_input(&ed->hl, gap_read, ed, logical_len(ed));
        ed->hl_dirty = false;
    }

    if (ed->lsp_dirty && ed->lsp && ed->path[0]) {
        uint64_t now = SDL_GetTicks();
        if (now - ed->lsp_dirty_ms >= 100) {
            editor_flush_lsp_sync(ed);
        }
    }

    if (g_editor_draw) {
        vk_draw_rounded_rect(g_editor_draw, x, y, w, h, 0.0f, 1.0f, ed->bg);
    }

    float gutter = ed->show_line_numbers ? ed->gutter_w : 0.0f;
    float text_left = x + ed->pad_x + gutter - (ed->soft_wrap ? 0.0f : ed->scroll_x);
    float space_w = char_width(ed, ' ');
    float max_w = get_wrap_width(ed);

    typedef struct VisLine {
        int line_no;
        float y;
        bool is_cur;
        bool is_continuation;
    } VisLine;
    VisLine vis_lines[256];
    int num_vis_lines = 0;

    size_t sel_a = 0, sel_b = 0;
    bool has_sel = editor_has_selection(ed);
    if (has_sel) editor_get_selection(ed, &sel_a, &sel_b);

    size_t len = logical_len(ed);

    /* 1. Bracket match search for caret */
    size_t b1 = (size_t)-1, b2 = (size_t)-1;
    if (ed->caret < len && is_bracket_char(char_at(ed, ed->caret))) {
        b1 = ed->caret;
        editor_find_matching_bracket(ed, b1, &b2);
    } else if (ed->caret > 0 && is_bracket_char(char_at(ed, ed->caret - 1))) {
        b1 = ed->caret - 1;
        editor_find_matching_bracket(ed, b1, &b2);
    }

    /* 2. Active caret line */
    int cur_line = editor_current_line(ed);

    size_t i = 0;
    size_t line_start_idx = 0;
    float line_y = y + ed->pad_y + ed->font->ascent - ed->scroll_y;
    char linebuf[4096];
    int line_no = 1;
    int prev_indent_levels = 0;

    while (i <= len) {
        bool at_end = (i == len);
        bool at_nl = !at_end && char_at(ed, i) == '\n';

        if (at_end || at_nl) {
            size_t line_len = i - line_start_idx;
            if (line_len >= sizeof(linebuf)) line_len = sizeof(linebuf) - 1;

            for (size_t k = 0; k < line_len; ++k)
                linebuf[k] = char_at(ed, line_start_idx + k);
            linebuf[line_len] = 0;

            /* Indent level of this logical line */
            int indent_levels = 0;
            size_t ws_cols = 0;
            bool only_ws = true;
            for (size_t k = 0; k < line_len; ++k) {
                if (linebuf[k] == ' ') ws_cols++;
                else if (linebuf[k] == '\t') ws_cols += 4;
                else { only_ws = false; break; }
            }
            if (!only_ws) {
                indent_levels = (int)(ws_cols / 4);
                prev_indent_levels = indent_levels;
            } else {
                /* For blank / whitespace lines, carry over previous indentation */
                indent_levels = prev_indent_levels;
            }

            /* Logical line can be wrapped into 1 or more rows */
            WrapRow rows[64];
            int nr = wrap_logical_line(ed, line_start_idx, i, max_w, rows, 64);

            for (int r = 0; r < nr; ++r) {
                size_t r_start = rows[r].start;
                size_t r_end   = rows[r].end;
                size_t r_len   = r_end > r_start ? r_end - r_start : 0;
                bool is_continuation = rows[r].continuation;

                bool row_visible = (line_y + ed->font->descent > y &&
                                     line_y - ed->font->ascent < y + h);

                if (row_visible) {
                    /* Record visible line for gutter overlay */
                    if (ed->show_line_numbers && num_vis_lines < 256) {
                        vis_lines[num_vis_lines].line_no = line_no;
                        vis_lines[num_vis_lines].y = line_y;
                        vis_lines[num_vis_lines].is_cur = (line_no == cur_line);
                        vis_lines[num_vis_lines].is_continuation = is_continuation;
                        num_vis_lines++;
                    }

                    /* Feature: Current line highlight */
                    if (line_no == cur_line && g_editor_draw) {
                        const Theme *th = theme_current();
                        vk_draw_rounded_rect(g_editor_draw, x + gutter, line_y - ed->font->ascent,
                                             w - gutter, ed->line_height, 0.0f, 1.0f, th->line_highlight);
                    }

                    /* Feature: Indent guides (drawn on enclosing scopes: col 4, 8... strictly before text) */
                    if (g_editor_draw && indent_levels > 1) {
                        const Theme *th = theme_current();
                        uint8_t guide_col[4];
                        if (th && (th->indent_guide[0] || th->indent_guide[1] || th->indent_guide[2] || th->indent_guide[3])) {
                            memcpy(guide_col, th->indent_guide, 4);
                        } else {
                            guide_col[0] = 62; guide_col[1] = 68; guide_col[2] = 81; guide_col[3] = 90;
                        }
                        for (int lvl = 1; lvl < indent_levels; ++lvl) {
                            float gx = text_left + (float)(lvl * 4) * space_w;
                            if (gx >= x + gutter && gx < x + w - 16.0f) {
                                vk_draw_rounded_rect(g_editor_draw, gx, line_y - ed->font->ascent,
                                                     1.0f, ed->line_height, 0.0f, 1.0f, guide_col);
                            }
                        }
                    }

                    /* Feature: Highlight every find match */
                    if (ed->find_active && ed->find_query[0] && g_editor_draw) {
                        size_t qlen = strlen(ed->find_query);
                        if (qlen > 0 && r_len >= qlen) {
                            for (size_t k = r_start; k + qlen <= r_end; ++k) {
                                if (match_at(ed, k, ed->find_query, qlen, ed->find_case)) {
                                    float mx = text_left;
                                    for (size_t t = r_start; t < k; ++t)
                                        mx += char_width(ed, char_at(ed, t));
                                    float mw = 0.0f;
                                    for (size_t t = k; t < k + qlen; ++t)
                                        mw += char_width(ed, char_at(ed, t));

                                    if (mx + mw >= x + gutter && mx < x + w) {
                                        bool is_active_match = has_sel && sel_a == k && sel_b == k + qlen;
                                        uint8_t match_c[4];
                                        if (is_active_match) {
                                            match_c[0] = 255; match_c[1] = 160; match_c[2] = 40; match_c[3] = 180;
                                        } else {
                                            match_c[0] = 255; match_c[1] = 225; match_c[2] = 100; match_c[3] = 120;
                                        }
                                        vk_draw_rounded_rect(g_editor_draw, mx, line_y - ed->font->ascent,
                                                             mw, ed->line_height, 2.0f, 1.0f, match_c);
                                    }
                                }
                            }
                        }
                    }

                    /* Selection highlight for this row */
                    if (has_sel && g_editor_draw) {
                        size_t hs = sel_a > r_start ? sel_a : r_start;
                        size_t he = sel_b < r_end ? sel_b : r_end;
                        if (hs < he) {
                            float sx = text_left;
                            for (size_t k = r_start; k < hs; ++k)
                                sx += char_width(ed, char_at(ed, k));
                            float sw = 0;
                            for (size_t k = hs; k < he; ++k)
                                sw += char_width(ed, char_at(ed, k));
                            if (sw > 0 && sx + sw >= x + gutter && sx < x + w) {
                                vk_draw_rounded_rect(g_editor_draw, sx, line_y - ed->font->ascent,
                                                     sw, ed->line_height, 0.0f, 1.0f, ed->sel_color);
                            }
                        }
                    }

                    /* Feature: Matching bracket highlight */
                    if (g_editor_draw && b1 != (size_t)-1 && b2 != (size_t)-1) {
                        const Theme *th = theme_current();
                        const uint8_t *bracket_bg = th->bracket_match;
                        if (b1 >= r_start && b1 < r_end) {
                            float bx = text_left;
                            for (size_t k = r_start; k < b1; ++k)
                                bx += char_width(ed, char_at(ed, k));
                            float bw = char_width(ed, char_at(ed, b1));
                            if (bx + bw >= x + gutter && bx < x + w) {
                                vk_draw_rounded_rect(g_editor_draw, bx, line_y - ed->font->ascent,
                                                     bw, ed->line_height, 2.0f, 1.0f, bracket_bg);
                            }
                        }
                        if (b2 >= r_start && b2 < r_end) {
                            float bx = text_left;
                            for (size_t k = r_start; k < b2; ++k)
                                bx += char_width(ed, char_at(ed, k));
                            float bw = char_width(ed, char_at(ed, b2));
                            if (bx + bw >= x + gutter && bx < x + w) {
                                vk_draw_rounded_rect(g_editor_draw, bx, line_y - ed->font->ascent,
                                                     bw, ed->line_height, 2.0f, 1.0f, bracket_bg);
                            }
                        }
                    }

                    /* Syntax-coloured runs for this row */
                    if (r_len > 0) {
                        size_t row_off = r_start - line_start_idx;
                        size_t col0 = 0;
                        while (col0 < r_len) {
                            size_t abs_pos = r_start + col0;
                            HlKind k = highlight_kind_at(&ed->hl, abs_pos);
                            size_t col1 = col0 + 1;
                            while (col1 < r_len &&
                                   highlight_kind_at(&ed->hl, r_start + col1) == k)
                                col1++;
                            char tmp[2048];
                            size_t n = col1 - col0;
                            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
                            memcpy(tmp, linebuf + row_off + col0, n);
                            tmp[n] = 0;
                            uint8_t col[4];
                            highlight_color(k, col);
                            if (k == HL_DEFAULT) {
                                col[0] = ed->text_color[0];
                                col[1] = ed->text_color[1];
                                col[2] = ed->text_color[2];
                                col[3] = ed->text_color[3];
                            }
                            float xoff = 0;
                            for (size_t t = 0; t < col0; ++t)
                                xoff += char_width(ed, linebuf[row_off + t]);

                            float rx = text_left + xoff;
                            float rw = 0.0f;
                            for (size_t t = 0; t < n; ++t)
                                rw += char_width(ed, tmp[t]);

                            if (rx + rw >= x + gutter && rx < x + w) {
                                font_draw(ed->font, ctx, rx, line_y, tmp, col);
                            }
                            col0 = col1;
                        }
                    }

                    /* Feature: LSP Diagnostic underline */
                    if (ed->lsp && g_editor_draw) {
                        const LspDiagnostic *diag = NULL;
                        if (lsp_diagnostics_for_line(ed->lsp, line_no, &diag) > 0 && diag) {
                            uint8_t dcol[4];
                            if (diag->severity == LSP_SEV_ERROR) {
                                dcol[0] = 235; dcol[1] = 87; dcol[2] = 87; dcol[3] = 240;
                            } else if (diag->severity == LSP_SEV_WARNING) {
                                dcol[0] = 242; dcol[1] = 201; dcol[2] = 76; dcol[3] = 240;
                            } else {
                                dcol[0] = 80; dcol[1] = 160; dcol[2] = 240; dcol[3] = 240;
                            }
                            float sq_x = text_left;
                            float sq_w = fmaxf(24.0f, (float)r_len * space_w);
                            if (sq_x + sq_w >= x + gutter && sq_x < x + w) {
                                vk_draw_rounded_rect(g_editor_draw, sq_x, line_y + 2.0f, sq_w, 2.0f, 1.0f, 1.0f, dcol);
                            }
                        }
                    }
                }

                line_y += ed->line_height;
            }

            if (at_end) break;
            line_start_idx = i + 1;
            line_no++;
            i++;
            continue;
        }
        i++;
    }

    /* Caret */
    if (g_editor_draw) {
        float cx, cy;
        caret_pixel_pos(ed, ed->caret, &cx, &cy);
        float chh = ed->font->ascent - ed->font->descent;
        if (cy + ed->font->descent > y && cy - ed->font->ascent < y + h) {
            if (cx >= x + gutter && cx <= x + w - 4.0f) {
                vk_draw_rounded_rect(g_editor_draw,
                                     cx, cy - ed->font->ascent,
                                     2.0f, chh,
                                     1.0f, 1.0f, ed->caret_color);
            }
        }
    }

    /* Gutter background & line numbers: occludes any horizontally scrolled text */
    if (ed->show_line_numbers && gutter > 0.0f) {
        const Theme *th = theme_current();
        if (g_editor_draw) {
            /* Gutter background */
            vk_draw_rounded_rect(g_editor_draw, x, y, gutter, h, 0.0f, 1.0f, th->gutter_bg);
        }
        if (ed->font) {
            for (int v = 0; v < num_vis_lines; ++v) {
                if (!vis_lines[v].is_continuation) {
                    char num[16];
                    snprintf(num, sizeof(num), "%d", vis_lines[v].line_no);
                    float nw = font_measure(ed->font, num);
                    uint8_t num_c[4];
                    if (vis_lines[v].is_cur) {
                        memcpy(num_c, th->fg, 4);
                    } else {
                        memcpy(num_c, th->gutter_fg, 4);
                    }
                    font_draw(ed->font, ctx, x + gutter - nw - 8.0f, vis_lines[v].y, num, num_c);

                    /* Feature: LSP Diagnostic gutter dot */
                    if (ed->lsp && g_editor_draw) {
                        const LspDiagnostic *diag = NULL;
                        if (lsp_diagnostics_for_line(ed->lsp, vis_lines[v].line_no, &diag) > 0 && diag) {
                            uint8_t dcol[4];
                            if (diag->severity == LSP_SEV_ERROR) {
                                dcol[0] = 235; dcol[1] = 87; dcol[2] = 87; dcol[3] = 255;
                            } else if (diag->severity == LSP_SEV_WARNING) {
                                dcol[0] = 242; dcol[1] = 201; dcol[2] = 76; dcol[3] = 255;
                            } else {
                                dcol[0] = 80; dcol[1] = 160; dcol[2] = 240; dcol[3] = 255;
                            }
                            vk_draw_rounded_rect(g_editor_draw, x + 4.0f, vis_lines[v].y - 8.0f, 6.0f, 6.0f, 3.0f, 1.0f, dcol);
                        }
                    }
                } else {
                    uint8_t wrap_dot[4];
                    memcpy(wrap_dot, th->gutter_fg, 4);
                    wrap_dot[3] = 160;
                    font_draw(ed->font, ctx, x + gutter - 12.0f, vis_lines[v].y, "·", wrap_dot);
                }
            }
        }
    }

    /* Feature: Visible vertical scrollbar */
    float content_h = (float)editor_total_rows(ed) * ed->line_height + ed->pad_y * 2.0f;
    if (g_editor_draw && content_h > h) {
        float sb_w = 6.0f;
        float sb_pad_r = 3.0f;
        float sb_x = x + w - sb_w - sb_pad_r;
        float sb_y = y + 2.0f;
        float sb_h = h - 4.0f;

        /* Track */
        uint8_t track_c[4] = { 240, 240, 244, 90 };
        vk_draw_rounded_rect(g_editor_draw, sb_x - 1.0f, sb_y, sb_w + 2.0f, sb_h, 3.0f, 1.0f, track_c);

        float thumb_h = fmaxf(24.0f, sb_h * (sb_h / content_h));
        float max_scroll = content_h - h;
        float thumb_y = sb_y + (ed->scroll_y / max_scroll) * (sb_h - thumb_h);
        if (thumb_y < sb_y) thumb_y = sb_y;
        if (thumb_y + thumb_h > sb_y + sb_h) thumb_y = sb_y + sb_h - thumb_h;

        uint8_t thumb_c[4] = { 170, 175, 185, 200 };
        if (ed->sb_dragging) {
            thumb_c[0] = 120; thumb_c[1] = 125; thumb_c[2] = 135; thumb_c[3] = 240;
        }
        vk_draw_rounded_rect(g_editor_draw, sb_x, thumb_y, sb_w, thumb_h, 3.0f, 1.0f, thumb_c);
    }

    /* Feature: Horizontal scrollbar */
    if (!ed->soft_wrap && g_editor_draw) {
        float max_content_w = editor_max_content_width(ed);
        float visible_w = fmaxf(100.0f, w - ed->pad_x * 2.0f - gutter - 24.0f);
        float max_scroll_x = fmaxf(0.0f, max_content_w - visible_w + 40.0f);

        if (max_scroll_x > 0.0f) {
            float sb_h = 6.0f;
            float sb_pad_b = 3.0f;
            float sb_y = y + h - sb_h - sb_pad_b;
            float sb_x = x + gutter + 2.0f;
            float sb_w = w - gutter - 20.0f; /* leave room for vertical scrollbar */

            if (sb_w > 40.0f) {
                /* Track */
                uint8_t track_c[4] = { 240, 240, 244, 90 };
                vk_draw_rounded_rect(g_editor_draw, sb_x, sb_y - 1.0f, sb_w, sb_h + 2.0f, 3.0f, 1.0f, track_c);

                float full_w = max_content_w + ed->pad_x * 2.0f + gutter + 40.0f;
                float thumb_w = fmaxf(24.0f, sb_w * (sb_w / full_w));
                float thumb_x = sb_x + (ed->scroll_x / max_scroll_x) * (sb_w - thumb_w);
                if (thumb_x < sb_x) thumb_x = sb_x;
                if (thumb_x + thumb_w > sb_x + sb_w) thumb_x = sb_x + sb_w - thumb_w;

                uint8_t thumb_c[4] = { 170, 175, 185, 200 };
                if (ed->sb_x_dragging) {
                    thumb_c[0] = 120; thumb_c[1] = 125; thumb_c[2] = 135; thumb_c[3] = 240;
                }
                vk_draw_rounded_rect(g_editor_draw, thumb_x, sb_y, thumb_w, sb_h, 3.0f, 1.0f, thumb_c);
            }
        }
    }
}
