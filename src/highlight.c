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

/* Comprehensive C Tree-sitter query rules matching Neovim / NvChad captures.
 * Each rule is verified against the runtime language grammar before being included,
 * ensuring highlighting never breaks if a grammar version omits a specific node or token.
 */
static const char *QUERY_RULES[] = {
    /* Statement & control flow keywords */
    "\"break\" @keyword\n",
    "\"case\" @keyword\n",
    "\"continue\" @keyword\n",
    "\"default\" @keyword\n",
    "\"do\" @keyword\n",
    "\"else\" @keyword\n",
    "\"for\" @keyword\n",
    "\"goto\" @keyword\n",
    "\"if\" @keyword\n",
    "\"return\" @keyword\n",
    "\"switch\" @keyword\n",
    "\"while\" @keyword\n",
    "\"sizeof\" @keyword\n",

    /* Storage and type modifier keywords */
    "\"const\" @keyword\n",
    "\"static\" @keyword\n",
    "\"extern\" @keyword\n",
    "\"inline\" @keyword\n",
    "\"volatile\" @keyword\n",
    "\"register\" @keyword\n",
    "\"auto\" @keyword\n",
    "\"restrict\" @keyword\n",
    "\"struct\" @keyword\n",
    "\"union\" @keyword\n",
    "\"enum\" @keyword\n",
    "\"typedef\" @keyword\n",

    /* Types */
    "(primitive_type) @type\n",
    "(type_identifier) @type\n",
    "(sized_type_specifier) @type\n",

    /* Literals */
    "(string_literal) @string\n",
    "(system_lib_string) @string\n",
    "(char_literal) @string\n",
    "(number_literal) @number\n",
    "(comment) @comment\n",

    /* Preprocessor directives & macros (Helix & NvChad style) */
    "\"#include\" @include\n",
    "\"#define\" @keyword.directive\n",
    "\"#ifdef\" @keyword.directive\n",
    "\"#ifndef\" @keyword.directive\n",
    "\"#if\" @keyword.directive\n",
    "\"#else\" @keyword.directive\n",
    "\"#elif\" @keyword.directive\n",
    "\"#endif\" @keyword.directive\n",
    "\"#undef\" @keyword.directive\n",
    "\"#pragma\" @keyword.directive\n",
    "\"#error\" @keyword.directive\n",
    "\"#warning\" @keyword.directive\n",
    "(preproc_directive) @keyword.directive\n",
    "(preproc_arg) @preproc\n",
    "(preproc_def name: (identifier) @constant)\n",
    "(preproc_function_def name: (identifier) @function)\n",
    "(preproc_call directive: (preproc_directive) @keyword.directive)\n",

    /* Functions & Calls */
    "(call_expression function: (identifier) @function)\n",
    "(call_expression function: (field_expression field: (field_identifier) @function))\n",
    "(function_declarator declarator: (identifier) @function)\n",
    "(function_declarator declarator: (pointer_declarator declarator: (identifier) @function))\n",
    "(function_declarator declarator: (pointer_declarator (identifier) @function))\n",

    /* Struct / Union fields */
    "(field_identifier) @property\n",

    /* Enum constants */
    "(enumerator name: (identifier) @constant)\n",

    /* Builtin constants (NULL, true, false, nullptr filtered via is_constant_builtin) */
    "(identifier) @constant_builtin\n",

    /* Operators */
    "\"->\" @operator\n",
    "\"++\" @operator\n",
    "\"--\" @operator\n",
    "\"==\" @operator\n",
    "\"!=\" @operator\n",
    "\"<=\" @operator\n",
    "\">=\" @operator\n",
    "\"&&\" @operator\n",
    "\"||\" @operator\n",
    "\"<<\" @operator\n",
    "\">>\" @operator\n",
    "\"+=\" @operator\n",
    "\"-=\" @operator\n",
    "\"*=\" @operator\n",
    "\"/=\" @operator\n",
    "\"%=\" @operator\n",
    "\"&=\" @operator\n",
    "\"|=\" @operator\n",
    "\"^=\" @operator\n",
    "\"+\" @operator\n",
    "\"-\" @operator\n",
    "\"*\" @operator\n",
    "\"/\" @operator\n",
    "\"%\" @operator\n",
    "\"=\" @operator\n",
    "\"<\" @operator\n",
    "\">\" @operator\n",
    "\"!\" @operator\n",
    "\"&\" @operator\n",
    "\"|\" @operator\n",
    "\"^\" @operator\n",
    "\"~\" @operator\n",
    "\"?\" @operator\n",
    "\":\" @operator\n",
    "\".\" @operator\n",
    NULL
};

