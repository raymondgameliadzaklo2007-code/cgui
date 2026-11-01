/**
 * fonts.h – System font discovery
 */

#ifndef FONTS_H
#define FONTS_H

#include <stddef.h>
#include <stdbool.h>

typedef struct FontEntry {
    char *family;   /* display name */
    char *path;     /* file path */
    bool  monospace;
    bool  regular;  /* preferred default face */
} FontEntry;

typedef struct FontList {
    FontEntry *items;
    size_t     count;
    size_t     capacity;
} FontList;

void font_list_init(FontList *list);
void font_list_free(FontList *list);

bool font_list_scan(FontList *list, bool sort_mono_first);

/** Prefer monospace + Regular; never returns NULL if list non-empty. */
const char *font_list_default_mono(const FontList *list);

#endif /* FONTS_H */
