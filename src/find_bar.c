/**
 * find_bar.c – find / replace bar (Ctrl+F, Ctrl+H) and go-to-line bar (Ctrl+G)
 */

#include <stdio.h>

#include <SDL3/SDL.h>

#include "find_bar.h"
#include "tab_bar.h"

void draw_find_bar(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float bar_h = app->replace_mode ? 72.0f : 44.0f;
    uint8_t bg[4] = { 236, 236, 240, 255 };
    uint8_t field[4] = { 255, 255, 255, 255 };
    uint8_t text_c[4] = { 30, 30, 30, 255 };
    uint8_t dim[4] = { 110, 110, 120, 255 };
    uint8_t accent[4] = { 0, 122, 255, 255 };

    vk_draw_rounded_rect(&app->draw, 0, 0, w, bar_h, 0.0f, 1.0f, bg);

    float fx = 12.0f, fy = 8.0f, fw = w - 24.0f, fh = 28.0f;
    vk_draw_rounded_rect(&app->draw, fx, fy, fw, fh, 6.0f, 1.0f, field);

    float baseline = fy + 6.0f + app->font.ascent;
    const char *fq = app->find_len > 0 ? app->find_query : "Find…";
    font_draw(&app->font, ctx, fx + 10, baseline, fq, app->find_len > 0 ? text_c : dim);
    if (app->find_field == 0) {
        float tw = font_measure(&app->font, app->find_len > 0 ? app->find_query : "");
        vk_draw_rounded_rect(&app->draw, fx + 10 + tw + 1, fy + 5, 2, fh - 10, 1, 1, accent);
    }

    /* Match count badge (e.g. "3 of 12") */
    if (app->find_len > 0) {
        int cur_match = 0;
        int total_matches = editor_count_matches(app_ed(app), app->find_query, app->find_case, &cur_match);
        char match_str[64];
        if (total_matches == 0) {
            snprintf(match_str, sizeof(match_str), "No matches");
        } else if (cur_match > 0) {
            snprintf(match_str, sizeof(match_str), "%d of %d", cur_match, total_matches);
        } else {
            snprintf(match_str, sizeof(match_str), "%d match%s", total_matches, total_matches == 1 ? "" : "es");
        }
        float mw = font_measure(&app->font, match_str);
        uint8_t count_c[4] = { 110, 110, 125, 255 };
        if (total_matches == 0) {
            count_c[0] = 210; count_c[1] = 70; count_c[2] = 70; count_c[3] = 255;
        }
        font_draw(&app->font, ctx, fx + fw - mw - 12.0f, baseline, match_str, count_c);
    }

    if (app->replace_mode) {
        float fy2 = fy + 32.0f;
        vk_draw_rounded_rect(&app->draw, fx, fy2, fw, fh, 6.0f, 1.0f, field);
        float b2 = fy2 + 6.0f + app->font.ascent;
        const char *rq = app->replace_len > 0 ? app->replace_query : "Replace with…";
        font_draw(&app->font, ctx, fx + 10, b2, rq, app->replace_len > 0 ? text_c : dim);
        if (app->find_field == 1) {
            float tw = font_measure(&app->font, app->replace_len > 0 ? app->replace_query : "");
            vk_draw_rounded_rect(&app->draw, fx + 10 + tw + 1, fy2 + 5, 2, fh - 10, 1, 1, accent);
        }
    }

    if (app->find_status[0]) {
        font_draw(&app->font, ctx, fx + 10, bar_h + app->font.ascent + 2,
                  app->find_status, dim);
    }
}


void draw_goto_bar(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float bar_h = 44.0f;
    uint8_t bg[4] = { 236, 236, 240, 255 };
    uint8_t field[4] = { 255, 255, 255, 255 };
    uint8_t text_c[4] = { 30, 30, 30, 255 };
    uint8_t dim[4] = { 110, 110, 120, 255 };
    uint8_t accent[4] = { 0, 122, 255, 255 };

    vk_draw_rounded_rect(&app->draw, 0, 0, w, bar_h, 0.0f, 1.0f, bg);
    float fx = 12.0f, fy = 8.0f, fw = w - 24.0f, fh = 28.0f;
    vk_draw_rounded_rect(&app->draw, fx, fy, fw, fh, 6.0f, 1.0f, field);

    float baseline = fy + 6.0f + app->font.ascent;
    char prompt[64];
    int cur = editor_current_line(app_ed(app));
    int tot = editor_line_count(app_ed(app));
    if (app->goto_len > 0)
        snprintf(prompt, sizeof(prompt), "Go to line: %s", app->goto_buf);
    else
        snprintf(prompt, sizeof(prompt), "Go to line… (current %d / %d)", cur, tot);
    font_draw(&app->font, ctx, fx + 10, baseline, prompt,
              app->goto_len > 0 ? text_c : dim);

    float tw = font_measure(&app->font, prompt);
    vk_draw_rounded_rect(&app->draw, fx + 10 + tw + 1, fy + 5, 2, fh - 10, 1, 1, accent);
}