static HlKind capture_to_kind(const char *name, uint32_t len)
{
    if (len >= 7 && !strncmp(name, "include", 7)) return HL_INCLUDE;
    if (len >= 14 && !strncmp(name, "keyword.import", 14)) return HL_INCLUDE;
    if (len >= 17 && !strncmp(name, "keyword.directive", 17)) return HL_PREPROC;
    if (len >= 7 && !strncmp(name, "preproc", 7)) return HL_PREPROC;
    if (len >= 6 && !strncmp(name, "define", 6)) return HL_PREPROC;
    if (len >= 7 && !strncmp(name, "keyword", 7)) return HL_KEYWORD;
    if (len >= 4 && !strncmp(name, "type", 4)) return HL_TYPE;
    if (len >= 8 && !strncmp(name, "function", 8)) return HL_FUNCTION;
    if (len >= 6 && !strncmp(name, "string", 6)) return HL_STRING;
    if (len >= 16 && !strncmp(name, "constant.numeric", 16)) return HL_NUMBER;
    if (len >= 6 && !strncmp(name, "number", 6)) return HL_NUMBER;
    if (len >= 7 && !strncmp(name, "comment", 7)) return HL_COMMENT;
    if (len >= 8 && !strncmp(name, "operator", 8)) return HL_OPERATOR;
    if (len >= 8 && !strncmp(name, "constant", 8)) return HL_CONSTANT;
    if (len >= 8 && !strncmp(name, "property", 8)) return HL_PROPERTY;
    if (len >= 21 && !strncmp(name, "variable.other.member", 21)) return HL_PROPERTY;
    return HL_DEFAULT;
}

static char *load_scm_file(const char *path, size_t *out_len)
{
    if (!path) return NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz <= 0 || sz > 512 * 1024) { fclose(fp); return NULL; }
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t nr = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[nr] = '\0';
    if (out_len) *out_len = nr;
    return buf;
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
    int rules_accepted = 0;
    TSQuery *query = NULL;

    /* 1. Try loading external Helix / Neovim .scm query file from disk */
    const char *scm_paths[] = {
#ifdef CGUI_DEFAULT_QUERIES_PATH
        CGUI_DEFAULT_QUERIES_PATH "/c/highlights.scm",
#endif
        "assets/queries/c/highlights.scm",
        "queries/c/highlights.scm",
        NULL
    };

    for (size_t i = 0; scm_paths[i] != NULL; ++i) {
        size_t slen = 0;
        char *stext = load_scm_file(scm_paths[i], &slen);
        if (stext) {
            query = ts_query_new(lang, stext, (uint32_t)slen, &err_offset, &err);
            if (query) {
                printf("Tree-sitter C query loaded from %s\n", scm_paths[i]);
                free(stext);
                break;
            }
            free(stext);
        }
    }

    /* 2. Fall back to validated built-in rule concatenation if .scm was not loaded */
    if (!query) {
        size_t cap = 8192;
        char *query_buf = malloc(cap);
        if (!query_buf) {
            ts_parser_delete(parser);
            return false;
        }
        query_buf[0] = '\0';
        size_t query_len = 0;

        for (size_t i = 0; QUERY_RULES[i] != NULL; ++i) {
            const char *rule = QUERY_RULES[i];
            uint32_t test_off = 0;
            TSQueryError test_err = TSQueryErrorNone;
            TSQuery *test_q = ts_query_new(lang, rule, (uint32_t)strlen(rule), &test_off, &test_err);
            if (test_q) {
                ts_query_delete(test_q);
                size_t rlen = strlen(rule);
                if (query_len + rlen + 1 >= cap) {
                    cap *= 2;
                    char *nb = realloc(query_buf, cap);
                    if (!nb) break;
                    query_buf = nb;
                }
                memcpy(query_buf + query_len, rule, rlen);
                query_len += rlen;
                query_buf[query_len] = '\0';
                rules_accepted++;
            }
        }

        if (query_len > 0) {
            query = ts_query_new(lang, query_buf, (uint32_t)query_len, &err_offset, &err);
        }
        free(query_buf);
    }

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
    if (rules_accepted > 0) {
        printf("Tree-sitter C highlighter ready (%d rules active)\n", rules_accepted);
    } else {
        printf("Tree-sitter C highlighter ready\n");
    }
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

