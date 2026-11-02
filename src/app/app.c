#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app.h"
#include "app/dialogs.h"
#include "app/tabs.h"
#include "project/project_sidebar.h"
#include "project/project_tree.h"
#include "spv_loader.h"
#include "ui/font_picker.h"
#include "ui/search_bars.h"
#include "ui/status_bar.h"
#include "ui/tab_bar.h"
#include "lucide_close_svg.h"

static const char *starter_text =
    "// cgui editor\n"
    "// F1 fonts | Ctrl+F find | Ctrl+O open | Ctrl+S save\n"
    "#include <stdio.h>\n"
    "\n"
    "int main(void) {\n"
    "    printf(\"hello\\n\");\n"
    "    return 0;\n"
    "}\n";

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

static bool create_draw(App *app)
{
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
        !load_spv(tx_v,&app->text_vert,&app->text_vert_n) ||
        !load_spv(tx_f,&app->text_frag,&app->text_frag_n)) {
        fprintf(stderr, "Missing SPIR-V – run meson compile -C build\n");
        return false;
    }

    VkDrawShaders shaders = {
        .rr_vert = w1, .rr_vert_n = n1,
        .rr_frag = w2, .rr_frag_n = n2,
        .blur_vert = w3, .blur_vert_n = n3,
        .blur_frag = w4, .blur_frag_n = n4,
        .blit_vert = w5, .blit_vert_n = n5,
        .blit_frag = w6, .blit_frag_n = n6,
    };
    bool ok = vk_draw_create(&app->draw, &app->vk, &shaders);
    free(w1); free(w2); free(w3); free(w4); free(w5); free(w6);
    if (!ok)
        fprintf(stderr, "UI draw failed\n");
    return ok;
}

static bool choose_font(App *app)
{
    font_list_init(&app->fonts);
    if (!font_list_scan(&app->fonts, true)) {
        fprintf(stderr, "No fonts found on system\n");
        return false;
    }

    const char *path = font_list_default_mono(&app->fonts);
    if (!path) path = app->fonts.items[0].path;

    for (size_t i = 0; i < app->fonts.count; ++i) {
        if (strcmp(app->fonts.items[i].path, path) == 0) {
            app->font_index = (int)i;
            app->picker_sel = (int)i;
            break;
        }
    }

    if (!reload_font(app, path)) {
        bool ok = false;
        for (size_t i = 0; i < app->fonts.count; ++i) {
            if (reload_font(app, app->fonts.items[i].path)) {
                app->font_index = (int)i;
                app->picker_sel = (int)i;
                ok = true;
                break;
            }
        }
        if (!ok) {
            fprintf(stderr, "Could not load any font file\n");
            return false;
        }
    }
    return true;
}

static bool open_initial_tabs(App *app, int argc, char **argv)
{
    app->tab_count = 0;
    app->tab_active = 0;
    SDL_PathInfo argument_info;
    bool argument_is_directory = argc > 1 &&
        SDL_GetPathInfo(argv[1], &argument_info) &&
        argument_info.type == SDL_PATHTYPE_DIRECTORY;
    if (argc > 1 && !argument_is_directory) {
        if (!tab_add(app, argv[1], NULL)) {
            fprintf(stderr, "Editor create failed\n");
            return false;
        }
    } else {
        if (!tab_add(app, NULL, starter_text)) {
            fprintf(stderr, "Editor create failed\n");
            return false;
        }
    }
    return true;
}

static void load_icons(App *app)
{
    const char *iv[] = { "build/icon.vert.spv", "icon.vert.spv", NULL };
    const char *ifr[] = { "build/icon.frag.spv", "icon.frag.spv", NULL };
    uint32_t *vw = NULL, *fw = NULL;
    size_t vn = 0, fn = 0;
    app->icons_ok = false;
    if (load_spv(iv, &vw, &vn) && load_spv(ifr, &fw, &fn)) {
        if (icon_renderer_create(&app->icons, &app->vk, vw, vn, fw, fn)) {
            if (icon_load_svg_memory(&app->icon_close, &app->vk,
                                     lucide_close_svg_data,
                                     sizeof(lucide_close_svg_data))) {
                app->icons_ok = true;
            }
        }
        free(vw); free(fw);
    }
    if (app->icons_ok)
        project_icons_load(app);
}

