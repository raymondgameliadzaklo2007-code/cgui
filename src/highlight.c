/**
 * highlight.c – Tree-sitter C highlighting
 */

#include "highlight.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_c(void);

/* Known-good captures only (no unknown node types). */
static const char *C_HIGHLIGHT_QUERY =
    "\"break\" @keyword\n"
    "\"case\" @keyword\n"
    "\"const\" @keyword\n"
    "\"continue\" @keyword\n"
    "\"default\" @keyword\n"
    "\"do\" @keyword\n"
    "\"else\" @keyword\n"
    "\"enum\" @keyword\n"
    "\"extern\" @keyword\n"
    "\"for\" @keyword\n"
    "\"if\" @keyword\n"
    "\"inline\" @keyword\n"
    "\"return\" @keyword\n"
    "\"sizeof\" @keyword\n"
    "\"static\" @keyword\n"
    "\"struct\" @keyword\n"
    "\"switch\" @keyword\n"
    "\"typedef\" @keyword\n"
    "\"union\" @keyword\n"
    "\"volatile\" @keyword\n"
    "\"while\" @keyword\n"
    "\"goto\" @keyword\n"
    "(primitive_type) @type\n"
    "(type_identifier) @type\n"
    "(string_literal) @string\n"
    "(system_lib_string) @string\n"
    "(char_literal) @string\n"
    "(number_literal) @number\n"
    "(comment) @comment\n"
    "(preproc_directive) @keyword\n"
    "(preproc_include) @keyword\n"
    "(preproc_def) @keyword\n"
    "(call_expression function: (identifier) @function)\n"
    "(function_declarator declarator: (identifier) @function)\n"
    "(field_identifier) @property\n"
    ;

static HlKind capture_to_kind(const char *name, uint32_t len)
{
    if (len == 7 && !strncmp(name, "keyword", 7)) return HL_KEYWORD;
    if (len == 4 && !strncmp(name, "type", 4)) return HL_TYPE;
    if (len == 8 && !strncmp(name, "function", 8)) return HL_FUNCTION;
    if (len == 6 && !strncmp(name, "string", 6)) return HL_STRING;
    if (len == 6 && !strncmp(name, "number", 6)) return HL_NUMBER;
    if (len == 7 && !strncmp(name, "comment", 7)) return HL_COMMENT;
    if (len == 7 && !strncmp(name, "preproc", 7)) return HL_PREPROC;
    if (len == 8 && !strncmp(name, "operator", 8)) return HL_OPERATOR;
    if (len == 8 && !strncmp(name, "constant", 8)) return HL_CONSTANT;
    if (len == 8 && !strncmp(name, "property", 8)) return HL_PROPERTY;
    return HL_DEFAULT;
}

bool highlight_init(Highlighter *hl)
{
    memset(hl, 0, sizeof(*hl));

    TSParser *parser = ts_parser_new();
    if (!parser) return false;

    const TSLanguage *lang = tree_sitter_c();
    if (!ts_parser_set_language(parser, lang)) {
        fprintf(stderr, "highlight: language version mismatch\n");
        ts_parser_delete(parser);
        return false;
    }

    uint32_t err_offset = 0;
    TSQueryError err = TSQueryErrorNone;
    TSQuery *query = ts_query_new(lang, C_HIGHLIGHT_QUERY, (uint32_t)strlen(C_HIGHLIGHT_QUERY),
                                  &err_offset, &err);
    if (!query) {
        fprintf(stderr, "highlight: query error %d at %u\n", (int)err, err_offset);
        ts_parser_delete(parser);
        return false;
    }

    hl->parser = parser;
    hl->language = (void *)lang;
    hl->query = query;
    hl->need_full = true;
    hl->ready = true;
    printf("Tree-sitter C highlighter ready\n");
    return true;
}

void highlight_destroy(Highlighter *hl)
{
    if (!hl) return;
    if (hl->tree) ts_tree_delete((TSTree *)hl->tree);
    if (hl->query) ts_query_delete((TSQuery *)hl->query);
    if (hl->parser) ts_parser_delete((TSParser *)hl->parser);
    free(hl->spans);
    memset(hl, 0, sizeof(*hl));
}

