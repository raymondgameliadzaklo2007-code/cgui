/**
 * theme.c – UI and syntax color theme engine for cgui
 */

#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Theme g_theme;

bool theme_parse_hex(const char *hex, uint8_t out[4])
{
    if (!hex || !out) return false;
    if (hex[0] == '#') hex++;

    size_t len = strlen(hex);
    unsigned int r = 0, g = 0, b = 0, a = 255;

    if (len == 6) {
        if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) return false;
    } else if (len == 8) {
        if (sscanf(hex, "%02x%02x%02x%02x", &r, &g, &b, &a) != 4) return false;
    } else if (len == 3) {
        if (sscanf(hex, "%1x%1x%1x", &r, &g, &b) != 3) return false;
        r = r * 17; g = g * 17; b = b * 17;
    } else if (len == 4) {
        if (sscanf(hex, "%1x%1x%1x%1x", &r, &g, &b, &a) != 4) return false;
        r = r * 17; g = g * 17; b = b * 17; a = a * 17;
    } else {
        return false;
    }

    out[0] = (uint8_t)r;
    out[1] = (uint8_t)g;
    out[2] = (uint8_t)b;
    out[3] = (uint8_t)a;
    return true;
}

static void set_color(uint8_t dst[4], uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
}

void theme_set_builtin_onedark(Theme *th)
{
    if (!th) return;
    memset(th, 0, sizeof(*th));
    strncpy(th->name, "onedark", sizeof(th->name) - 1);
    th->is_dark = true;

    /* Editor */
    set_color(th->bg, 36, 41, 51, 255);             /* #242933 black */
    set_color(th->fg, 171, 178, 191, 255);          /* #abb2bf white */
    set_color(th->caret, 97, 175, 239, 255);        /* #61afef nord_blue */
    set_color(th->selection, 62, 68, 81, 200);      /* #3e4451 one_bg3 */
    set_color(th->line_highlight, 49, 53, 63, 255); /* #31353f line */
    set_color(th->gutter_bg, 30, 34, 42, 255);      /* #1e222a darker_black */
    set_color(th->gutter_fg, 92, 99, 112, 255);     /* #5c6370 grey_fg */
    set_color(th->bracket_match, 97, 175, 239, 80);

    /* Syntax */
    set_color(th->syntax[HL_DEFAULT],  171, 178, 191, 255); /* white */
    set_color(th->syntax[HL_KEYWORD],  198, 120, 221, 255); /* #c678dd purple */
    set_color(th->syntax[HL_TYPE],     229, 192, 123, 255); /* #e5c07b yellow */
    set_color(th->syntax[HL_FUNCTION], 97,  175, 239, 255); /* #61afef blue */
    set_color(th->syntax[HL_STRING],   152, 195, 121, 255); /* #98c379 green */
    set_color(th->syntax[HL_NUMBER],   209, 154, 102, 255); /* #d19a66 orange */
    set_color(th->syntax[HL_COMMENT],  92,  99,  112, 255); /* #5c6370 grey_fg */
    set_color(th->syntax[HL_PREPROC],  224, 108, 117, 255); /* #e06c75 red */
    set_color(th->syntax[HL_OPERATOR], 86,  182, 194, 255); /* #56b6c2 teal */
    set_color(th->syntax[HL_CONSTANT], 209, 154, 102, 255); /* #d19a66 orange */
    set_color(th->syntax[HL_PROPERTY], 224, 108, 117, 255); /* #e06c75 red */

    /* Tab bar */
    set_color(th->tab_strip_bg,    30, 34, 42, 255);   /* #1e222a darker_black */
    set_color(th->tab_active_bg,   36, 41, 51, 255);   /* #242933 black */
    set_color(th->tab_inactive_bg, 30, 34, 42, 255);
    set_color(th->tab_edge,        49, 53, 63, 255);   /* line */
    set_color(th->tab_text_active, 220, 223, 228, 255);
    set_color(th->tab_text_inactive, 92, 99, 112, 255);

    /* Status bar */
    set_color(th->status_bg,   34, 38, 46, 255);    /* #22262e statusline_bg */
    set_color(th->status_edge, 49, 53, 63, 255);
    set_color(th->status_fg,   171, 178, 191, 255);

    /* Sidebar */
    set_color(th->sidebar_bg,       30, 34, 42, 255); /* darker_black */
    set_color(th->sidebar_edge,     49, 53, 63, 255);
    set_color(th->sidebar_text,     171, 178, 191, 255);
    set_color(th->sidebar_heading,  92, 99, 112, 255);
    set_color(th->sidebar_muted,    92, 99, 112, 255);
    set_color(th->sidebar_hover,    40, 44, 52, 180);
    set_color(th->sidebar_selected, 53, 59, 69, 255);

    /* Overlays */
    set_color(th->overlay_bg,   30, 34, 42, 255);
    set_color(th->overlay_edge, 49, 53, 63, 255);
    set_color(th->overlay_sel,  53, 59, 69, 255);
}

