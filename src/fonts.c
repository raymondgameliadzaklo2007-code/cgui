/**
 * fonts.c – Enumerate installed fonts
 */

#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __has_include
#  if __has_include(<fontconfig/fontconfig.h>)
#    define CGUI_HAS_FONTCONFIG 1
#    include <fontconfig/fontconfig.h>
#  endif
#endif

#ifndef CGUI_HAS_FONTCONFIG
/* Try classic include */
#  include <fontconfig/fontconfig.h>
#  define CGUI_HAS_FONTCONFIG 1
#endif

void font_list_init(FontList *list)
{
    memset(list, 0, sizeof(*list));
}

void font_list_free(FontList *list)
{
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].family);
        free(list->items[i].path);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}


static char *cgui_strcasestr(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return (char *)hay;
    size_t n = strlen(needle);
    for (const char *p = hay; *p; ++p) {
        size_t i = 0;
        while (i < n) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (!a || a != b) break;
            i++;
        }
        if (i == n) return (char *)p;
    }
    return NULL;
}

static bool ends_with_ci(const char *s, const char *suf)
{
    size_t n = strlen(s), m = strlen(suf);
    if (n < m) return false;
    return strcasecmp(s + n - m, suf) == 0;
}

static bool path_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool style_is_non_regular(const char *s)
{
    if (!s) return false;
    if (cgui_strcasestr(s, "regular") || cgui_strcasestr(s, "roman"))
        return false;
    return cgui_strcasestr(s, "bold") || cgui_strcasestr(s, "italic") ||
           cgui_strcasestr(s, "oblique") || cgui_strcasestr(s, "black") ||
           cgui_strcasestr(s, "heavy") || cgui_strcasestr(s, "light") ||
           cgui_strcasestr(s, "thin") || cgui_strcasestr(s, "medium") ||
           cgui_strcasestr(s, "semibold") || cgui_strcasestr(s, "extrabold") ||
           cgui_strcasestr(s, "extralight") || cgui_strcasestr(s, "condensed");
}

static bool looks_regular(const char *name, const char *path)
{
    /* Check path AND family – fontconfig family often omits "Italic" */
    if (style_is_non_regular(path) || style_is_non_regular(name))
        return false;
    return true;
}

static void push_font(FontList *list, const char *family, const char *path, bool mono)
{
    if (!path || !path_exists(path)) return;

    for (size_t i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].path, path) == 0) return;
    }

    if (list->count >= list->capacity) {
        size_t nc = list->capacity ? list->capacity * 2 : 64;
        FontEntry *ni = realloc(list->items, nc * sizeof(FontEntry));
        if (!ni) return;
        list->items = ni;
        list->capacity = nc;
    }

    FontEntry *e = &list->items[list->count++];
    e->family = strdup(family ? family : path);
    e->path = strdup(path);
    e->monospace = mono;
    e->regular = looks_regular(family, path);
}

static int mono_sort(const void *a, const void *b)
{
    const FontEntry *fa = a, *fb = b;
    if (fa->monospace != fb->monospace) return (int)fb->monospace - (int)fa->monospace;
    if (fa->regular != fb->regular) return (int)fb->regular - (int)fa->regular;
    return strcasecmp(fa->family, fb->family);
}

static void scan_dir(FontList *list, const char *dir, int depth)
{
    if (depth > 4 || !dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(list, path, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (ends_with_ci(ent->d_name, ".ttf") || ends_with_ci(ent->d_name, ".otf")) {
                bool mono = cgui_strcasestr(ent->d_name, "mono") != NULL
                         || cgui_strcasestr(ent->d_name, "consola") != NULL
                         || cgui_strcasestr(ent->d_name, "courier") != NULL
                         || cgui_strcasestr(ent->d_name, "menlo") != NULL
                         || cgui_strcasestr(ent->d_name, "jetbrains") != NULL
                         || cgui_strcasestr(ent->d_name, "iosevka") != NULL
                         || cgui_strcasestr(ent->d_name, "source code") != NULL
                         || cgui_strcasestr(ent->d_name, "hack") != NULL
                         || cgui_strcasestr(ent->d_name, "fira code") != NULL
                         || cgui_strcasestr(ent->d_name, "plex mono") != NULL;
                /* family from filename without extension */
                char family[256];
                snprintf(family, sizeof(family), "%s", ent->d_name);
                char *dot = strrchr(family, '.');
                if (dot) *dot = '\0';
                push_font(list, family, path, mono);
            }
        }
    }
    closedir(d);
}

static void scan_fontconfig(FontList *list)
{
#ifdef CGUI_HAS_FONTCONFIG
    if (!FcInit()) return;

    FcPattern *pat = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_SPACING, (char *)0);
    FcFontSet *fs = FcFontList(NULL, pat, os);

    if (fs) {
        for (int i = 0; i < fs->nfont; ++i) {
            FcChar8 *file = NULL, *family = NULL;
            int spacing = FC_PROPORTIONAL;
            FcPatternGetString(fs->fonts[i], FC_FILE, 0, &file);
            FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family);
            FcPatternGetInteger(fs->fonts[i], FC_SPACING, 0, &spacing);
            if (!file) continue;
            /* Only scalable outline fonts we can load with stb */
            const char *path = (const char *)file;
            if (!(ends_with_ci(path, ".ttf") || ends_with_ci(path, ".otf"))) continue;
            bool mono = (spacing == FC_MONO);
            if (!mono && family) {
                mono = cgui_strcasestr((const char *)family, "Mono") != NULL;
            }
            push_font(list, family ? (const char *)family : path, path, mono);
        }
        FcFontSetDestroy(fs);
    }
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
#else
    (void)list;
#endif
}

bool font_list_scan(FontList *list, bool sort_mono_first)
{
    font_list_free(list);
    font_list_init(list);

    scan_fontconfig(list);

    /* Always also walk common dirs (covers broken fontconfig / flatpak etc.) */
    const char *dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/opentype",
        NULL,
    };
    for (int i = 0; dirs[i]; ++i) scan_dir(list, dirs[i], 0);

    char *home = getenv("HOME");
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.local/share/fonts", home);
        scan_dir(list, path, 0);
        snprintf(path, sizeof(path), "%s/.fonts", home);
        scan_dir(list, path, 0);
    }

    if (sort_mono_first && list->count > 1) {
        qsort(list->items, list->count, sizeof(FontEntry), mono_sort);
    }

    size_t mono = 0;
    for (size_t i = 0; i < list->count; ++i)
        if (list->items[i].monospace) mono++;
    printf("Font scan: %zu fonts (%zu monospace)\n", list->count, mono);
    return list->count > 0;
}

const char *font_list_default_mono(const FontList *list)
{
    if (!list || list->count == 0) return NULL;
    /* 1) mono + regular */
    for (size_t i = 0; i < list->count; ++i)
        if (list->items[i].monospace && list->items[i].regular)
            return list->items[i].path;
    /* 2) any mono */
    for (size_t i = 0; i < list->count; ++i)
        if (list->items[i].monospace)
            return list->items[i].path;
    /* 3) any regular */
    for (size_t i = 0; i < list->count; ++i)
        if (list->items[i].regular)
            return list->items[i].path;
    return list->items[0].path;
}
