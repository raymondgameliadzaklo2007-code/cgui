/**
 * highlight.h – Tree-sitter based syntax spans
 */

#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum HlKind {
    HL_DEFAULT = 0,
    HL_KEYWORD,
    HL_TYPE,
    HL_FUNCTION,
    HL_STRING,
    HL_NUMBER,
    HL_COMMENT,
    HL_PREPROC,
    HL_OPERATOR,
    HL_CONSTANT,
    HL_PROPERTY,
    HL_COUNT
} HlKind;

typedef struct HlSpan {
    size_t start;
    size_t end;
    HlKind kind;
} HlSpan;

typedef struct Highlighter {
    void *parser;     /* TSParser* */
    void *language;   /* const TSLanguage* */
    void *query;      /* TSQuery* */
    void *tree;       /* TSTree* */

    HlSpan *spans;
    size_t  span_count;
    size_t  span_cap;

    bool ready;
} Highlighter;

bool highlight_init(Highlighter *hl);
void highlight_destroy(Highlighter *hl);

/** Reparse full buffer text (UTF-8). Builds ordered non-overlapping spans. */
bool highlight_update(Highlighter *hl, const char *text, size_t len);

/** Kind at byte offset, or HL_DEFAULT. */
HlKind highlight_kind_at(const Highlighter *hl, size_t offset);

/** Colour for kind (RGBA 0–255). */
void highlight_color(HlKind kind, uint8_t out[4]);

#endif /* HIGHLIGHT_H */
