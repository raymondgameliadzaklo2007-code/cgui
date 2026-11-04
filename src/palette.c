/**
 * palette.c – command palette (Ctrl+Shift+P)
 *
 * The command list itself (app_commands) lives in main.c next to the
 * commands it runs.
 */

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "palette.h"

static char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Subsequence fuzzy match. Returns -1 on miss, higher is better. Rewards
 * consecutive hits and hits at word starts so "ff" ranks "File: Find" over
 * "Font Size". */
static int fuzzy_score(const char *query, const char *text)
{
    if (!query[0]) return 0;
    int score = 0, streak = 0;
    const char *q = query;
    for (const char *t = text; *t && *q; ++t) {
        if (ascii_lower(*t) == ascii_lower(*q)) {
            bool word_start = t == text || t[-1] == ' ' || t[-1] == ':';
            streak++;
            score += 1 + streak * 2 + (word_start ? 6 : 0);
            q++;
        } else {
            streak = 0;
        }
    }
    return *q ? -1 : score;
}

static void palette_filter(App *app)
{
    int scores[PALETTE_MAX_MATCHES];
    app->palette_match_count = 0;
    for (int i = 0; i < app_command_count; ++i) {
        int s = fuzzy_score(app->palette_query, app_commands[i].label);
        if (s < 0) continue;
        /* insertion sort by score desc, stable for ties; when the list is
         * full the weakest match is dropped */
        int at = app->palette_match_count;
        if (at == PALETTE_MAX_MATCHES) {
            if (scores[at - 1] >= s) continue;
            at--;
        } else {
            app->palette_match_count++;
        }
        while (at > 0 && scores[at - 1] < s) {
            scores[at] = scores[at - 1];
            app->palette_matches[at] = app->palette_matches[at - 1];
            at--;
        }
        scores[at] = s;
        app->palette_matches[at] = i;
    }
    app->palette_sel = 0;
    app->palette_scroll = 0;
}

void palette_show(App *app)
{
    close_overlays(app);
    app->palette_open = true;
    app->palette_len = 0;
    app->palette_query[0] = 0;
    palette_filter(app);
}

static void palette_run_selected(App *app)
{
    if (app->palette_sel < 0 || app->palette_sel >= app->palette_match_count) return;
    const Command *cmd = &app_commands[app->palette_matches[app->palette_sel]];
    app->palette_open = false;
    cmd->run(app);
}

typedef struct PaletteLayout {
    float x, y, w, h;
    float field_h;
    float list_top;
    float row_h;
    int   visible;
} PaletteLayout;

static PaletteLayout palette_layout(const App *app)
{
    PaletteLayout l;
    float ww = (float)app->vk.swapchain_extent.width;
    float wh = (float)app->vk.swapchain_extent.height;
    l.w = ww - 80.0f < 620.0f ? ww - 80.0f : 620.0f;
    l.x = (float)(int)((ww - l.w) * 0.5f);
    l.y = 28.0f + 40.0f;
    l.field_h = app->font.line_height + 18.0f;
    l.row_h = app->font.line_height + 10.0f;
    l.list_top = l.y + l.field_h + 5.0f;
    int max_rows = (int)((wh - l.list_top - 60.0f) / l.row_h);
    if (max_rows > 10) max_rows = 10;
    if (max_rows < 1) max_rows = 1;
    int rows = app->palette_match_count < max_rows ? app->palette_match_count : max_rows;
    l.visible = max_rows;
    l.h = l.field_h + 5.0f + (rows > 0 ? rows : 1) * l.row_h + 6.0f;
    return l;
}