void highlight_reset(Highlighter *hl)
{
    if (!hl) return;
    if (hl->tree) ts_tree_delete((TSTree *)hl->tree);
    hl->tree = NULL;
    hl->span_count = 0;
    hl->has_dirty = false;
    hl->need_full = true;
}

/* ------------------------------------------------------------------ */
/* Edits                                                               */
/* ------------------------------------------------------------------ */

/* Map an old offset through an edit. Offsets inside the replaced region
 * collapse to its start (span starts) or new end (span ends). */
static size_t map_pos(size_t p, size_t start, size_t old_end, size_t new_end, bool is_end)
{
    if (p <= start) return p;
    if (p >= old_end) return p - old_end + new_end;
    return is_end ? new_end : start;
}

void highlight_edit(Highlighter *hl,
                    size_t start_byte, size_t old_end_byte, size_t new_end_byte,
                    HlPoint start_point, HlPoint old_end_point, HlPoint new_end_point)
{
    if (!hl || !hl->ready) return;

    if (hl->tree) {
        TSInputEdit e = {
            .start_byte = (uint32_t)start_byte,
            .old_end_byte = (uint32_t)old_end_byte,
            .new_end_byte = (uint32_t)new_end_byte,
            .start_point = { start_point.row, start_point.column },
            .old_end_point = { old_end_point.row, old_end_point.column },
            .new_end_point = { new_end_point.row, new_end_point.column },
        };
        ts_tree_edit((TSTree *)hl->tree, &e);
    }

    /* Shift existing spans so drawing stays aligned until the re-parse. */
    size_t w = 0;
    for (size_t i = 0; i < hl->span_count; ++i) {
        HlSpan s = hl->spans[i];
        s.start = map_pos(s.start, start_byte, old_end_byte, new_end_byte, false);
        s.end = map_pos(s.end, start_byte, old_end_byte, new_end_byte, true);
        if (s.end <= s.start) continue;
        hl->spans[w++] = s;
    }
    hl->span_count = w;

    /* Widen the dirty range (kept in current coordinates). */
    if (hl->has_dirty) {
        size_t lo = map_pos(hl->dirty_lo, start_byte, old_end_byte, new_end_byte, false);
        size_t hi = map_pos(hl->dirty_hi, start_byte, old_end_byte, new_end_byte, true);
        hl->dirty_lo = lo < start_byte ? lo : start_byte;
        hl->dirty_hi = hi > new_end_byte ? hi : new_end_byte;
    } else {
        hl->has_dirty = true;
        hl->dirty_lo = start_byte;
        hl->dirty_hi = new_end_byte;
    }
}

/* ------------------------------------------------------------------ */
/* Parse + span rebuild                                                */
/* ------------------------------------------------------------------ */

typedef struct ReadCtx {
    HlReadFn fn;
    void *payload;
} ReadCtx;

static const char *ts_read_cb(void *payload, uint32_t byte, TSPoint pos, uint32_t *bytes_read)
{
    (void)pos;
    ReadCtx *rc = payload;
    size_t n = 0;
    const char *p = rc->fn(rc->payload, byte, &n);
    if (!p) n = 0;
    if (n > UINT32_MAX) n = UINT32_MAX;
    *bytes_read = (uint32_t)n;
    return p ? p : "";
}

typedef struct StrCtx {
    const char *text;
    size_t len;
} StrCtx;

static const char *str_read(void *payload, size_t byte, size_t *len)
{
    StrCtx *s = payload;
    if (byte >= s->len) { *len = 0; return NULL; }
    *len = s->len - byte;
    return s->text + byte;
}

static char byte_at(HlReadFn read, void *payload, size_t i)
{
    size_t n = 0;
    const char *p = read(payload, i, &n);
    return (p && n) ? p[0] : '\0';
}

static int span_cmp(const void *a, const void *b)
{
    const HlSpan *sa = a, *sb = b;
    if (sa->start < sb->start) return -1;
    if (sa->start > sb->start) return 1;
    if (sa->end > sb->end) return -1;
    if (sa->end < sb->end) return 1;
    /* Deterministic tie-break so full and incremental rebuilds agree. */
    return (int)sa->kind - (int)sb->kind;
}

