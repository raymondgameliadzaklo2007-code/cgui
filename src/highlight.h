/**
 * highlight.h – Tree-sitter based syntax spans
 *
 * Incremental model:
 *   1. Every buffer mutation calls highlight_edit() with the edited byte
 *      range and row/column points. This applies ts_tree_edit() to the
 *      previous tree, shifts the existing spans and widens a dirty range.
 *   2. Before drawing, highlight_update_input() re-parses through a read
 *      callback (no full-text copy), asks Tree-sitter which ranges changed
 *      and rebuilds spans only inside the changed/edited region.
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
    HL_INCLUDE,
    HL_OPERATOR,
    HL_CONSTANT,
    HL_PROPERTY,
    HL_PUNCTUATION,
    HL_COUNT
} HlKind;

typedef struct HlSpan {
    size_t start;
    size_t end;
    HlKind kind;
} HlSpan;

/** Row / byte-column position (matches TSPoint). */
typedef struct HlPoint {
    uint32_t row;
    uint32_t column;
} HlPoint;

/**
 * Read callback: return a pointer to contiguous UTF-8 text starting at
 * `byte`, and store how many bytes are available there in *len.
 * Return NULL / *len = 0 at end of text.
 */
typedef const char *(*HlReadFn)(void *payload, size_t byte, size_t *len);

typedef struct Highlighter {
    void *parser;     /* TSParser* */
    void *language;   /* const TSLanguage* */
    void *query;      /* TSQuery* */
    void *tree;       /* TSTree* */

    /* Sorted by start, non-overlapping. */
    HlSpan *spans;
    size_t  span_count;
    size_t  span_cap;

    /* Region touched by edits since the last update (current coordinates). */
    bool   has_dirty;
    size_t dirty_lo;
    size_t dirty_hi;
    bool   need_full;  /* no usable tree: rebuild everything */

    bool ready;
} Highlighter;

bool highlight_init(Highlighter *hl);
void highlight_destroy(Highlighter *hl);

/** Forget the tree and spans (e.g. after loading a new file). */
void highlight_reset(Highlighter *hl);

/** Record one edit. Call it for every insertion / deletion, in order. */
void highlight_edit(Highlighter *hl,
                    size_t start_byte, size_t old_end_byte, size_t new_end_byte,
                    HlPoint start_point, HlPoint old_end_point, HlPoint new_end_point);

/** Incremental re-parse via read callback; rebuilds only changed spans. */
bool highlight_update_input(Highlighter *hl, HlReadFn read, void *payload, size_t len);

/** Convenience: re-parse from a contiguous buffer. */
bool highlight_update(Highlighter *hl, const char *text, size_t len);

/** Kind at byte offset, or HL_DEFAULT. O(log n). */
HlKind highlight_kind_at(const Highlighter *hl, size_t offset);

/** Colour for kind (RGBA 0–255). */
void highlight_color(HlKind kind, uint8_t out[4]);

/** Find matching bracket at byte offset using Tree-sitter AST. */
bool highlight_find_matching_bracket(const Highlighter *hl, size_t pos, size_t *out_match);

#endif /* HIGHLIGHT_H */
