/**
 * theme.h – UI and syntax color theme engine for cgui
 *
 * Supports NvChad base46 themes (reading base_30 and base_16 tables)
 * with a built-in compiled C fallback.
 */

#ifndef CGUI_THEME_H
#define CGUI_THEME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "highlight.h"

typedef struct Theme {
    char name[64];
    bool is_dark;

    /* Editor surface */
    uint8_t bg[4];
    uint8_t fg[4];
    uint8_t caret[4];
    uint8_t selection[4];
    uint8_t line_highlight[4];
    uint8_t gutter_bg[4];
    uint8_t gutter_fg[4];
    uint8_t bracket_match[4];
    uint8_t indent_guide[4];

    /* Tree-sitter syntax tokens (indexed by HlKind) */
    uint8_t syntax[HL_COUNT][4];

    /* Tab bar */
    uint8_t tab_strip_bg[4];
    uint8_t tab_active_bg[4];
    uint8_t tab_inactive_bg[4];
    uint8_t tab_edge[4];
    uint8_t tab_text_active[4];
    uint8_t tab_text_inactive[4];

    /* Status bar */
    uint8_t status_bg[4];
    uint8_t status_edge[4];
    uint8_t status_fg[4];

    /* Sidebar / Project view */
    uint8_t sidebar_bg[4];
    uint8_t sidebar_edge[4];
    uint8_t sidebar_text[4];
    uint8_t sidebar_heading[4];
    uint8_t sidebar_muted[4];
    uint8_t sidebar_hover[4];
    uint8_t sidebar_selected[4];

    /* Overlays (Palette, Find bar, etc.) */
    uint8_t overlay_bg[4];
    uint8_t overlay_edge[4];
    uint8_t overlay_sel[4];
} Theme;

/* Global current theme instance */
extern Theme g_theme;

/* Initialize theme system with default built-in theme (One Dark) */
void theme_init(void);

/* Get active theme */
const Theme *theme_current(void);

/* Look up syntax color for HlKind */
void theme_color_syntax(HlKind kind, uint8_t out[4]);

/* Parse hex color string (#RGB, #RGBA, #RRGGBB, #RRGGBBAA) */
bool theme_parse_hex(const char *hex, uint8_t out[4]);

/* Built-in fallback themes */
void theme_set_builtin_onedark(Theme *th);
void theme_set_builtin_light(Theme *th);

#endif /* CGUI_THEME_H */