void draw_palette(App *app, VkContext *ctx)
{
    PaletteLayout l = palette_layout(app);
    uint8_t scrim[4]    = { 20, 22, 26, 40 };
    uint8_t border[4]   = { 205, 207, 212, 255 };
    uint8_t panel[4]    = { 252, 252, 253, 255 };
    uint8_t divider[4]  = { 228, 229, 232, 255 };
    uint8_t text_c[4]   = { 36, 37, 41, 255 };
    uint8_t dim[4]      = { 128, 130, 136, 255 };
    uint8_t sel_bg[4]   = { 61, 133, 220, 34 };
    uint8_t hover_bg[4] = { 70, 75, 84, 14 };
    uint8_t accent[4]   = { 31, 94, 164, 255 };
    uint8_t key_bg[4]   = { 70, 75, 84, 18 };

    float ww = (float)ctx->swapchain_extent.width;
    float wh = (float)ctx->swapchain_extent.height;
    vk_draw_rounded_rect(&app->draw, 0, 0, ww, wh, 0.0f, 1.0f, scrim);
    vk_draw_rounded_rect(&app->draw, l.x - 1, l.y - 1, l.w + 2, l.h + 2, 9.0f, 1.0f, border);
    vk_draw_rounded_rect(&app->draw, l.x, l.y, l.w, l.h, 8.0f, 1.0f, panel);

    float field_baseline = l.y + (l.field_h + app->font.ascent + app->font.descent) * 0.5f;
    const char *shown = app->palette_len > 0 ? app->palette_query : "Type a command…";
    font_draw(&app->font, ctx, l.x + 14.0f, field_baseline, shown,
              app->palette_len > 0 ? text_c : dim);
    float caret_x = l.x + 14.0f + (app->palette_len > 0
                                   ? font_measure(&app->font, app->palette_query) + 1.0f
                                   : 0.0f);
    vk_draw_rounded_rect(&app->draw, caret_x, l.y + 9.0f, 2.0f, l.field_h - 18.0f,
                         1.0f, 1.0f, accent);
    vk_draw_rounded_rect(&app->draw, l.x, l.y + l.field_h, l.w, 1.0f, 0.0f, 1.0f, divider);

    if (app->palette_sel < app->palette_scroll)
        app->palette_scroll = app->palette_sel;
    if (app->palette_sel >= app->palette_scroll + l.visible)
        app->palette_scroll = app->palette_sel - l.visible + 1;

    if (app->palette_match_count == 0) {
        float b = l.list_top + (l.row_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, l.x + 14.0f, b, "No matching commands", dim);
        return;
    }

    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    for (int row = 0; row < l.visible; ++row) {
        int idx = app->palette_scroll + row;
        if (idx >= app->palette_match_count) break;
        const Command *cmd = &app_commands[app->palette_matches[idx]];
        float ry = l.list_top + row * l.row_h;
        bool hovered = mouse_x >= l.x && mouse_x < l.x + l.w &&
                       mouse_y >= ry && mouse_y < ry + l.row_h;
        if (idx == app->palette_sel)
            vk_draw_rounded_rect(&app->draw, l.x + 5.0f, ry, l.w - 10.0f, l.row_h,
                                 5.0f, 1.0f, sel_bg);
        else if (hovered)
            vk_draw_rounded_rect(&app->draw, l.x + 5.0f, ry, l.w - 10.0f, l.row_h,
                                 5.0f, 1.0f, hover_bg);

        float baseline = ry + (l.row_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, l.x + 14.0f, baseline, cmd->label, text_c);

        float kw = font_measure(&app->font, cmd->shortcut);
        float kx = l.x + l.w - 14.0f - kw;
        vk_draw_rounded_rect(&app->draw, kx - 6.0f, ry + 4.0f, kw + 12.0f,
                             l.row_h - 8.0f, 4.0f, 1.0f, key_bg);
        font_draw(&app->font, ctx, kx, baseline, cmd->shortcut, dim);
    }
}

/* Returns true when the event was consumed by the palette. */
bool palette_handle_event(App *app, const SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode key = ev->key.key;
        if (key == SDLK_ESCAPE) {
            app->palette_open = false;
        } else if (key == SDLK_UP) {
            if (app->palette_sel > 0) app->palette_sel--;
        } else if (key == SDLK_DOWN) {
            if (app->palette_sel + 1 < app->palette_match_count) app->palette_sel++;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            palette_run_selected(app);
        } else if (key == SDLK_BACKSPACE) {
            if (app->palette_len > 0) {
                app->palette_query[--app->palette_len] = 0;
                palette_filter(app);
            }
        } else if (key == SDLK_P && (ev->key.mod & SDL_KMOD_CTRL) &&
                   (ev->key.mod & SDL_KMOD_SHIFT)) {
            app->palette_open = false;
        }
        return true;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        for (const char *s = ev->text.text; s && *s; ++s) {
            unsigned char ch = (unsigned char)*s;
            if (ch < 32 || ch >= 127) continue;
            if (app->palette_len + 1 < (int)sizeof(app->palette_query)) {
                app->palette_query[app->palette_len++] = (char)ch;
                app->palette_query[app->palette_len] = 0;
            }
        }
        palette_filter(app);
        return true;
    }
    if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        app->palette_sel -= (int)ev->wheel.y;
        if (app->palette_sel >= app->palette_match_count)
            app->palette_sel = app->palette_match_count - 1;
        if (app->palette_sel < 0) app->palette_sel = 0;
        return true;
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev->button.button == SDL_BUTTON_LEFT) {
        PaletteLayout l = palette_layout(app);
        float mx = ev->button.x, my = ev->button.y;
        bool inside = mx >= l.x && mx < l.x + l.w && my >= l.y && my < l.y + l.h;
        if (!inside) {
            app->palette_open = false;
        } else if (my >= l.list_top) {
            int idx = app->palette_scroll + (int)((my - l.list_top) / l.row_h);
            if (idx >= 0 && idx < app->palette_match_count &&
                idx < app->palette_scroll + l.visible) {
                app->palette_sel = idx;
                palette_run_selected(app);
            }
        }
        return true;
    }
    return ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_MOTION;
}
