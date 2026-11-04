/**
 * tab_bar.c – open buffers (tabs) and the tab strip at the top of the window
 */

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_messagebox.h>

#include "tab_bar.h"

Editor *app_ed(App *app)
{
    if (app->tab_count <= 0) return NULL;
    if (app->tab_active < 0) app->tab_active = 0;
    if (app->tab_active >= app->tab_count) app->tab_active = app->tab_count - 1;
    return &app->tabs[app->tab_active];
}

const char *tab_label(const App *app, int index, char *buffer, size_t buffer_size)
{
    const Editor *ed = index >= 0 && index < app->tab_count ? &app->tabs[index] : NULL;
    if (!ed) return "Untitled";
    if (!ed->path[0]) {
        unsigned id = app->tab_ids[index];
        if (id > 1)
            snprintf(buffer, buffer_size, "Untitled %u%s", id, ed->dirty ? " •" : "");
        else
            snprintf(buffer, buffer_size, "Untitled%s", ed->dirty ? " •" : "");
        return buffer;
    }
    const char *base = ed->path;
    for (const char *p = ed->path; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}

void tab_update_title(App *app)
{
    Editor *ed = app_ed(app);
    if (!ed) {
        SDL_SetWindowTitle(app->window, "cgui");
        return;
    }
    char title[1200];
    char label[256];
    const char *base = tab_label(app, app->tab_active, label, sizeof(label));
    snprintf(title, sizeof(title), "%s%s – cgui", ed->dirty ? "• " : "", base);
    SDL_SetWindowTitle(app->window, title);
}

int tab_find_path(App *app, const char *path)
{
    if (!path || !path[0]) return -1;
    for (int i = 0; i < app->tab_count; ++i) {
        if (app->tabs[i].path[0] && strcmp(app->tabs[i].path, path) == 0)
            return i;
    }
    return -1;
}

bool tab_add(App *app, const char *path_or_null, const char *initial)
{
    if (app->tab_count >= MAX_TABS) {
        fprintf(stderr, "tab limit (%d)\n", MAX_TABS);
        return false;
    }
    if (path_or_null && path_or_null[0]) {
        int exist = tab_find_path(app, path_or_null);
        if (exist >= 0) {
            app->tab_active = exist;
            tab_update_title(app);
            return true;
        }
    }
    Editor *ed = &app->tabs[app->tab_count];
    if (!editor_create(ed, &app->font, initial ? initial : ""))
        return false;
    ed->soft_wrap = app->soft_wrap;
    if (path_or_null && path_or_null[0]) {
        if (!editor_load_file(ed, path_or_null)) {
            /* keep empty tab with path attempt failed - still usable */
        }
    }
    app->tab_active = app->tab_count;
    app->tab_ids[app->tab_count] = ++app->next_tab_id;
    app->tab_count++;
    tab_update_title(app);
    return true;
}

void tab_remove_at(App *app, int index)
{
    if (index < 0 || index >= app->tab_count) return;
    editor_destroy(&app->tabs[index]);
    for (int j = index; j < app->tab_count - 1; ++j)
        app->tabs[j] = app->tabs[j + 1];
    for (int j = index; j < app->tab_count - 1; ++j)
        app->tab_ids[j] = app->tab_ids[j + 1];
    app->tab_count--;
    if (app->tab_count == 0) {
        tab_add(app, NULL, "");
        return;
    }
    if (app->tab_active > index)
        app->tab_active--;
    else if (app->tab_active == index && index > 0)
        app->tab_active = index - 1;
    else if (app->tab_active >= app->tab_count)
        app->tab_active = app->tab_count - 1;
    tab_update_title(app);
}

float tab_width(const App *app, int index)
{
    char label[256];
    const char *name = tab_label(app, index, label, sizeof(label));
    float tw = font_measure(&app->font, name) + 40.0f;
    if (app->tabs[index].dirty)
        tw += font_measure(&app->font, "• ");
    if (tw < 96.0f) tw = 96.0f;
    return tw;
}

void draw_tab_bar(App *app, VkContext *ctx)
{
    /* Closer to Xcode: flush active tab, close control on active, hairline rules */
    float w = (float)ctx->swapchain_extent.width;
    const float bar_h = 28.0f;

    uint8_t strip[4]   = { 232, 232, 232, 255 };
    uint8_t content[4] = { 252, 252, 252, 255 };
    uint8_t rule[4]    = { 198, 198, 198, 255 };
    uint8_t bottom[4]  = { 180, 180, 180, 255 };
    uint8_t text_act[4]= { 0, 0, 0, 255 };
    uint8_t text_in[4] = { 96, 96, 96, 255 };
    vk_draw_rounded_rect(&app->draw, 0, 0, w, bar_h, 0.0f, 1.0f, strip);
    vk_draw_rounded_rect(&app->draw, 0, bar_h - 1.0f, w, 1.0f, 0.0f, 1.0f, bottom);

    float widths[MAX_TABS];
    float x = 0.0f;
    int visible = 0;
    for (int i = 0; i < app->tab_count && i < MAX_TABS; ++i) {
        float tw = tab_width(app, i);
        if (x + tw > w - 4.0f && i > 0) break;
        widths[i] = tw;
        x += tw;
        visible = i + 1;
    }

    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    x = 0.0f;
    for (int i = 0; i < visible; ++i) {
        float tw = widths[i];
        bool on = (i == app->tab_active);

        if (on) {
            vk_draw_rounded_rect(&app->draw, x, 0, tw, bar_h, 0.0f, 1.0f, content);
            if (i > 0)
                vk_draw_rounded_rect(&app->draw, x, 0, 1.0f, bar_h, 0.0f, 1.0f, rule);
            vk_draw_rounded_rect(&app->draw, x + tw - 1.0f, 0, 1.0f, bar_h, 0.0f, 1.0f, rule);
        } else if (i + 1 < visible && (i + 1) != app->tab_active) {
            vk_draw_rounded_rect(&app->draw, x + tw - 1.0f, 7.0f, 1.0f, bar_h - 14.0f,
                                 0.0f, 1.0f, rule);
        }

        char label[256];
        const char *name = tab_label(app, i, label, sizeof(label));
        if (app->tabs[i].dirty && app->tabs[i].path[0])
            snprintf(label, sizeof(label), "• %s", name);
        else if (name != label)
            snprintf(label, sizeof(label), "%s", name);

        float baseline = (bar_h + app->font.ascent + app->font.descent) * 0.5f;
        /* left-aligned with padding (Xcode shifts toward centre-left when close present) */
        float tx = x + 12.0f;
        font_draw(&app->font, ctx, tx, baseline, label, on ? text_act : text_in);

        /* Reveal the Lucide close icon on tab hover. */
        bool tab_hovered = mouse_y >= 0.0f && mouse_y < bar_h &&
                           mouse_x >= x && mouse_x < x + tw;
        if (tab_hovered) {
            float close_x = x + tw - 24.0f;
            bool close_hovered = mouse_x >= close_x && mouse_x < close_x + 18.0f;
            if (close_hovered) {
                uint8_t hover_bg[4] = { 110, 110, 110, 24 };
                vk_draw_rounded_rect(&app->draw, close_x, 5.0f, 18.0f, 18.0f,
                                     4.0f, 1.0f, hover_bg);
            }
            if (app->icons_ok && app->icon_close.ready) {
                uint8_t tint[4] = { 96, 96, 96, 255 };
                icon_draw(&app->icons, &app->icon_close, ctx,
                          x + tw - 22.0f, 7.0f, 14.0f, 14.0f, tint);
            } else {
                font_draw(&app->font, ctx, x + tw - 21.0f, baseline, "x", text_in);
            }
        }

        x += tw;
    }
}

int tab_prompt_unsaved(App *app, const Editor *ed)
{
    SDL_MessageBoxButtonData buttons[] = {
        { .flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, .buttonID = 0, .text = "Cancel" },
        { .buttonID = 1, .text = "Discard" },
        { .flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, .buttonID = 2, .text = "Save" },
    };
    char message[1200];
    char label[256];
    snprintf(message, sizeof(message), "Do you want to save changes to %s?",
             tab_label(app, (int)(ed - app->tabs), label, sizeof(label)));
    SDL_MessageBoxData data = {
        .flags = SDL_MESSAGEBOX_WARNING,
        .window = app->window,
        .title = "Unsaved Changes",
        .message = message,
        .numbuttons = (int)(sizeof(buttons) / sizeof(buttons[0])),
        .buttons = buttons,
    };
    int button = 0;
    if (!SDL_ShowMessageBox(&data, &button)) {
        fprintf(stderr, "Could not show unsaved-changes prompt: %s\n", SDL_GetError());
        return 0;
    }
    return button;
}

void tab_close_at(App *app, int index)
{
    if (index < 0 || index >= app->tab_count || app->save_dialog_open) return;
    Editor *ed = &app->tabs[index];
    if (!ed->dirty) {
        tab_remove_at(app, index);
        return;
    }

    switch (tab_prompt_unsaved(app, ed)) {
    case 1:
        tab_remove_at(app, index);
        break;
    case 2:
        if (ed->path[0]) {
            if (editor_save_file(ed, NULL))
                tab_remove_at(app, index);
        } else {
            show_save_as_dialog(app, index, SAVE_INTENT_CLOSE_TAB);
        }
        break;
    default:
        break;
    }
}

void tab_bar_handle_click(App *app, float mx)
{
    float x = 0.0f;
    for (int i = 0; i < app->tab_count; ++i) {
        float tw = tab_width(app, i);
        if (mx >= x && mx < x + tw) {
            if (mx >= x + tw - 24.0f) {
                tab_close_at(app, i);
            } else {
                app->tab_active = i;
                update_window_title(app);
            }
            break;
        }
        x += tw;
    }
}
