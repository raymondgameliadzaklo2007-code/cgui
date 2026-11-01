/**
 * main.c – cgui code editor
 *
 * F1 = font picker (system fonts)
 * Ctrl+C/X/V = clipboard, wheel = scroll, shift+arrows = select
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_messagebox.h>

#include "vk_context.h"
#include "vk_draw.h"
#include "spv_loader.h"
#include "font.h"
#include "editor.h"
#include "fonts.h"
#include "icon.h"
#include "lucide_close_svg.h"

#define MAX_TABS 16

typedef enum SaveIntent {
    SAVE_INTENT_NORMAL,
    SAVE_INTENT_CLOSE_TAB,
    SAVE_INTENT_QUIT,
} SaveIntent;

typedef struct App {
    SDL_Window *window;
    VkContext   vk;
    VkDraw      draw;
    Font        font;
    FontList    fonts;

    Editor tabs[MAX_TABS];
    int    tab_count;
    int    tab_active;
    unsigned tab_ids[MAX_TABS];
    unsigned next_tab_id;

    IconRenderer icons;
    Icon         icon_close;
    bool         icons_ok;


    /* Cached text shader SPIR-V for font reloads */
    uint32_t *text_vert;
    size_t    text_vert_n;
    uint32_t *text_frag;
    size_t    text_frag_n;

    int    font_index;
    float  font_size;
    bool   picker_open;
    int    picker_sel;
    int    picker_scroll;

    bool   find_open;
    bool   replace_mode;     /* Ctrl+H */
    char   find_query[256];
    int    find_len;
    char   replace_query[256];
    int    replace_len;
    int    find_field;       /* 0 = find, 1 = replace */
    bool   find_case;
    char   find_status[64];

    bool   goto_open;
    char   goto_buf[32];
    int    goto_len;

    char pending_open[1024];
    char pending_save[1024];
    int pending_save_tab;
    SaveIntent save_intent;
    bool save_dialog_open;
    bool quit_pending;
    bool title_dirty;

    bool running;
    bool framebuffer_resized;
} App;

static Editor *app_ed(App *app)
{
    if (app->tab_count <= 0) return NULL;
    if (app->tab_active < 0) app->tab_active = 0;
    if (app->tab_active >= app->tab_count) app->tab_active = app->tab_count - 1;
    return &app->tabs[app->tab_active];
}

static const char *tab_label(const App *app, int index, char *buffer, size_t buffer_size)
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

static void tab_update_title(App *app)
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

static int tab_find_path(App *app, const char *path)
{
    if (!path || !path[0]) return -1;
    for (int i = 0; i < app->tab_count; ++i) {
        if (app->tabs[i].path[0] && strcmp(app->tabs[i].path, path) == 0)
            return i;
    }
    return -1;
}

static bool tab_add(App *app, const char *path_or_null, const char *initial)
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

static void tab_remove_at(App *app, int index)
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

static void tab_close_at(App *app, int index);
static void continue_quit(App *app);
static void show_save_as_dialog(App *app, int tab_index, SaveIntent intent);

static float tab_width(const App *app, int index)
{
    char label[256];
    const char *name = tab_label(app, index, label, sizeof(label));
    float tw = font_measure(&app->font, name) + 40.0f;
    if (app->tabs[index].dirty)
        tw += font_measure(&app->font, "• ");
    if (tw < 96.0f) tw = 96.0f;
    return tw;
}

static void draw_tab_bar(App *app, VkContext *ctx)
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