static bool span_push(HlSpan **arr, size_t *count, size_t *cap, HlSpan s)
{
    if (*count >= *cap) {
        size_t nc = *cap ? *cap * 2 : 256;
        HlSpan *ns = realloc(*arr, nc * sizeof(HlSpan));
        if (!ns) return false;
        *arr = ns;
        *cap = nc;
    }
    (*arr)[(*count)++] = s;
    return true;
}

/* Collect captures intersecting [lo, hi) (whole file when full). */
static void collect(Highlighter *hl, TSTree *tree, bool full, size_t lo, size_t hi,
                    HlSpan **out, size_t *count, size_t *cap)
{
    TSQueryCursor *cursor = ts_query_cursor_new();
    if (!cursor) return;
    if (!full) ts_query_cursor_set_byte_range(cursor, (uint32_t)lo, (uint32_t)hi);
    ts_query_cursor_exec(cursor, (TSQuery *)hl->query, ts_tree_root_node(tree));

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            TSQueryCapture cap_ = match.captures[i];
            uint32_t nlen = 0;
            const char *name = ts_query_capture_name_for_id((TSQuery *)hl->query,
                                                            cap_.index, &nlen);
            HlKind kind = capture_to_kind(name, nlen);
            if (kind == HL_DEFAULT) continue;
            size_t s = ts_node_start_byte(cap_.node);
            size_t e = ts_node_end_byte(cap_.node);
            if (e <= s) continue;
            if (!full && !(s < hi && e > lo)) continue;
            span_push(out, count, cap, (HlSpan){ .start = s, .end = e, .kind = kind });
        }
    }
    ts_query_cursor_delete(cursor);
}

/* Make spans non-overlapping. Input sorted by (start asc, end desc), so an
 * enclosing (longer) span comes first and wins, matching the old lookup. */
static void flatten(Highlighter *hl)
{
    size_t w = 0;
    size_t last_end = 0;
    for (size_t i = 0; i < hl->span_count; ++i) {
        HlSpan s = hl->spans[i];
        if (w > 0 && s.start < last_end) {
            if (s.end <= last_end) continue;
            s.start = last_end;
        }
        hl->spans[w++] = s;
        last_end = s.end;
    }
    hl->span_count = w;
}

bool highlight_update_input(Highlighter *hl, HlReadFn read, void *payload, size_t len)
{
    if (!hl || !hl->ready || !read) return false;

    TSTree *old = hl->tree;
    bool full = hl->need_full || !old;

    ReadCtx rc = { .fn = read, .payload = payload };
    TSInput input = {
        .payload = &rc,
        .read = ts_read_cb,
        .encoding = TSInputEncodingUTF8,
    };
    TSTree *tree = ts_parser_parse((TSParser *)hl->parser, full ? NULL : old, input);
    if (!tree) return false;

    size_t lo = hl->dirty_lo, hi = hl->dirty_hi;
    if (!full) {
        uint32_t n = 0;
        TSRange *ranges = ts_tree_get_changed_ranges(old, tree, &n);
        for (uint32_t i = 0; i < n; ++i) {
            if (!hl->has_dirty) {
                lo = ranges[i].start_byte;
                hi = ranges[i].end_byte;
                hl->has_dirty = true;
            }
            if (ranges[i].start_byte < lo) lo = ranges[i].start_byte;
            if (ranges[i].end_byte > hi) hi = ranges[i].end_byte;
        }
        free(ranges);
        if (!hl->has_dirty) full = false; /* nothing changed */
    }

    if (old) ts_tree_delete(old);
    hl->tree = tree;

    if (full) {
        hl->span_count = 0;
        collect(hl, tree, true, 0, len, &hl->spans, &hl->span_count, &hl->span_cap);
        if (hl->span_count > 1)
            qsort(hl->spans, hl->span_count, sizeof(HlSpan), span_cmp);
        flatten(hl);
    } else if (hl->has_dirty) {
        if (hi > len) hi = len;
        if (lo > hi) lo = hi;
        /* Changed ranges are tight; nodes touching their edges can still
         * change kind. Widen to whole lines on both sides. */
        while (lo > 0 && byte_at(read, payload, lo - 1) != '\n') lo--;
        while (hi < len && byte_at(read, payload, hi) != '\n') hi++;
        if (hi < len) hi++;
        if (hi <= lo) hi = lo + 1;

        HlSpan *fresh = NULL;
        size_t fcount = 0, fcap = 0;
        collect(hl, tree, false, lo, hi, &fresh, &fcount, &fcap);
        if (fcount > 1) qsort(fresh, fcount, sizeof(HlSpan), span_cmp);

        /* Merge: keep old spans outside [lo, hi), add fresh ones. */
        size_t total = hl->span_count + fcount;
        HlSpan *merged = malloc((total ? total : 1) * sizeof(HlSpan));
        if (merged) {
            size_t m = 0, i = 0, j = 0;
            while (i < hl->span_count || j < fcount) {
                if (i < hl->span_count) {
                    HlSpan *s = &hl->spans[i];
                    if (s->start < hi && s->end > lo) { i++; continue; }
                    if (j >= fcount || span_cmp(s, &fresh[j]) <= 0) {
                        merged[m++] = *s; i++; continue;
                    }
                }
                merged[m++] = fresh[j++];
            }
            free(hl->spans);
            hl->spans = merged;
            hl->span_count = m;
            hl->span_cap = total ? total : 1;
            flatten(hl);
        }
        free(fresh);
    }

    hl->has_dirty = false;
    hl->need_full = false;
    hl->dirty_lo = hl->dirty_hi = 0;
    return true;
}

