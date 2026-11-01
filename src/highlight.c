/**
 * highlight.c – Tree-sitter C highlighting
 */

#include "highlight.h"

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

static int span_cmp(const void *a, const void *b)
{
    const HlSpan *sa = a, *sb = b;
    if (sa->start < sb->start) return -1;
    if (sa->start > sb->start) return 1;
    if (sa->end > sb->end) return -1;
    if (sa->end < sb->end) return 1;
    return 0;
}

bool highlight_update(Highlighter *hl, const char *text, size_t len)
{
    if (!hl || !hl->ready) return false;
    if (!text) text = "";

    TSParser *parser = hl->parser;
    TSTree *old = hl->tree;
    TSTree *tree = ts_parser_parse_string(parser, old, text, (uint32_t)len);
    if (old) ts_tree_delete(old);
    hl->tree = tree;
    if (!tree) return false;

    hl->span_count = 0;

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, (TSQuery *)hl->query, ts_tree_root_node(tree));

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            TSQueryCapture cap = match.captures[i];
            uint32_t nlen = 0;
            const char *name = ts_query_capture_name_for_id((TSQuery *)hl->query, cap.index, &nlen);
            HlKind kind = capture_to_kind(name, nlen);
            if (kind == HL_DEFAULT) continue;

            uint32_t start = ts_node_start_byte(cap.node);
            uint32_t end = ts_node_end_byte(cap.node);
            if (end <= start) continue;

            if (hl->span_count >= hl->span_cap) {
                size_t nc = hl->span_cap ? hl->span_cap * 2 : 256;
                HlSpan *ns = realloc(hl->spans, nc * sizeof(HlSpan));
                if (!ns) continue;
                hl->spans = ns;
                hl->span_cap = nc;
            }
            hl->spans[hl->span_count++] = (HlSpan){
                .start = start,
                .end = end,
                .kind = kind,
            };
        }
    }
    ts_query_cursor_delete(cursor);

    if (hl->span_count > 1)
        qsort(hl->spans, hl->span_count, sizeof(HlSpan), span_cmp);

    return true;
}

HlKind highlight_kind_at(const Highlighter *hl, size_t offset)
{
    if (!hl || !hl->spans || hl->span_count == 0) return HL_DEFAULT;

    HlKind kind = HL_DEFAULT;
    size_t best_len = 0;
    for (size_t i = 0; i < hl->span_count; ++i) {
        const HlSpan *s = &hl->spans[i];
        if (offset < s->start) break;
        if (offset >= s->end) continue;
        size_t len = s->end - s->start;
        /* Prefer longer spans so block comments win over any inner leftovers */
        if (len >= best_len) {
            best_len = len;
            kind = s->kind;
        }
    }
    return kind;
}

void highlight_color(HlKind kind, uint8_t out[4])
{
    switch (kind) {
    case HL_KEYWORD:   out[0]=170; out[1]=13;  out[2]=145; out[3]=255; break;
    case HL_TYPE:      out[0]=0;   out[1]=116; out[2]=189; out[3]=255; break;
    case HL_FUNCTION:  out[0]=50;  out[1]=100; out[2]=180; out[3]=255; break;
    case HL_STRING:    out[0]=196; out[1]=26;  out[2]=22;  out[3]=255; break;
    case HL_NUMBER:    out[0]=28;  out[1]=0;   out[2]=207; out[3]=255; break;
    case HL_COMMENT:   out[0]=80;  out[1]=140; out[2]=90;  out[3]=255; break;
    case HL_PREPROC:   out[0]=100; out[1]=56;  out[2]=32;  out[3]=255; break;
    case HL_OPERATOR:  out[0]=50;  out[1]=50;  out[2]=50;  out[3]=255; break;
    case HL_CONSTANT:  out[0]=28;  out[1]=0;   out[2]=207; out[3]=255; break;
    case HL_PROPERTY:  out[0]=50;  out[1]=120; out[2]=100; out[3]=255; break;
    default:           out[0]=30;  out[1]=30;  out[2]=30;  out[3]=255; break;
    }
}