bool app_init(App *app, SDL_Window *window, int argc, char **argv)
{
    app->window = window;
    app->running = true;
    app->font_size = 15.0f;
    app->font_index = 0;

    if (!vk_context_create(&app->vk, window)) {
        fprintf(stderr, "Vulkan context failed\n");
        return false;
    }
    if (!create_draw(app))
        return false;
    if (!choose_font(app))
        return false;

    editor_set_draw_context(&app->draw);
    if (!open_initial_tabs(app, argc, argv))
        return false;
    if (!project_open_initial(app, argc > 1 ? argv[1] : NULL))
        fprintf(stderr, "Project navigator could not load an initial folder\n");

    load_icons(app);

    SDL_StartTextInput(window);
    printf("Ready – %zu fonts available. Press F1 to pick.\n", app->fonts.count);
    return true;
}

static void process_pending(App *app)
{
    if (app->pending_open[0]) {
        tab_add(app, app->pending_open, NULL);
        app->pending_open[0] = 0;
    }
    if (app->pending_project[0]) {
        project_set_root(app, app->pending_project);
        app->pending_project[0] = 0;
    }
    if (app->pending_save[0]) {
        SaveIntent intent = app->save_intent;
        int index = app->pending_save_tab;
        bool saved = index >= 0 && index < app->tab_count &&
                     editor_save_file(&app->tabs[index], app->pending_save);
        app->pending_save[0] = 0;
        app->save_intent = SAVE_INTENT_NORMAL;
        if (saved && intent == SAVE_INTENT_CLOSE_TAB)
            tab_remove_at(app, index);
        else if (!saved && intent == SAVE_INTENT_QUIT)
            app->quit_pending = false;
        tab_update_title(app);
    }
}

static void sync_window_title(App *app)
{
    Editor *ed = app_ed(app);
    bool dirty = ed && ed->dirty;
    if (dirty != app->title_dirty) {
        app->title_dirty = dirty;
        tab_update_title(app);
    }
}

static void draw_frame(App *app)
{
    VkContext *ctx = &app->vk;
    float ww = (float)ctx->swapchain_extent.width;
    float wh = (float)ctx->swapchain_extent.height;
    draw_tab_bar(app, ctx);
    draw_project_sidebar(app, ctx, wh, TAB_BAR_HEIGHT, STATUS_BAR_HEIGHT);
    Editor *ed = app_ed(app);
    if (ed)
        editor_draw(ed, ctx, PROJECT_SIDEBAR_WIDTH, TAB_BAR_HEIGHT,
                    ww - PROJECT_SIDEBAR_WIDTH, wh - TAB_BAR_HEIGHT - STATUS_BAR_HEIGHT);
    draw_status_bar(app, ctx);

    if (app->goto_open)
        draw_goto_bar(app, ctx);
    if (app->find_open)
        draw_find_bar(app, ctx);
    if (app->picker_open)
        draw_picker(app, ctx);
}

void app_frame(App *app)
{
    if (app->framebuffer_resized) {
        int ww = 0, hh = 0;
        SDL_GetWindowSizeInPixels(app->window, &ww, &hh);
        if (ww > 0 && hh > 0) {
            vk_context_recreate_swapchain(&app->vk);
            vk_draw_resize(&app->draw);
        }
        app->framebuffer_resized = false;
    }

    process_pending(app);
    if (app->quit_pending && !app->save_dialog_open)
        continue_quit(app);
    sync_window_title(app);

    if (!vk_context_begin_frame(&app->vk)) {
        app->framebuffer_resized = true;
        return;
    }
    draw_frame(app);
    if (!vk_context_end_frame(&app->vk))
        app->framebuffer_resized = true;
}

void app_shutdown(App *app)
{
    SDL_StopTextInput(app->window);
    for (int i = 0; i < app->tab_count; ++i)
        editor_destroy(&app->tabs[i]);
    free(app->project_entries);
    free(app->project_visible);
    if (app->icons_ok) {
        project_icons_destroy(app);
        icon_destroy(&app->icon_close, &app->vk);
        icon_renderer_destroy(&app->icons);
    }
    font_destroy(&app->font);
    font_list_free(&app->fonts);
    free(app->text_vert);
    free(app->text_frag);
    vk_draw_destroy(&app->draw);
    vk_context_destroy(&app->vk);
}