static void draw_status_bar(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float h = (float)ctx->swapchain_extent.height;
    const float bar_h = 26.0f;
    uint8_t bg[4]   = { 246, 246, 248, 255 };
    uint8_t edge[4] = { 210, 210, 215, 255 };
    uint8_t text[4] = { 90, 90, 98, 255 };

    float y = h - bar_h;
    vk_draw_rounded_rect(&app->draw, 0, y, w, bar_h, 0.0f, 1.0f, bg);
    vk_draw_rounded_rect(&app->draw, 0, y, w, 1.0f, 0.0f, 1.0f, edge);

    Editor *ed = app_ed(app);
    char left[256];
    char right[128];
    if (ed) {
        const char *path = ed->path[0] ? ed->path : "Untitled";
        snprintf(left, sizeof(left), "%s%s", ed->dirty ? "• " : "", path);
        snprintf(right, sizeof(right), "Ln %d, Col %d",
                 editor_current_line(ed), editor_current_col(ed));
    } else {
        snprintf(left, sizeof(left), "No buffer");
        right[0] = 0;
    }

    float baseline = y + (bar_h + app->font.ascent + app->font.descent) * 0.5f;
    font_draw(&app->font, ctx, 12.0f, baseline, left, text);
    float rw = font_measure(&app->font, right);
    font_draw(&app->font, ctx, w - rw - 14.0f, baseline, right, text);
}

static bool load_spv(const char **paths, uint32_t **words, size_t *count)
{
    for (int i = 0; paths[i]; ++i) {
        if (spv_load_file(paths[i], words, count)) {
            printf("Loaded %s\n", paths[i]);
            return true;
        }
    }
    return false;
}

static float display_scale(SDL_Window *win)
{
    float scale = SDL_GetWindowDisplayScale(win);
    if (scale < 1.0f) scale = 1.0f;
    /* Cap so atlas stays reasonable on absurd scales */
    if (scale > 3.0f) scale = 3.0f;
    return scale;
}

static bool reload_font(App *app, const char *path)
{
    if (!path) return false;
    Font fresh;
    float scale = display_scale(app->window);
    if (!font_create(&fresh, &app->vk, path, app->font_size, scale,
                     app->text_vert, app->text_vert_n,
                     app->text_frag, app->text_frag_n)) {
        fprintf(stderr, "Failed to load font: %s\n", path);
        return false;
    }
    font_destroy(&app->font);
    app->font = fresh;
    for (int i = 0; i < app->tab_count; ++i) {
        app->tabs[i].font = &app->font;
        app->tabs[i].line_height = app->font.line_height;
    }
    printf("Active font: %s (%.1fpx)\n", path, app->font_size);
    return true;
}

static void draw_picker(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float h = (float)ctx->swapchain_extent.height;
    float pw = w * 0.55f;
    float ph = h * 0.7f;
    float px = (w - pw) * 0.5f;
    float py = (h - ph) * 0.5f;

    uint8_t panel_bg[4] = { 245, 245, 247, 255 };
    uint8_t row_sel[4]  = { 0, 122, 255, 60 };
    uint8_t title_c[4]  = { 20, 20, 20, 255 };
    uint8_t mono_c[4]   = { 0, 100, 0, 255 };
    uint8_t text_c[4]   = { 40, 40, 40, 255 };
    uint8_t dim_c[4]    = { 120, 120, 120, 255 };

    vk_draw_rounded_rect(&app->draw, px, py, pw, ph, 12.0f, 1.25f, panel_bg);

    font_draw(&app->font, ctx, px + 20, py + 28, "Fonts  (Enter = apply, Esc = close, F1 = toggle)", title_c);

    float row_h = app->font.line_height + 4.0f;
    float list_top = py + 48.0f;
    int visible = (int)((ph - 64.0f) / row_h);
    if (visible < 1) visible = 1;

    if (app->picker_sel < app->picker_scroll)
        app->picker_scroll = app->picker_sel;
    if (app->picker_sel >= app->picker_scroll + visible)
        app->picker_scroll = app->picker_sel - visible + 1;
    if (app->picker_scroll < 0) app->picker_scroll = 0;

    int n = (int)app->fonts.count;
    for (int i = 0; i < visible; ++i) {
        int idx = app->picker_scroll + i;
        if (idx >= n) break;
        float ry = list_top + i * row_h;
        if (idx == app->picker_sel) {
            vk_draw_rounded_rect(&app->draw, px + 10, ry - app->font.ascent,
                                 pw - 20, row_h, 4.0f, 1.0f, row_sel);
        }
        FontEntry *e = &app->fonts.items[idx];
        char label[384];
        snprintf(label, sizeof(label), "%s%s%s", e->family,
                 e->monospace ? "  [mono]" : "",
                 e->regular ? "" : "  (style)");
        font_draw(&app->font, ctx, px + 24, ry,
                  label, e->monospace ? mono_c : text_c);
    }

    char status[128];
    snprintf(status, sizeof(status), "%d / %zu", app->picker_sel + 1, app->fonts.count);
    font_draw(&app->font, ctx, px + 20, py + ph - 18, status, dim_c);
}


