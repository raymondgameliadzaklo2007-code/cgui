#include <stdio.h>

#include "app/overlay_input.h"
#include "app/tabs.h"
#include "ui/font_picker.h"

static void goto_handle_key(App *app, const SDL_KeyboardEvent *key)
{
    if (key->key == SDLK_ESCAPE) {
        app->goto_open = false;
    } else if (key->key == SDLK_BACKSPACE) {
        if (app->goto_len > 0)
            app->goto_buf[--app->goto_len] = 0;
    } else if (key->key == SDLK_RETURN || key->key == SDLK_KP_ENTER) {
        int line = 0;
        for (int i = 0; i < app->goto_len; ++i) {
            if (app->goto_buf[i] >= '0' && app->goto_buf[i] <= '9')
                line = line * 10 + (app->goto_buf[i] - '0');
        }
        if (line > 0)
            editor_goto_line(app_ed(app), line);
        app->goto_open = false;
    }
}

bool overlay_goto_event(App *app, SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        goto_handle_key(app, &ev->key);
        return true;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
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
    return ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
}

static void find_handle_key(App *app, const SDL_KeyboardEvent *key)
{
    if (key->key == SDLK_ESCAPE) {
        app->find_open = false;
        app->find_status[0] = 0;
        return;
    }
    if (key->key == SDLK_BACKSPACE) {
        if (app->find_field == 0 && app->find_len > 0) {
            app->find_query[--app->find_len] = 0;
        } else if (app->find_field == 1 && app->replace_len > 0) {
            app->replace_query[--app->replace_len] = 0;
        }
        app->find_status[0] = 0;
        return;
    }
    if (key->key == SDLK_TAB) {
        if (app->replace_mode)
            app->find_field = 1 - app->find_field;
        return;
    }
    if (key->key == SDLK_RETURN || key->key == SDLK_KP_ENTER) {
        bool shift = (key->mod & SDL_KMOD_SHIFT) != 0;
        bool alt = (key->mod & SDL_KMOD_ALT) != 0;
        if (app->replace_mode && alt) {
            int n = editor_replace_all(app_ed(app), app->find_query,
                                       app->replace_query, app->find_case);
            snprintf(app->find_status, sizeof(app->find_status), "Replaced %d", n);
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
        return;
    }
    if (key->key == SDLK_G && (key->mod & SDL_KMOD_CTRL)) {
        bool shift = (key->mod & SDL_KMOD_SHIFT) != 0;
        bool ok = shift
            ? editor_find_prev(app_ed(app), app->find_query, app->find_case)
            : editor_find_next(app_ed(app), app->find_query, app->find_case);
        snprintf(app->find_status, sizeof(app->find_status),
                 ok ? "Found" : "Not found");
    }
}

bool overlay_find_event(App *app, SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        find_handle_key(app, &ev->key);
        return true;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
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
    return ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
}

void overlay_picker_event(App *app, SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        if (ev->key.key == SDLK_ESCAPE) {
            app->picker_open = false;
        } else if (ev->key.key == SDLK_UP) {
            if (app->picker_sel > 0) app->picker_sel--;
        } else if (ev->key.key == SDLK_DOWN) {
            if (app->picker_sel + 1 < (int)app->fonts.count) app->picker_sel++;
        } else if (ev->key.key == SDLK_RETURN || ev->key.key == SDLK_KP_ENTER) {
            if (app->picker_sel >= 0 && app->picker_sel < (int)app->fonts.count) {
                if (reload_font(app, app->fonts.items[app->picker_sel].path)) {
                    app->font_index = app->picker_sel;
                    app->picker_open = false;
                }
            }
        } else if (ev->key.key == SDLK_PAGEUP) {
            app->picker_sel -= 10;
            if (app->picker_sel < 0) app->picker_sel = 0;
        } else if (ev->key.key == SDLK_PAGEDOWN) {
            app->picker_sel += 10;
            if (app->picker_sel >= (int)app->fonts.count)
                app->picker_sel = (int)app->fonts.count - 1;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        app->picker_sel -= (int)ev->wheel.y * 3;
        if (app->picker_sel < 0) app->picker_sel = 0;
        if (app->picker_sel >= (int)app->fonts.count)
            app->picker_sel = (int)app->fonts.count - 1;
    }
}