void theme_set_builtin_light(Theme *th)
{
    if (!th) return;
    memset(th, 0, sizeof(*th));
    strncpy(th->name, "light", sizeof(th->name) - 1);
    th->is_dark = false;

    /* Editor */
    set_color(th->bg, 252, 252, 252, 255);
    set_color(th->fg, 30, 30, 30, 255);
    set_color(th->caret, 0, 122, 255, 255);
    set_color(th->selection, 180, 210, 255, 180);
    set_color(th->line_highlight, 240, 243, 248, 255);
    set_color(th->gutter_bg, 245, 245, 247, 255);
    set_color(th->gutter_fg, 140, 140, 145, 255);
    set_color(th->bracket_match, 180, 215, 255, 175);

    /* Syntax */
    set_color(th->syntax[HL_DEFAULT],  30, 30, 30, 255);
    set_color(th->syntax[HL_KEYWORD],  170, 13, 145, 255);
    set_color(th->syntax[HL_TYPE],     0, 116, 189, 255);
    set_color(th->syntax[HL_FUNCTION], 50, 100, 180, 255);
    set_color(th->syntax[HL_STRING],   196, 26, 22, 255);
    set_color(th->syntax[HL_NUMBER],   28, 0, 207, 255);
    set_color(th->syntax[HL_COMMENT],  80, 140, 90, 255);
    set_color(th->syntax[HL_PREPROC],  100, 56, 32, 255);
    set_color(th->syntax[HL_OPERATOR], 50, 50, 50, 255);
    set_color(th->syntax[HL_CONSTANT], 28, 0, 207, 255);
    set_color(th->syntax[HL_PROPERTY], 50, 120, 100, 255);

    /* Chrome */
    set_color(th->tab_strip_bg,    232, 232, 232, 255);
    set_color(th->tab_active_bg,   252, 252, 252, 255);
    set_color(th->tab_inactive_bg, 232, 232, 232, 255);
    set_color(th->tab_edge,        198, 198, 198, 255);
    set_color(th->tab_text_active, 0, 0, 0, 255);
    set_color(th->tab_text_inactive, 96, 96, 96, 255);

    set_color(th->status_bg,   246, 246, 248, 255);
    set_color(th->status_edge, 210, 210, 215, 255);
    set_color(th->status_fg,   90, 90, 98, 255);

    set_color(th->sidebar_bg,       240, 240, 242, 255);
    set_color(th->sidebar_edge,     215, 215, 218, 255);
    set_color(th->sidebar_text,     54, 55, 59, 255);
    set_color(th->sidebar_heading,  125, 127, 132, 255);
    set_color(th->sidebar_muted,    125, 127, 132, 255);
    set_color(th->sidebar_hover,    70, 75, 84, 16);
    set_color(th->sidebar_selected, 61, 133, 220, 32);

    set_color(th->overlay_bg,   252, 252, 252, 255);
    set_color(th->overlay_edge, 200, 200, 205, 255);
    set_color(th->overlay_sel,  220, 235, 252, 255);
}

void theme_init(void)
{
    theme_set_builtin_onedark(&g_theme);
}

const Theme *theme_current(void)
{
    return &g_theme;
}

void theme_color_syntax(HlKind kind, uint8_t out[4])
{
    if (kind < HL_COUNT) {
        out[0] = g_theme.syntax[kind][0];
        out[1] = g_theme.syntax[kind][1];
        out[2] = g_theme.syntax[kind][2];
        out[3] = g_theme.syntax[kind][3];
    } else {
        out[0] = g_theme.fg[0];
        out[1] = g_theme.fg[1];
        out[2] = g_theme.fg[2];
        out[3] = g_theme.fg[3];
    }
}