static void update_window_title(App *app)
{
    tab_update_title(app);
}

static void on_open_dialog(void *userdata, const char * const *filelist, int filter)
{
    (void)filter;
    App *app = userdata;
    if (!filelist || !filelist[0]) return;
    snprintf(app->pending_open, sizeof(app->pending_open), "%s", filelist[0]);
}

static void on_save_dialog(void *userdata, const char * const *filelist, int filter)
{
    (void)filter;
    App *app = userdata;
    app->save_dialog_open = false;
    if (!filelist || !filelist[0]) {
        if (app->save_intent == SAVE_INTENT_QUIT)
            app->quit_pending = false;
        app->save_intent = SAVE_INTENT_NORMAL;
        return;
    }
    snprintf(app->pending_save, sizeof(app->pending_save), "%s", filelist[0]);
}

static void file_open_dialog(App *app)
{
    static const SDL_DialogFileFilter filters[] = {
        { "Source / text", "c;h;cpp;hpp;cc;hh;py;rs;go;js;ts;txt;md;json;toml" },
        { "All files", "*" },
    };
    SDL_ShowOpenFileDialog(on_open_dialog, app, app->window,
                           filters, 2, NULL, false);
}

static void file_save_as_dialog(App *app)
{
    show_save_as_dialog(app, app->tab_active, SAVE_INTENT_NORMAL);
}

static void show_save_as_dialog(App *app, int tab_index, SaveIntent intent)
{
    static const SDL_DialogFileFilter filters[] = {
        { "All files", "*" },
    };
    if (tab_index < 0 || tab_index >= app->tab_count || app->save_dialog_open)
        return;
    app->pending_save_tab = tab_index;
    app->save_intent = intent;
    app->save_dialog_open = true;
    const char *def = app->tabs[tab_index].path[0] ? app->tabs[tab_index].path : NULL;
    SDL_ShowSaveFileDialog(on_save_dialog, app, app->window, filters, 1, def);
}

static int prompt_unsaved(App *app, const Editor *ed)
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