void find_bar_open(App *app, bool replace)
{
    close_overlays(app);
    app->find_open = true;
    app->replace_mode = replace;
    app->find_field = 0;
    app->find_status[0] = 0;
}

void goto_bar_open(App *app)
{
    close_overlays(app);
    app->goto_open = true;
    app->goto_len = 0;
    app->goto_buf[0] = 0;
}

bool find_bar_handle_event(App *app, const SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        if (ev->key.key == SDLK_ESCAPE) {
            app->find_open = false;
            app->find_status[0] = 0;
            return true;
        }
        if (ev->key.key == SDLK_BACKSPACE) {
            if (app->find_field == 0 && app->find_len > 0) {
                app->find_query[--app->find_len] = 0;
            } else if (app->find_field == 1 && app->replace_len > 0) {
                app->replace_query[--app->replace_len] = 0;
            }
            app->find_status[0] = 0;
            return true;
        }
        if (ev->key.key == SDLK_TAB) {
            if (app->replace_mode)
                app->find_field = 1 - app->find_field;
            return true;
        }
        if (ev->key.key == SDLK_RETURN || ev->key.key == SDLK_KP_ENTER) {
            bool shift = (ev->key.mod & SDL_KMOD_SHIFT) != 0;
            bool alt = (ev->key.mod & SDL_KMOD_ALT) != 0;
            if (app->replace_mode && alt) {
                int n = editor_replace_all(app_ed(app), app->find_query,
                                           app->replace_query, app->find_case);
                snprintf(app->find_status, sizeof(app->find_status),
                         "Replaced %d", n);
            } else if (app->replace_mode && !shift && app->find_field == 1) {
                bool ok = editor_replace_next(app_ed(app), app->find_query,
                                              app->replace_query, app->find_case);
                snprintf(app->find_status, sizeof(app->find_status),
                         ok ? "Replaced · next" : "Done");
            } else {
                bool ok = shift
                    ? editor_find_prev(app_ed(app), app->find_query, app->find_case)
                    : editor_find_next(app_ed(app), app->find_query, app->find_case);
                snprintf(app->find_status, sizeof(app->find_status),
                         ok ? "Found" : "Not found");
            }
            return true;
        }
        if (ev->key.key == SDLK_G && (ev->key.mod & SDL_KMOD_CTRL)) {
            bool shift = (ev->key.mod & SDL_KMOD_SHIFT) != 0;
            bool ok = shift
                ? editor_find_prev(app_ed(app), app->find_query, app->find_case)
                : editor_find_next(app_ed(app), app->find_query, app->find_case);
            snprintf(app->find_status, sizeof(app->find_status),
                     ok ? "Found" : "Not found");
            return true;
        }
    } else if (ev->type == SDL_EVENT_TEXT_INPUT) {
        const char *s = ev->text.text;
        while (s && *s) {
            unsigned char ch = (unsigned char)*s++;
            if (ch < 32 || ch >= 127) continue;
            if (app->find_field == 0 && app->find_len + 1 < (int)sizeof(app->find_query)) {
                app->find_query[app->find_len++] = (char)ch;
                app->find_query[app->find_len] = 0;
            } else if (app->find_field == 1 && app->replace_len + 1 < (int)sizeof(app->replace_query)) {
                app->replace_query[app->replace_len++] = (char)ch;
                app->replace_query[app->replace_len] = 0;
            }
        }
        app->find_status[0] = 0;
        return true;
    }
    /* swallow other input while find is open */
    return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_TEXT_INPUT ||
           ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
}

bool goto_bar_handle_event(App *app, const SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        if (ev->key.key == SDLK_ESCAPE) {
            app->goto_open = false;
            return true;
        }
        if (ev->key.key == SDLK_BACKSPACE) {
            if (app->goto_len > 0)
                app->goto_buf[--app->goto_len] = 0;
            return true;
        }
        if (ev->key.key == SDLK_RETURN || ev->key.key == SDLK_KP_ENTER) {
            int line = 0;
            for (int i = 0; i < app->goto_len; ++i) {
                if (app->goto_buf[i] >= '0' && app->goto_buf[i] <= '9')
                    line = line * 10 + (app->goto_buf[i] - '0');
            }
            if (line > 0)
                editor_goto_line(app_ed(app), line);
            app->goto_open = false;
            return true;
        }
    } else if (ev->type == SDL_EVENT_TEXT_INPUT) {
        const char *s = ev->text.text;
        while (s && *s) {
            unsigned char ch = (unsigned char)*s++;
            if (ch >= '0' && ch <= '9' &&
                app->goto_len + 1 < (int)sizeof(app->goto_buf)) {
                app->goto_buf[app->goto_len++] = (char)ch;
                app->goto_buf[app->goto_len] = 0;
            }
        }
        return true;
    }
    return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_TEXT_INPUT ||
           ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
}
