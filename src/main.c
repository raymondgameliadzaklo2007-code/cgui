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
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>

#include "app.h"
#include "spv_loader.h"
#include "lucide_close_svg.h"
#include "tab_bar.h"
#include "sidebar.h"
#include "find_bar.h"
#include "palette.h"

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
    char left[sizeof(((Editor *)0)->path) + 8];
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

static bool swapchain_is_srgb(VkFormat fmt)
{
    switch (fmt) {
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
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
    font_set_target_srgb(&fresh, swapchain_is_srgb(app->vk.swapchain_format));
    const uint8_t text_bg[3] = {
        (uint8_t)(app->vk.clear_color[0] * 255.0f + 0.5f),
        (uint8_t)(app->vk.clear_color[1] * 255.0f + 0.5f),
        (uint8_t)(app->vk.clear_color[2] * 255.0f + 0.5f),
    };
    font_set_background(&fresh, text_bg);

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

void update_window_title(App *app)
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

void show_save_as_dialog(App *app, int tab_index, SaveIntent intent)
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

static void continue_quit(App *app)
{
    if (!app->quit_pending || app->save_dialog_open) return;
    for (int i = 0; i < app->tab_count; ++i) {
        Editor *ed = &app->tabs[i];
        if (!ed->dirty) continue;
        switch (tab_prompt_unsaved(app, ed)) {
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

/* ---------------------------------------------------------------- *
 * Commands + command palette (Ctrl+Shift+P)                        *
 * ---------------------------------------------------------------- */

void close_overlays(App *app)
{
    app->find_open = false;
    app->goto_open = false;
    app->picker_open = false;
    app->palette_open = false;
}

static void cmd_new_file(App *app)      { tab_add(app, NULL, ""); }
static void cmd_open_file(App *app)     { file_open_dialog(app); }
static void cmd_open_folder(App *app)   { project_show_folder_dialog(app); }
static void cmd_save_as(App *app)       { file_save_as_dialog(app); }
static void cmd_close_tab(App *app)     { tab_close_at(app, app->tab_active); }

static void cmd_save(App *app)
{
    Editor *ed = app_ed(app);
    if (!ed) return;
    if (!ed->path[0]) {
        file_save_as_dialog(app);
        return;
    }
    editor_save_file(ed, NULL);
    update_window_title(app);
}

static void cycle_tab(App *app, int delta)
{
    if (app->tab_count <= 0) return;
    app->tab_active = (app->tab_active + delta + app->tab_count) % app->tab_count;
    update_window_title(app);
}
static void cmd_next_tab(App *app) { cycle_tab(app, 1); }
static void cmd_prev_tab(App *app) { cycle_tab(app, -1); }

static void cmd_find(App *app)    { find_bar_open(app, false); }
static void cmd_replace(App *app) { find_bar_open(app, true); }

static void cmd_goto_line(App *app) { goto_bar_open(app); }

static void cmd_select_font(App *app)
{
    close_overlays(app);
    app->picker_open = true;
    app->picker_sel = app->font_index;
}

static void set_font_size(App *app, float size)
{
    if (size < 8.0f) size = 8.0f;
    if (size > 40.0f) size = 40.0f;
    if (size == app->font_size || app->font_index < 0 ||
        (size_t)app->font_index >= app->fonts.count)
        return;
    float previous = app->font_size;
    app->font_size = size;
    if (!reload_font(app, app->fonts.items[app->font_index].path))
        app->font_size = previous;
}
static void cmd_font_bigger(App *app)  { set_font_size(app, app->font_size + 1.0f); }
static void cmd_font_smaller(App *app) { set_font_size(app, app->font_size - 1.0f); }
static void cmd_font_reset(App *app)   { set_font_size(app, 15.0f); }

static void cmd_quit(App *app)
{
    if (!app->quit_pending) {
        app->quit_pending = true;
        if (!app->save_dialog_open)
            continue_quit(app);
    }
}

static void cmd_toggle_soft_wrap(App *app)
{
    app->soft_wrap = !app->soft_wrap;
    for (int i = 0; i < app->tab_count; ++i)
        app->tabs[i].soft_wrap = app->soft_wrap;
}

const Command app_commands[] = {
    { "File: New File",          "Ctrl+N",       cmd_new_file },
    { "File: Open File…",        "Ctrl+O",       cmd_open_file },
    { "File: Open Folder…",      "Ctrl+Shift+O", cmd_open_folder },
    { "File: Save",              "Ctrl+S",       cmd_save },
    { "File: Save As…",          "Ctrl+Shift+S", cmd_save_as },
    { "Tab: Close",              "Ctrl+W",       cmd_close_tab },
    { "Tab: Next",               "Ctrl+Tab",     cmd_next_tab },
    { "Tab: Previous",           "Ctrl+Shift+Tab", cmd_prev_tab },
    { "Search: Find",            "Ctrl+F",       cmd_find },
    { "Search: Replace",         "Ctrl+H",       cmd_replace },
    { "Go: Go to Line…",         "Ctrl+G",       cmd_goto_line },
    { "View: Select Font…",      "F1",           cmd_select_font },
    { "View: Increase Font Size", "Ctrl+=",      cmd_font_bigger },
    { "View: Decrease Font Size", "Ctrl+-",      cmd_font_smaller },
    { "View: Reset Font Size",   "Ctrl+0",       cmd_font_reset },
    { "View: Toggle Soft Wrap",   "Alt+Z",        cmd_toggle_soft_wrap },
    { "App: Quit",               "Ctrl+Q",       cmd_quit },
};
const int app_command_count = (int)(sizeof(app_commands) / sizeof(app_commands[0]));

int main(int argc, char **argv)
{
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
    SDL_PathInfo argument_info;
    bool argument_is_directory = argc > 1 &&
        SDL_GetPathInfo(argv[1], &argument_info) &&
        argument_info.type == SDL_PATHTYPE_DIRECTORY;
    if (argc > 1 && !argument_is_directory) {
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
    if (!project_open_initial(&app, argc > 1 ? argv[1] : NULL))
        fprintf(stderr, "Project navigator could not load an initial folder\n");


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
    if (app.icons_ok)
        project_icons_load(&app);

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

            if (app.palette_open) {
                if (palette_handle_event(&app, &ev))
                    continue;
            }

            if (ev.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keymod mod = ev.key.mod;
                bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
                bool shift = (mod & SDL_KMOD_SHIFT) != 0;
                bool alt = (mod & SDL_KMOD_ALT) != 0;

                if (alt && !ctrl && ev.key.key == SDLK_Z) {
                    cmd_toggle_soft_wrap(&app);
                    continue;
                }

                if (ctrl && shift && ev.key.key == SDLK_P) {
                    palette_show(&app);
                    continue;
                }
                if (!app.picker_open && ctrl && !shift &&
                    (ev.key.key == SDLK_EQUALS || ev.key.key == SDLK_PLUS ||
                     ev.key.key == SDLK_KP_PLUS)) {
                    cmd_font_bigger(&app);
                    continue;
                }
                if (!app.picker_open && ctrl &&
                    (ev.key.key == SDLK_MINUS || ev.key.key == SDLK_KP_MINUS)) {
                    cmd_font_smaller(&app);
                    continue;
                }
                if (!app.picker_open && ctrl && ev.key.key == SDLK_0) {
                    cmd_font_reset(&app);
                    continue;
                }
                if (ctrl && ev.key.key == SDLK_Q) {
                    cmd_quit(&app);
                    continue;
                }

                if (ev.key.key == SDLK_F1) {
                    app.picker_open = !app.picker_open;
                    app.picker_sel = app.font_index;
                    continue;
                }
                if (!app.picker_open && ctrl && shift && ev.key.key == SDLK_O) {
                    project_show_folder_dialog(&app);
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
                tab_bar_handle_click(&app, (float)ev.button.x);
                continue;
            }
            if (app.goto_open && goto_bar_handle_event(&app, &ev))
                continue;
            if (app.find_open && find_bar_handle_event(&app, &ev))
                continue;
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

            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                ev.button.button == SDL_BUTTON_LEFT &&
                !app.find_open && !app.goto_open && !app.picker_open &&
                project_handle_click(&app, (float)ev.button.x, (float)ev.button.y,
                                     28.0f, 26.0f,
                                     (float)app.vk.swapchain_extent.height))
                continue;

            if (ev.type == SDL_EVENT_MOUSE_WHEEL &&
                !app.find_open && !app.goto_open && !app.picker_open &&
                project_handle_wheel(&app, ev.wheel.y, 28.0f, 26.0f,
                                     (float)app.vk.swapchain_extent.height))
                continue;

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
        if (app.pending_project[0]) {
            project_set_root(&app, app.pending_project);
            app.pending_project[0] = 0;
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
        draw_project_sidebar(&app, &app.vk, wh, tab_h, status_h);
        Editor *ed = app_ed(&app);
        if (ed) {
            editor_set_find_query(ed, app.find_open && app.find_len > 0 ? app.find_query : NULL, app.find_case);
            editor_draw(ed, &app.vk, PROJECT_SIDEBAR_WIDTH, tab_h,
                        ww - PROJECT_SIDEBAR_WIDTH, wh - tab_h - status_h);
        }
        draw_status_bar(&app, &app.vk);

        if (app.goto_open)
            draw_goto_bar(&app, &app.vk);
        if (app.find_open)
            draw_find_bar(&app, &app.vk);
        if (app.picker_open)
            draw_picker(&app, &app.vk);
        if (app.palette_open)
            draw_palette(&app, &app.vk);

        if (!vk_context_end_frame(&app.vk))
            app.framebuffer_resized = true;
    }

    SDL_StopTextInput(window);
    for (int i = 0; i < app.tab_count; ++i)
        editor_destroy(&app.tabs[i]);
    project_free(&app);
    if (app.icons_ok) {
        project_icons_destroy(&app);
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