static void tab_close_at(App *app, int index)
{
    if (index < 0 || index >= app->tab_count || app->save_dialog_open) return;
    Editor *ed = &app->tabs[index];
    if (!ed->dirty) {
        tab_remove_at(app, index);
        return;
    }

    switch (prompt_unsaved(app, ed)) {
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

static void continue_quit(App *app)
{
    if (!app->quit_pending || app->save_dialog_open) return;
    for (int i = 0; i < app->tab_count; ++i) {
        Editor *ed = &app->tabs[i];
        if (!ed->dirty) continue;
        switch (prompt_unsaved(app, ed)) {
        case 1:
            continue;
        case 2:
            if (ed->path[0]) {
                if (!editor_save_file(ed, NULL)) {
                    app->quit_pending = false;
                    return;
                }
                continue;
            }
            show_save_as_dialog(app, i, SAVE_INTENT_QUIT);
            return;
        default:
            app->quit_pending = false;
            return;
        }
    }
    app->quit_pending = false;
    app->running = false;
}


static void draw_find_bar(App *app, VkContext *ctx)
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


static void draw_goto_bar(App *app, VkContext *ctx)
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

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Size window from the display's usable bounds (swapchain follows window pixels). */
    int win_w = 1280, win_h = 800;
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    SDL_Rect bounds;
    if (SDL_GetDisplayUsableBounds(disp, &bounds)) {
        win_w = bounds.w * 4 / 5;
        win_h = bounds.h * 4 / 5;
        if (win_w < 800) win_w = 800;
        if (win_h < 600) win_h = 600;
    }

    SDL_Window *window = SDL_CreateWindow(
        "cgui – editor",
        win_w, win_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    App app = {0};
    app.window = window;
    app.running = true;
    app.font_size = 15.0f;
    app.font_index = 0;

    if (!vk_context_create(&app.vk, window)) {
        fprintf(stderr, "Vulkan context failed\n");
        return 1;
    }

    const char *rr_v[] = { "build/rounded_rect.vert.spv", "rounded_rect.vert.spv", NULL };
    const char *rr_f[] = { "build/rounded_rect.frag.spv", "rounded_rect.frag.spv", NULL };
    const char *bl_v[] = { "build/blur.vert.spv", "blur.vert.spv", NULL };
    const char *bl_f[] = { "build/blur.frag.spv", "blur.frag.spv", NULL };
    const char *bt_v[] = { "build/blit.vert.spv", "blit.vert.spv", NULL };
    const char *bt_f[] = { "build/blit.frag.spv", "blit.frag.spv", NULL };
    const char *tx_v[] = { "build/text.vert.spv", "text.vert.spv", NULL };
    const char *tx_f[] = { "build/text.frag.spv", "text.frag.spv", NULL };

    uint32_t *w1=NULL,*w2=NULL,*w3=NULL,*w4=NULL,*w5=NULL,*w6=NULL;
    size_t n1=0,n2=0,n3=0,n4=0,n5=0,n6=0;
    if (!load_spv(rr_v,&w1,&n1) || !load_spv(rr_f,&w2,&n2) ||
        !load_spv(bl_v,&w3,&n3) || !load_spv(bl_f,&w4,&n4) ||
        !load_spv(bt_v,&w5,&n5) || !load_spv(bt_f,&w6,&n6) ||
        !load_spv(tx_v,&app.text_vert,&app.text_vert_n) ||
        !load_spv(tx_f,&app.text_frag,&app.text_frag_n)) {
        fprintf(stderr, "Missing SPIR-V – run meson compile -C build\n");
        return 1;
    }

    VkDrawShaders shaders = {
        .rr_vert = w1, .rr_vert_n = n1,
        .rr_frag = w2, .rr_frag_n = n2,
        .blur_vert = w3, .blur_vert_n = n3,
        .blur_frag = w4, .blur_frag_n = n4,
        .blit_vert = w5, .blit_vert_n = n5,
        .blit_frag = w6, .blit_frag_n = n6,
    };
    if (!vk_draw_create(&app.draw, &app.vk, &shaders)) {
        fprintf(stderr, "UI draw failed\n");
        return 1;
    }
    free(w1); free(w2); free(w3); free(w4); free(w5); free(w6);

    font_list_init(&app.fonts);
    if (!font_list_scan(&app.fonts, true)) {
        fprintf(stderr, "No fonts found on system\n");
        return 1;
    }

    const char *path = font_list_default_mono(&app.fonts);
    if (!path) path = app.fonts.items[0].path;

    /* Remember index */
    for (size_t i = 0; i < app.fonts.count; ++i) {
        if (strcmp(app.fonts.items[i].path, path) == 0) {
            app.font_index = (int)i;
            app.picker_sel = (int)i;
            break;
        }
    }

    if (!reload_font(&app, path)) {
        /* Try every font until one loads */
        bool ok = false;
        for (size_t i = 0; i < app.fonts.count; ++i) {
            if (reload_font(&app, app.fonts.items[i].path)) {
                app.font_index = (int)i;
                app.picker_sel = (int)i;
                ok = true;
                break;
            }
        }
        if (!ok) {
            fprintf(stderr, "Could not load any font file\n");
            return 1;
        }
    }

    editor_set_draw_context(&app.draw);
    const char *starter =
        "// cgui editor\n"
        "// F1 fonts | Ctrl+F find | Ctrl+O open | Ctrl+S save\n"
        "#include <stdio.h>\n"
        "\n"
        "int main(void) {\n"
        "    printf(\"hello\\n\");\n"
        "    return 0;\n"
        "}\n";
    app.tab_count = 0;
    app.tab_active = 0;
    if (argc > 1) {
        if (!tab_add(&app, argv[1], NULL)) {
            fprintf(stderr, "Editor create failed\n");
            return 1;
        }
    } else {
        if (!tab_add(&app, NULL, starter)) {
            fprintf(stderr, "Editor create failed\n");
            return 1;
        }
    }


    /* Icons (optional assets) */
    {
        const char *iv[] = { "build/icon.vert.spv", "icon.vert.spv", NULL };
        const char *ifr[] = { "build/icon.frag.spv", "icon.frag.spv", NULL };
        uint32_t *vw = NULL, *fw = NULL;
        size_t vn = 0, fn = 0;
        app.icons_ok = false;
        if (load_spv(iv, &vw, &vn) && load_spv(ifr, &fw, &fn)) {
            if (icon_renderer_create(&app.icons, &app.vk, vw, vn, fw, fn)) {
                if (icon_load_svg_memory(&app.icon_close, &app.vk,
                                         lucide_close_svg_data,
                                         sizeof(lucide_close_svg_data))) {
                    app.icons_ok = true;
                }
            }
            free(vw); free(fw);
        }
    }

    SDL_StartTextInput(window);
    printf("Ready – %zu fonts available. Press F1 to pick.\n", app.fonts.count);

    while (app.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT ||
                ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (!app.quit_pending) {
                    app.quit_pending = true;
                    if (!app.save_dialog_open)
                        continue_quit(&app);
                }
                continue;
            }
            if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
                ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                app.framebuffer_resized = true;
                continue;
            }
            if (app.save_dialog_open)
                continue;

            if (ev.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keymod mod = ev.key.mod;
                bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
                bool shift = (mod & SDL_KMOD_SHIFT) != 0;

                if (ev.key.key == SDLK_F1) {
                    app.picker_open = !app.picker_open;
                    app.picker_sel = app.font_index;
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_O) {
                    file_open_dialog(&app);
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_F) {
                    app.find_open = true;
                    app.replace_mode = false;
                    app.find_field = 0;
                    app.picker_open = false;
                    app.find_status[0] = 0;
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_H) {
                    app.find_open = true;
                    app.replace_mode = true;
                    app.find_field = 0;
                    app.picker_open = false;
                    app.find_status[0] = 0;
                    continue;
                }

                if (!app.picker_open && ctrl && ev.key.key == SDLK_N) {
                    tab_add(&app, NULL, "");
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_W) {
                    tab_close_at(&app, app.tab_active);
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_TAB) {
                    if (app.tab_count > 0) {
                        app.tab_active = (app.tab_active + (shift ? app.tab_count - 1 : 1)) % app.tab_count;
                        update_window_title(&app);
                    }
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_G) {
                    app.goto_open = true;
                    app.find_open = false;
                    app.picker_open = false;
                    app.goto_len = 0;
                    app.goto_buf[0] = 0;
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_S) {
                    if (shift || !(app_ed(&app) && app_ed(&app)->path[0]))
                        file_save_as_dialog(&app);
                    else {
                        editor_save_file(app_ed(&app), NULL);
                        update_window_title(&app);
                    }
                    continue;
                }
            }




            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                ev.button.button == SDL_BUTTON_LEFT &&
                ev.button.y < 28 &&
                !app.find_open && !app.goto_open && !app.picker_open) {
                float mx = (float)ev.button.x;
                float x = 0.0f;
                for (int i = 0; i < app.tab_count; ++i) {
                    float tw = tab_width(&app, i);
                    if (mx >= x && mx < x + tw) {
                        if (mx >= x + tw - 24.0f) {
                            tab_close_at(&app, i);
                        } else {
                            app.tab_active = i;
                            update_window_title(&app);
                        }
                        break;
                    }
                    x += tw;
                }
                continue;
            }
            if (app.goto_open) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_ESCAPE) {
                        app.goto_open = false;
                        continue;
                    }
                    if (ev.key.key == SDLK_BACKSPACE) {
                        if (app.goto_len > 0)
                            app.goto_buf[--app.goto_len] = 0;
                        continue;
                    }
                    if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                        int line = 0;
                        for (int i = 0; i < app.goto_len; ++i) {
                            if (app.goto_buf[i] >= '0' && app.goto_buf[i] <= '9')
                                line = line * 10 + (app.goto_buf[i] - '0');
                        }
                        if (line > 0)
                            editor_goto_line(app_ed(&app), line);
                        app.goto_open = false;
                        continue;
                    }
                } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                    const char *s = ev.text.text;
                    while (s && *s) {
                        unsigned char ch = (unsigned char)*s++;
                        if (ch >= '0' && ch <= '9' &&
                            app.goto_len + 1 < (int)sizeof(app.goto_buf)) {
                            app.goto_buf[app.goto_len++] = (char)ch;
                            app.goto_buf[app.goto_len] = 0;
                        }
                    }
                    continue;
                }
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_TEXT_INPUT ||
                    ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                    continue;
            }

            if (app.find_open) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_ESCAPE) {
                        app.find_open = false;
                        app.find_status[0] = 0;
                        continue;
                    }
                    if (ev.key.key == SDLK_BACKSPACE) {
                        if (app.find_field == 0 && app.find_len > 0) {
                            app.find_query[--app.find_len] = 0;
                        } else if (app.find_field == 1 && app.replace_len > 0) {
                            app.replace_query[--app.replace_len] = 0;
                        }
                        app.find_status[0] = 0;
                        continue;
                    }
                    if (ev.key.key == SDLK_TAB) {
                        if (app.replace_mode)
                            app.find_field = 1 - app.find_field;
                        continue;
                    }
                    if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                        bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                        bool alt = (ev.key.mod & SDL_KMOD_ALT) != 0;
                        if (app.replace_mode && alt) {
                            int n = editor_replace_all(app_ed(&app), app.find_query,
                                                       app.replace_query, app.find_case);
                            snprintf(app.find_status, sizeof(app.find_status),
                                     "Replaced %d", n);
                        } else if (app.replace_mode && !shift && app.find_field == 1) {
                            bool ok = editor_replace_next(app_ed(&app), app.find_query,
                                                          app.replace_query, app.find_case);
                            snprintf(app.find_status, sizeof(app.find_status),
                                     ok ? "Replaced · next" : "Done");
                        } else {
                            bool ok = shift
                                ? editor_find_prev(app_ed(&app), app.find_query, app.find_case)
                                : editor_find_next(app_ed(&app), app.find_query, app.find_case);
                            snprintf(app.find_status, sizeof(app.find_status),
                                     ok ? "Found" : "Not found");
                        }
                        continue;
                    }
                    if (ev.key.key == SDLK_G && (ev.key.mod & SDL_KMOD_CTRL)) {
                        bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                        bool ok = shift
                            ? editor_find_prev(app_ed(&app), app.find_query, app.find_case)
                            : editor_find_next(app_ed(&app), app.find_query, app.find_case);
                        snprintf(app.find_status, sizeof(app.find_status),
                                 ok ? "Found" : "Not found");
                        continue;
                    }
                } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                    const char *s = ev.text.text;
                    while (s && *s) {
                        unsigned char ch = (unsigned char)*s++;
                        if (ch < 32 || ch >= 127) continue;
                        if (app.find_field == 0 && app.find_len + 1 < (int)sizeof(app.find_query)) {
                            app.find_query[app.find_len++] = (char)ch;
                            app.find_query[app.find_len] = 0;
                        } else if (app.find_field == 1 && app.replace_len + 1 < (int)sizeof(app.replace_query)) {
                            app.replace_query[app.replace_len++] = (char)ch;
                            app.replace_query[app.replace_len] = 0;
                        }
                    }
                    app.find_status[0] = 0;
                    continue;
                }
                /* swallow other input while find is open */
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_TEXT_INPUT ||
                    ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                    continue;
            }
            if (app.picker_open) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_ESCAPE) {
                        app.picker_open = false;
                    } else if (ev.key.key == SDLK_UP) {
                        if (app.picker_sel > 0) app.picker_sel--;
                    } else if (ev.key.key == SDLK_DOWN) {
                        if (app.picker_sel + 1 < (int)app.fonts.count) app.picker_sel++;
                    } else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) {
                        if (app.picker_sel >= 0 && app.picker_sel < (int)app.fonts.count) {
                            if (reload_font(&app, app.fonts.items[app.picker_sel].path)) {
                                app.font_index = app.picker_sel;
                                app.picker_open = false;
                            }
                        }
                    } else if (ev.key.key == SDLK_PAGEUP) {
                        app.picker_sel -= 10;
                        if (app.picker_sel < 0) app.picker_sel = 0;
                    } else if (ev.key.key == SDLK_PAGEDOWN) {
                        app.picker_sel += 10;
                        if (app.picker_sel >= (int)app.fonts.count)
                            app.picker_sel = (int)app.fonts.count - 1;
                    }
                } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                    app.picker_sel -= (int)ev.wheel.y * 3;
                    if (app.picker_sel < 0) app.picker_sel = 0;
                    if (app.picker_sel >= (int)app.fonts.count)
                        app.picker_sel = (int)app.fonts.count - 1;
                }
                continue;
            }

            {
                Editor *ed = app_ed(&app);
                if (ed) editor_handle_event(ed, &ev);
            }
        }

        if (app.framebuffer_resized) {
            int ww = 0, hh = 0;
            SDL_GetWindowSizeInPixels(window, &ww, &hh);
            if (ww > 0 && hh > 0) {
                vk_context_recreate_swapchain(&app.vk);
                vk_draw_resize(&app.draw);
            }
            app.framebuffer_resized = false;
        }

        
        if (app.pending_open[0]) {
            tab_add(&app, app.pending_open, NULL);
            app.pending_open[0] = 0;
        }
        if (app.pending_save[0]) {
            SaveIntent intent = app.save_intent;
            int index = app.pending_save_tab;
            bool saved = index >= 0 && index < app.tab_count &&
                         editor_save_file(&app.tabs[index], app.pending_save);
            app.pending_save[0] = 0;
            app.save_intent = SAVE_INTENT_NORMAL;
            if (saved && intent == SAVE_INTENT_CLOSE_TAB)
                tab_remove_at(&app, index);
            else if (!saved && intent == SAVE_INTENT_QUIT)
                app.quit_pending = false;
            update_window_title(&app);
        }
        if (app.quit_pending && !app.save_dialog_open)
            continue_quit(&app);
        {
            Editor *ed = app_ed(&app);
            bool dirty = ed && ed->dirty;
            if (dirty != app.title_dirty) {
                app.title_dirty = dirty;
                update_window_title(&app);
            }
        }

        if (!vk_context_begin_frame(&app.vk)) {
            app.framebuffer_resized = true;
            continue;
        }

        float ww = (float)app.vk.swapchain_extent.width;
        float wh = (float)app.vk.swapchain_extent.height;
        const float tab_h = 28.0f;
        const float status_h = 26.0f;
        draw_tab_bar(&app, &app.vk);
        Editor *ed = app_ed(&app);
        if (ed)
            editor_draw(ed, &app.vk, 0, tab_h, ww, wh - tab_h - status_h);
        draw_status_bar(&app, &app.vk);

        if (app.goto_open)
            draw_goto_bar(&app, &app.vk);
        if (app.find_open)
            draw_find_bar(&app, &app.vk);
        if (app.picker_open)
            draw_picker(&app, &app.vk);

        if (!vk_context_end_frame(&app.vk))
            app.framebuffer_resized = true;
    }

    SDL_StopTextInput(window);
    for (int i = 0; i < app.tab_count; ++i)
        editor_destroy(&app.tabs[i]);
    if (app.icons_ok) {
        icon_destroy(&app.icon_close, &app.vk);
        icon_renderer_destroy(&app.icons);
    }
    font_destroy(&app.font);
    font_list_free(&app.fonts);
    free(app.text_vert);
    free(app.text_frag);
    vk_draw_destroy(&app.draw);
    vk_context_destroy(&app.vk);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