static bool is_constant_builtin(HlReadFn read, void *payload, size_t s, size_t e)
{
    size_t len = e - s;
    if (len < 4 || len > 7 || !read) return false;
    char buf[8];
    for (size_t i = 0; i < len; ++i) {
        size_t n = 0;
        const char *p = read(payload, s + i, &n);
        if (!p || n == 0) return false;
        buf[i] = p[0];
    }
    buf[len] = '\0';
    if (len == 4 && (strcmp(buf, "NULL") == 0 || strcmp(buf, "true") == 0)) return true;
    if (len == 5 && strcmp(buf, "false") == 0) return true;
    if (len == 7 && strcmp(buf, "nullptr") == 0) return true;
    return false;
}

/* Ensure preprocessor directive commands (#include, #define, #ifdef, etc.) are always highlighted */
static void scan_preprocs(TSNode node, size_t lo, size_t hi, bool full,
                          HlReadFn read, void *payload,
                          HlSpan **out, size_t *count, size_t *cap)
{
    const char *type = ts_node_type(node);
    if (type && !strncmp(type, "preproc_", 8)) {
        size_t s = ts_node_start_byte(node);
        size_t e = ts_node_end_byte(node);
        if (full || (s < hi && e > lo)) {
            size_t p = s;
            if (byte_at(read, payload, p) == '#') {
                p++;
                while (p < e && (byte_at(read, payload, p) == ' ' || byte_at(read, payload, p) == '\t'))
                    p++;
                size_t word_start = p;
                while (p < e) {
                    char ch = byte_at(read, payload, p);
                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
                        p++;
                    else
                        break;
                }
                if (p > s) {
                    bool is_inc = false;
                    if (p - word_start == 7) {
                        char w[8];
                        for (size_t k = 0; k < 7; ++k) w[k] = byte_at(read, payload, word_start + k);
                        w[7] = '\0';
                        if (!strcmp(w, "include")) is_inc = true;
                    }
                    span_push(out, count, cap, (HlSpan){ .start = s, .end = p, .kind = is_inc ? HL_INCLUDE : HL_PREPROC });
                }
            }
        }
    }
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; ++i) {
        TSNode ch = ts_node_child(node, i);
        size_t cs = ts_node_start_byte(ch);
        size_t ce = ts_node_end_byte(ch);
        if (!full && (ce <= lo || cs >= hi)) continue;
        scan_preprocs(ch, lo, hi, full, read, payload, out, count, cap);
    }
}

/* Collect captures intersecting [lo, hi) (whole file when full). */
static void collect(Highlighter *hl, TSTree *tree, bool full, size_t lo, size_t hi,
                    HlReadFn read, void *payload,
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
            size_t s = ts_node_start_byte(cap_.node);
            size_t e = ts_node_end_byte(cap_.node);
            if (e <= s) continue;
            if (!full && !(s < hi && e > lo)) continue;

            HlKind kind = HL_DEFAULT;
            if (nlen == 16 && !strncmp(name, "constant_builtin", 16)) {
                if (is_constant_builtin(read, payload, s, e)) {
                    kind = HL_CONSTANT;
                }
            } else {
                kind = capture_to_kind(name, nlen);
            }
            if (kind == HL_DEFAULT) continue;
            span_push(out, count, cap, (HlSpan){ .start = s, .end = e, .kind = kind });
        }
    }
    ts_query_cursor_delete(cursor);

    /* Guarantee all preprocessor command tokens (#include, #define, #ifdef, etc.) are highlighted */
    scan_preprocs(ts_tree_root_node(tree), lo, hi, full, read, payload, out, count, cap);
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
        collect(hl, tree, true, 0, len, read, payload, &hl->spans, &hl->span_count, &hl->span_cap);
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
        collect(hl, tree, false, lo, hi, read, payload, &fresh, &fcount, &fcap);
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