bool highlight_update(Highlighter *hl, const char *text, size_t len)
{
    StrCtx s = { .text = text ? text : "", .len = text ? len : 0 };
    return highlight_update_input(hl, str_read, &s, s.len);
}

HlKind highlight_kind_at(const Highlighter *hl, size_t offset)
{
    if (!hl || !hl->spans || hl->span_count == 0) return HL_DEFAULT;

    /* Last span with start <= offset. */
    size_t lo = 0, hi = hl->span_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (hl->spans[mid].start <= offset) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return HL_DEFAULT;
    const HlSpan *s = &hl->spans[lo - 1];
    return offset < s->end ? s->kind : HL_DEFAULT;
}

void highlight_color(HlKind kind, uint8_t out[4])
{
    theme_color_syntax(kind, out);
}

bool highlight_find_matching_bracket(const Highlighter *hl, size_t pos, size_t *out_match)
{
    if (!hl || !hl->ready || !hl->tree) return false;

    TSTree *tree = (TSTree *)hl->tree;
    TSNode root = ts_tree_root_node(tree);
    TSNode node = ts_node_descendant_for_byte_range(root, (uint32_t)pos, (uint32_t)pos + 1);
    if (ts_node_is_null(node)) return false;

    const char *type = ts_node_type(node);
    if (!type || type[0] == '\0' || type[1] != '\0') return false;
    char b = type[0];

    char target = 0;
    bool is_opener = false;
    if (b == '(')      { target = ')'; is_opener = true; }
    else if (b == ')') { target = '('; is_opener = false; }
    else if (b == '[') { target = ']'; is_opener = true; }
    else if (b == ']') { target = '['; is_opener = false; }
    else if (b == '{') { target = '}'; is_opener = true; }
    else if (b == '}') { target = '{'; is_opener = false; }
    else return false;

    TSNode parent = ts_node_parent(node);
    while (!ts_node_is_null(parent)) {
        uint32_t child_count = ts_node_child_count(parent);
        uint32_t node_start = ts_node_start_byte(node);

        if (is_opener) {
            for (int i = (int)child_count - 1; i >= 0; --i) {
                TSNode ch = ts_node_child(parent, (uint32_t)i);
                if (ts_node_start_byte(ch) > node_start) {
                    const char *ctype = ts_node_type(ch);
                    if (ctype && ctype[0] == target && ctype[1] == '\0') {
                        if (out_match) *out_match = (size_t)ts_node_start_byte(ch);
                        return true;
                    }
                }
            }
        } else {
            for (uint32_t i = 0; i < child_count; ++i) {
                TSNode ch = ts_node_child(parent, i);
                if (ts_node_start_byte(ch) < node_start) {
                    const char *ctype = ts_node_type(ch);
                    if (ctype && ctype[0] == target && ctype[1] == '\0') {
                        if (out_match) *out_match = (size_t)ts_node_start_byte(ch);
                        return true;
                    }
                }
            }
        }
        /* Check enclosing parent in case of wrapper grammar rules */
        parent = ts_node_parent(parent);
    }

    return false;
}

