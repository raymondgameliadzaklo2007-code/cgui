/**
 * welcome.c – Xcode & JetBrains style Welcome Window & Project Generator for cgui
 */

#include "welcome.h"
#include "app.h"
#include "theme.h"
#include "sidebar.h"
#include "tab_bar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>

static void get_recent_file_path(char *out, size_t sz)
{
    char *pref = SDL_GetPrefPath("cgui", "cgui");
    if (pref) {
        snprintf(out, sz, "%srecent_projects.txt", pref);
        SDL_free(pref);
        return;
    }
    const char *home = getenv("HOME");
    if (home) {
        snprintf(out, sz, "%s/.config/cgui/recent_projects.txt", home);
        return;
    }
    snprintf(out, sz, "recent_projects.txt");
}

void welcome_add_recent(const char *path)
{
    if (!path || !path[0]) return;

    char file_path[1024];
    get_recent_file_path(file_path, sizeof(file_path));

    char existing[16][1024];
    int count = 0;

    FILE *f = fopen(file_path, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f) && count < 16) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) line[--l] = 0;
            if (line[0] && strcmp(line, path) != 0) {
                snprintf(existing[count++], sizeof(existing[0]), "%s", line);
            }
        }
        fclose(f);
    }

    f = fopen(file_path, "w");
    if (!f) return;

    fprintf(f, "%s\n", path);
    for (int i = 0; i < count && i < 15; ++i) {
        fprintf(f, "%s\n", existing[i]);
    }
    fclose(f);
}

void welcome_load_recents(WelcomeState *st)
{
    if (!st) return;
    st->recent_count = 0;

    char file_path[1024];
    get_recent_file_path(file_path, sizeof(file_path));

    FILE *f = fopen(file_path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f) && st->recent_count < 16) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) line[--l] = 0;
        if (!line[0]) continue;

        SDL_PathInfo info;
        if (SDL_GetPathInfo(line, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            snprintf(st->recent_paths[st->recent_count], sizeof(st->recent_paths[0]), "%s", line);

            const char *slash = strrchr(line, '/');
            const char *base = slash ? slash + 1 : line;
            snprintf(st->recent_names[st->recent_count], sizeof(st->recent_names[0]), "%s", base[0] ? base : line);

            st->recent_count++;
        }
    }
    fclose(f);
}

void welcome_init(App *app)
{
    if (!app) return;
    WelcomeState *st = &app->welcome;
    memset(st, 0, sizeof(*st));

    st->mode = WELCOME_MODE_HUB;
    st->selected_template = PROJECT_TEMPLATE_C_MESON;
    st->focused_field = 1;

    const ProjectTemplateInfo *info = project_template_get_info(st->selected_template);
    snprintf(st->project_name, sizeof(st->project_name), "%s", info->default_name);
    st->project_name_len = (int)strlen(st->project_name);

    const char *home = getenv("HOME");
    if (home) {
        char proj_dir[512];
        snprintf(proj_dir, sizeof(proj_dir), "%s/Projects", home);
        SDL_PathInfo pinfo;
        if (SDL_GetPathInfo(proj_dir, &pinfo) && pinfo.type == SDL_PATHTYPE_DIRECTORY) {
            snprintf(st->parent_location, sizeof(st->parent_location), "%s", proj_dir);
        } else {
            snprintf(st->parent_location, sizeof(st->parent_location), "%s", home);
        }
    } else {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(st->parent_location, sizeof(st->parent_location), "%s", cwd);
        } else {
            snprintf(st->parent_location, sizeof(st->parent_location), "/tmp");
        }
    }
    st->parent_location_len = (int)strlen(st->parent_location);

    welcome_load_recents(st);
}

void welcome_open(App *app)
{
    if (!app) return;
    close_overlays(app);
    app->welcome_open = true;

    /* If opened while a project is already loaded, jump straight to New Project mode;
       otherwise, show Xcode-style Welcome Hub */
    if (app->project_root[0] || app->tab_count > 0) {
        app->welcome.mode = WELCOME_MODE_NEW_PROJECT;
    } else {
        app->welcome.mode = WELCOME_MODE_HUB;
    }
    welcome_load_recents(&app->welcome);
}

void welcome_close(App *app)
{
    if (!app) return;
    app->welcome_open = false;
}

static void on_browse_folder(void *userdata, const char * const *filelist, int filter)
{
    (void)filter;
    App *app = userdata;
    if (!filelist || !filelist[0]) return;

    snprintf(app->welcome.parent_location, sizeof(app->welcome.parent_location), "%s", filelist[0]);
    app->welcome.parent_location_len = (int)strlen(app->welcome.parent_location);
}

static bool welcome_create_project(App *app)
{
    WelcomeState *st = &app->welcome;
    if (!st->project_name[0] || !st->parent_location[0]) {
        snprintf(st->status_msg, sizeof(st->status_msg), "Project name and location cannot be empty.");
        st->status_ms = SDL_GetTicks();
        return false;
    }

    char target_dir[1024];
    snprintf(target_dir, sizeof(target_dir), "%s/%s", st->parent_location, st->project_name);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(target_dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        (void)mkdir(target_dir, 0755);
    }

    if (!project_scaffold(target_dir, (ProjectTemplate)st->selected_template, st->project_name)) {
        snprintf(st->status_msg, sizeof(st->status_msg), "Failed to generate project files.");
        st->status_ms = SDL_GetTicks();
        return false;
    }

    welcome_add_recent(target_dir);
    project_set_root(app, target_dir);
    project_config_detect(&app->project_cfg, app->project_root);

    const char *entry_rel = project_template_entry_file((ProjectTemplate)st->selected_template);
    char entry_abs[1024];
    snprintf(entry_abs, sizeof(entry_abs), "%s/%s", target_dir, entry_rel);
    tab_add(app, entry_abs, NULL);

    update_window_title(app);
    welcome_close(app);
    return true;
}

void welcome_draw(App *app, VkContext *ctx)
{
    if (!app || !app->welcome_open) return;

    WelcomeState *st = &app->welcome;
    const Theme *th = theme_current();

    float ww = (float)ctx->swapchain_extent.width;
    float wh = (float)ctx->swapchain_extent.height;

    uint64_t now = SDL_GetTicks();
    if (now - st->last_blink_ms >= 500) {
        st->cursor_blink = !st->cursor_blink;
        st->last_blink_ms = now;
    }

    /* 1. Scrim backdrop */
    uint8_t scrim[4] = { 12, 14, 18, 180 };
    vk_draw_rounded_rect(&app->draw, 0.0f, 0.0f, ww, wh, 0.0f, 1.0f, scrim);

    /* 2. Window Card Geometry (Proportional & Centered) */
    float card_w = 780.0f;
    float card_h = 490.0f;
    if (card_w > ww - 40.0f) card_w = ww - 40.0f;
    if (card_h > wh - 40.0f) card_h = wh - 40.0f;

    float card_x = (float)(int)((ww - card_w) * 0.5f);
    float card_y = (float)(int)((wh - card_h) * 0.5f);

    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    /* Card background & subtle border shadow */
    vk_draw_rounded_rect(&app->draw, card_x - 1.0f, card_y - 1.0f, card_w + 2.0f, card_h + 2.0f,
                         10.0f, 1.0f, th->overlay_edge);
    vk_draw_rounded_rect(&app->draw, card_x, card_y, card_w, card_h,
                         9.0f, 1.0f, th->overlay_bg);

    /* Close Button in top right */
    bool can_close = (app->tab_count > 0 || app->project_root[0]);
    float close_x = card_x + card_w - 28.0f;
    float close_y = card_y + 12.0f;
    bool close_hov = (mx >= close_x - 4.0f && mx < close_x + 20.0f && my >= close_y - 2.0f && my < close_y + 20.0f);
    if (close_hov) {
        uint8_t chov_bg[4] = { 80, 85, 95, 45 };
        vk_draw_rounded_rect(&app->draw, close_x - 4.0f, close_y - 2.0f, 22.0f, 20.0f, 4.0f, 1.0f, chov_bg);
    }
    font_draw(&app->font, ctx, close_x, close_y + app->font.ascent, "✕",
              close_hov ? th->fg : th->gutter_fg);

    /* =========================================================================
     * VIEW 1: XCODE WELCOME HUB (Actions on Left, Recent Projects on Right)
     * ========================================================================= */
    if (st->mode == WELCOME_MODE_HUB) {
        float left_w = 320.0f;

        /* Left Pane (Branding & Action Cards) */
        vk_draw_rounded_rect(&app->draw, card_x, card_y, left_w, card_h, 9.0f, 1.0f, th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, card_x + left_w - 1.0f, card_y, 1.0f, card_h, 0.0f, 1.0f, th->sidebar_edge);

        /* Branding */
        float by = card_y + 36.0f;
        font_draw(&app->font, ctx, card_x + 32.0f, by + app->font.ascent + 2.0f, "cgui", th->fg);
        by += 32.0f;
        font_draw(&app->font, ctx, card_x + 32.0f, by + app->font.ascent, "Welcome to cgui", th->fg);
        by += 20.0f;
        font_draw(&app->font, ctx, card_x + 32.0f, by + app->font.ascent, "Version 0.1.0  •  C23 & Vulkan Editor", th->gutter_fg);
        by += 44.0f;

        /* Action Card 1: Create New Project */
        float act_w = left_w - 64.0f;
        float act_h = 56.0f;

        bool act1_hov = (mx >= card_x + 32.0f && mx < card_x + 32.0f + act_w &&
                         my >= by && my < by + act_h);
        vk_draw_rounded_rect(&app->draw, card_x + 32.0f, by, act_w, act_h, 6.0f, 1.0f,
                             act1_hov ? th->sidebar_hover : th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, card_x + 32.0f, by, act_w, act_h, 6.0f, 1.0f, th->sidebar_edge);

        font_draw(&app->font, ctx, card_x + 46.0f, by + 20.0f, "+ Create New Project…", th->fg);
        font_draw(&app->font, ctx, card_x + 46.0f, by + 38.0f, "Start with a Meson, Cargo, or Make starter", th->gutter_fg);

        by += act_h + 16.0f;

        /* Action Card 2: Open Existing Project */
        bool act2_hov = (mx >= card_x + 32.0f && mx < card_x + 32.0f + act_w &&
                         my >= by && my < by + act_h);
        vk_draw_rounded_rect(&app->draw, card_x + 32.0f, by, act_w, act_h, 6.0f, 1.0f,
                             act2_hov ? th->sidebar_hover : th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, card_x + 32.0f, by, act_w, act_h, 6.0f, 1.0f, th->sidebar_edge);

        font_draw(&app->font, ctx, card_x + 46.0f, by + 20.0f, "Open Existing Folder…", th->fg);
        font_draw(&app->font, ctx, card_x + 46.0f, by + 38.0f, "Open an existing repository or workspace", th->gutter_fg);

        /* Hint at bottom */
        if (can_close) {
            font_draw(&app->font, ctx, card_x + 32.0f, card_y + card_h - 22.0f, "Press Esc to return to editor", th->gutter_fg);
        }

        /* Right Pane (Recent Projects) */
        float rx = card_x + left_w + 24.0f;
        float rw = card_w - left_w - 48.0f;
        float ry = card_y + 32.0f;

        font_draw(&app->font, ctx, rx, ry + app->font.ascent, "Recent Projects", th->fg);
        ry += 30.0f;

        if (st->recent_count == 0) {
            font_draw(&app->font, ctx, rx, ry + 40.0f + app->font.ascent,
                      "No recent projects found.", th->gutter_fg);
            font_draw(&app->font, ctx, rx, ry + 62.0f + app->font.ascent,
                      "Click \"Create New Project\" to get started.", th->gutter_fg);
        } else {
            float row_h = 44.0f;
            int max_show = (int)((card_h - 90.0f) / (row_h + 4.0f));
            if (max_show > st->recent_count) max_show = st->recent_count;

            for (int i = 0; i < max_show; ++i) {
                bool row_hov = (mx >= rx && mx < rx + rw && my >= ry && my < ry + row_h);
                if (row_hov) {
                    vk_draw_rounded_rect(&app->draw, rx, ry, rw, row_h, 5.0f, 1.0f, th->overlay_sel);
                }
                font_draw(&app->font, ctx, rx + 12.0f, ry + 18.0f, st->recent_names[i], th->fg);
                font_draw(&app->font, ctx, rx + 12.0f, ry + 34.0f, st->recent_paths[i], th->gutter_fg);
                ry += row_h + 4.0f;
            }
        }
    }

    /* =========================================================================
     * VIEW 2: JETBRAINS & XCODE TEMPLATE GENERATOR
     * ========================================================================= */
    else if (st->mode == WELCOME_MODE_NEW_PROJECT) {
        /* Header Bar */
        float hy = card_y + 16.0f;

        /* Back Button */
        float back_w = 70.0f, back_h = 24.0f;
        float back_x = card_x + 18.0f;
        bool back_hov = (mx >= back_x && mx < back_x + back_w && my >= hy && my < hy + back_h);
        if (back_hov) {
            vk_draw_rounded_rect(&app->draw, back_x, hy, back_w, back_h, 4.0f, 1.0f, th->sidebar_hover);
        }
        font_draw(&app->font, ctx, back_x + 8.0f, hy + (back_h + app->font.ascent + app->font.descent) * 0.5f,
                  "← Back", back_hov ? th->fg : th->gutter_fg);

        /* Title */
        float title_x = card_x + (card_w - font_measure(&app->font, "Choose a Project Template")) * 0.5f;
        font_draw(&app->font, ctx, title_x, hy + (back_h + app->font.ascent + app->font.descent) * 0.5f,
                  "Choose a Project Template", th->fg);

        /* Divider */
        vk_draw_rounded_rect(&app->draw, card_x, card_y + 50.0f, card_w, 1.0f, 0.0f, 1.0f, th->sidebar_edge);

        /* Template Grid (2 rows x 4 columns) */
        float grid_top = card_y + 64.0f;
        float cell_w = (card_w - 48.0f - 24.0f) / 4.0f;
        float cell_h = 42.0f;

        for (int i = 0; i < PROJECT_TEMPLATE_COUNT; ++i) {
            int row = i / 4;
            int col = i % 4;
            float cx_pos = card_x + 24.0f + col * (cell_w + 8.0f);
            float cy_pos = grid_top + row * (cell_h + 8.0f);

            bool is_sel = (st->selected_template == i);
            bool hov = (mx >= cx_pos && mx < cx_pos + cell_w && my >= cy_pos && my < cy_pos + cell_h);

            const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)i);

            if (is_sel) {
                vk_draw_rounded_rect(&app->draw, cx_pos, cy_pos, cell_w, cell_h, 6.0f, 1.0f, th->sidebar_selected);
                vk_draw_rounded_rect(&app->draw, cx_pos, cy_pos, cell_w, cell_h, 6.0f, 1.0f, th->caret);
            } else if (hov) {
                vk_draw_rounded_rect(&app->draw, cx_pos, cy_pos, cell_w, cell_h, 6.0f, 1.0f, th->sidebar_hover);
                vk_draw_rounded_rect(&app->draw, cx_pos, cy_pos, cell_w, cell_h, 6.0f, 1.0f, th->sidebar_edge);
            } else {
                vk_draw_rounded_rect(&app->draw, cx_pos, cy_pos, cell_w, cell_h, 6.0f, 1.0f, th->sidebar_bg);
                vk_draw_rounded_rect(&app->draw, cx_pos, cy_pos, cell_w, cell_h, 6.0f, 1.0f, th->sidebar_edge);
            }

            font_draw(&app->font, ctx, cx_pos + 12.0f, cy_pos + 17.0f, ti->tag, is_sel ? th->caret : th->fg);
            font_draw(&app->font, ctx, cx_pos + 12.0f, cy_pos + 32.0f, ti->standard, th->gutter_fg);
        }

        /* Divider */
        float form_top = grid_top + 2.0f * (cell_h + 8.0f) + 12.0f;
        vk_draw_rounded_rect(&app->draw, card_x + 24.0f, form_top, card_w - 48.0f, 1.0f, 0.0f, 1.0f, th->sidebar_edge);

        /* Project Settings Form */
        const ProjectTemplateInfo *cur_ti = project_template_get_info((ProjectTemplate)st->selected_template);
        float fy = form_top + 18.0f;
        float form_w = card_w - 48.0f;

        font_draw(&app->font, ctx, card_x + 24.0f, fy + app->font.ascent, cur_ti->name, th->fg);
        font_draw(&app->font, ctx, card_x + 24.0f + font_measure(&app->font, cur_ti->name) + 12.0f,
                  fy + app->font.ascent, cur_ti->desc, th->gutter_fg);
        fy += 28.0f;

        /* Field 1: Name */
        float label_w = 110.0f;
        float field_h = 30.0f;

        font_draw(&app->font, ctx, card_x + 24.0f, fy + (field_h + app->font.ascent + app->font.descent) * 0.5f,
                  "Project Name:", th->fg);

        float inp_x = card_x + 24.0f + label_w;
        float inp_w = form_w - label_w;

        bool name_focused = (st->focused_field == 1);
        vk_draw_rounded_rect(&app->draw, inp_x, fy, inp_w, field_h, 4.0f, 1.0f, th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, inp_x, fy, inp_w, field_h, 4.0f, 1.0f,
                             name_focused ? th->caret : th->sidebar_edge);

        float text_b = fy + (field_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, inp_x + 10.0f, text_b, st->project_name, th->fg);
        if (name_focused && st->cursor_blink) {
            float tw = font_measure(&app->font, st->project_name);
            vk_draw_rounded_rect(&app->draw, inp_x + 10.0f + tw, fy + 7.0f, 2.0f, field_h - 14.0f, 0.0f, 1.0f, th->caret);
        }

        fy += field_h + 12.0f;

        /* Field 2: Location */
        font_draw(&app->font, ctx, card_x + 24.0f, fy + (field_h + app->font.ascent + app->font.descent) * 0.5f,
                  "Location:", th->fg);

        float browse_w = 80.0f;
        float loc_w = inp_w - browse_w - 6.0f;
        bool loc_focused = (st->focused_field == 2);

        vk_draw_rounded_rect(&app->draw, inp_x, fy, loc_w, field_h, 4.0f, 1.0f, th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, inp_x, fy, loc_w, field_h, 4.0f, 1.0f,
                             loc_focused ? th->caret : th->sidebar_edge);

        text_b = fy + (field_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, inp_x + 10.0f, text_b, st->parent_location, th->fg);
        if (loc_focused && st->cursor_blink) {
            float tw = font_measure(&app->font, st->parent_location);
            vk_draw_rounded_rect(&app->draw, inp_x + 10.0f + tw, fy + 7.0f, 2.0f, field_h - 14.0f, 0.0f, 1.0f, th->caret);
        }

        /* Browse Button */
        float bx = inp_x + loc_w + 6.0f;
        bool browse_hov = (mx >= bx && mx < bx + browse_w && my >= fy && my < fy + field_h);
        vk_draw_rounded_rect(&app->draw, bx, fy, browse_w, field_h, 4.0f, 1.0f,
                             browse_hov ? th->sidebar_hover : th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, bx, fy, browse_w, field_h, 4.0f, 1.0f, th->sidebar_edge);
        font_draw(&app->font, ctx, bx + 14.0f, text_b, "Browse…", browse_hov ? th->fg : th->gutter_fg);

        /* Footer Bar */
        float foot_y = card_y + card_h - 48.0f;
        vk_draw_rounded_rect(&app->draw, card_x, foot_y - 10.0f, card_w, 1.0f, 0.0f, 1.0f, th->sidebar_edge);

        char spec[256];
        snprintf(spec, sizeof(spec), "Files: %s  •  Toolchain: %s  •  Run: Ctrl+B",
                 cur_ti->entry_file, cur_ti->toolchain);
        font_draw(&app->font, ctx, card_x + 24.0f, foot_y + 16.0f, spec, th->gutter_fg);

        /* Action Buttons */
        float btn_w = 110.0f;
        float btn_h = 30.0f;
        float create_x = card_x + card_w - 24.0f - btn_w;
        float cancel_x = create_x - btn_w - 10.0f;

        /* Cancel Button */
        bool can_hov = (mx >= cancel_x && mx < cancel_x + btn_w && my >= foot_y && my < foot_y + btn_h);
        vk_draw_rounded_rect(&app->draw, cancel_x, foot_y, btn_w, btn_h, 5.0f, 1.0f,
                             can_hov ? th->sidebar_hover : th->sidebar_bg);
        vk_draw_rounded_rect(&app->draw, cancel_x, foot_y, btn_w, btn_h, 5.0f, 1.0f, th->sidebar_edge);
        font_draw(&app->font, ctx, cancel_x + 32.0f, foot_y + (btn_h + app->font.ascent + app->font.descent) * 0.5f,
                  "Cancel", can_hov ? th->fg : th->gutter_fg);

        /* Create Button (Accent) */
        bool cr_hov = (mx >= create_x && mx < create_x + btn_w && my >= foot_y && my < foot_y + btn_h);
        vk_draw_rounded_rect(&app->draw, create_x, foot_y, btn_w, btn_h, 5.0f, 1.0f, th->caret);
        uint8_t cr_fg[4] = { 255, 255, 255, 255 };
        if (cr_hov) {
            cr_fg[0] = 230; cr_fg[1] = 230; cr_fg[2] = 230;
        }
        font_draw(&app->font, ctx, create_x + 18.0f, foot_y + (btn_h + app->font.ascent + app->font.descent) * 0.5f,
                  "Create Project", cr_fg);
    }
}

bool welcome_handle_event(App *app, const SDL_Event *ev)
{
    if (!app || !app->welcome_open) return false;

    WelcomeState *st = &app->welcome;
    float ww = (float)app->vk.swapchain_extent.width;
    float wh = (float)app->vk.swapchain_extent.height;

    float card_w = 780.0f;
    float card_h = 490.0f;
    if (card_w > ww - 40.0f) card_w = ww - 40.0f;
    if (card_h > wh - 40.0f) card_h = wh - 40.0f;

    float card_x = (float)(int)((ww - card_w) * 0.5f);
    float card_y = (float)(int)((wh - card_h) * 0.5f);

    /* 1. Mouse Click Handling */
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float mx = (float)ev->button.x;
        float my = (float)ev->button.y;

        /* Click outside card closes if project open */
        bool inside = (mx >= card_x && mx <= card_x + card_w && my >= card_y && my <= card_y + card_h);
        if (!inside) {
            if (app->tab_count > 0 || app->project_root[0]) {
                welcome_close(app);
                return true;
            }
            return true;
        }

        /* Close Button [✕] in top right */
        float close_x = card_x + card_w - 28.0f;
        float close_y = card_y + 12.0f;
        if (mx >= close_x - 4.0f && mx < close_x + 20.0f && my >= close_y - 2.0f && my < close_y + 20.0f) {
            if (app->tab_count > 0 || app->project_root[0]) {
                welcome_close(app);
            }
            return true;
        }

        /* -------------------------------------------------------------
         * MODE 1: WELCOME HUB CLICKS
         * ------------------------------------------------------------- */
        if (st->mode == WELCOME_MODE_HUB) {
            float left_w = 320.0f;

            if (mx < card_x + left_w) {
                float by = card_y + 36.0f + 32.0f + 20.0f + 44.0f;
                float act_w = left_w - 64.0f;
                float act_h = 56.0f;

                /* Action 1: Create New Project */
                if (mx >= card_x + 32.0f && mx < card_x + 32.0f + act_w && my >= by && my < by + act_h) {
                    st->mode = WELCOME_MODE_NEW_PROJECT;
                    st->focused_field = 1;
                    return true;
                }
                by += act_h + 16.0f;

                /* Action 2: Open Existing Folder */
                if (mx >= card_x + 32.0f && mx < card_x + 32.0f + act_w && my >= by && my < by + act_h) {
                    project_show_folder_dialog(app);
                    return true;
                }
            } else {
                /* Recent Projects on Right */
                float rx = card_x + left_w + 24.0f;
                float rw = card_w - left_w - 48.0f;
                float ry = card_y + 62.0f;
                float row_h = 44.0f;

                int max_show = (int)((card_h - 90.0f) / (row_h + 4.0f));
                if (max_show > st->recent_count) max_show = st->recent_count;

                for (int i = 0; i < max_show; ++i) {
                    if (mx >= rx && mx < rx + rw && my >= ry && my < ry + row_h) {
                        project_set_root(app, st->recent_paths[i]);
                        project_config_detect(&app->project_cfg, app->project_root);
                        welcome_add_recent(st->recent_paths[i]);
                        update_window_title(app);
                        welcome_close(app);
                        return true;
                    }
                    ry += row_h + 4.0f;
                }
            }
            return true;
        }

        /* -------------------------------------------------------------
         * MODE 2: NEW PROJECT GENERATOR CLICKS
         * ------------------------------------------------------------- */
        else if (st->mode == WELCOME_MODE_NEW_PROJECT) {
            float hy = card_y + 16.0f;

            /* Back Button */
            float back_w = 70.0f, back_h = 24.0f;
            float back_x = card_x + 18.0f;
            if (mx >= back_x && mx < back_x + back_w && my >= hy && my < hy + back_h) {
                st->mode = WELCOME_MODE_HUB;
                return true;
            }

            /* Template Grid */
            float grid_top = card_y + 64.0f;
            float cell_w = (card_w - 48.0f - 24.0f) / 4.0f;
            float cell_h = 42.0f;

            for (int i = 0; i < PROJECT_TEMPLATE_COUNT; ++i) {
                int row = i / 4;
                int col = i % 4;
                float cx_pos = card_x + 24.0f + col * (cell_w + 8.0f);
                float cy_pos = grid_top + row * (cell_h + 8.0f);

                if (mx >= cx_pos && mx < cx_pos + cell_w && my >= cy_pos && my < cy_pos + cell_h) {
                    st->selected_template = i;
                    const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)i);
                    snprintf(st->project_name, sizeof(st->project_name), "%s", ti->default_name);
                    st->project_name_len = (int)strlen(st->project_name);
                    return true;
                }
            }

            /* Fields */
            float form_top = grid_top + 2.0f * (cell_h + 8.0f) + 12.0f;
            float fy = form_top + 18.0f + 28.0f;
            float label_w = 110.0f;
            float field_h = 30.0f;
            float inp_x = card_x + 24.0f + label_w;
            float form_w = card_w - 48.0f;
            float inp_w = form_w - label_w;

            /* Field 1: Name */
            if (mx >= inp_x && mx < inp_x + inp_w && my >= fy && my < fy + field_h) {
                st->focused_field = 1;
                return true;
            }
            fy += field_h + 12.0f;

            /* Field 2: Location */
            float browse_w = 80.0f;
            float loc_w = inp_w - browse_w - 6.0f;
            if (mx >= inp_x && mx < inp_x + loc_w && my >= fy && my < fy + field_h) {
                st->focused_field = 2;
                return true;
            }

            /* Browse button */
            float bx = inp_x + loc_w + 6.0f;
            if (mx >= bx && mx < bx + browse_w && my >= fy && my < fy + field_h) {
                SDL_ShowOpenFolderDialog(on_browse_folder, app, app->window,
                                         st->parent_location[0] ? st->parent_location : NULL, false);
                return true;
            }

            /* Footer Buttons: Cancel & Create */
            float foot_y = card_y + card_h - 48.0f;
            float btn_w = 110.0f;
            float btn_h = 30.0f;
            float create_x = card_x + card_w - 24.0f - btn_w;
            float cancel_x = create_x - btn_w - 10.0f;

            if (mx >= cancel_x && mx < cancel_x + btn_w && my >= foot_y && my < foot_y + btn_h) {
                if (app->tab_count > 0 || app->project_root[0]) {
                    welcome_close(app);
                } else {
                    st->mode = WELCOME_MODE_HUB;
                }
                return true;
            }

            if (mx >= create_x && mx < create_x + btn_w && my >= foot_y && my < foot_y + btn_h) {
                welcome_create_project(app);
                return true;
            }

            return true;
        }
    }

    /* 2. Text Input */
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        if (st->mode == WELCOME_MODE_NEW_PROJECT) {
            const char *text = ev->text.text;
            if (st->focused_field == 1) {
                size_t add_len = strlen(text);
                if ((size_t)st->project_name_len + add_len < sizeof(st->project_name) - 1) {
                    memcpy(&st->project_name[st->project_name_len], text, add_len);
                    st->project_name_len += (int)add_len;
                    st->project_name[st->project_name_len] = 0;
                }
                return true;
            } else if (st->focused_field == 2) {
                size_t add_len = strlen(text);
                if ((size_t)st->parent_location_len + add_len < sizeof(st->parent_location) - 1) {
                    memcpy(&st->parent_location[st->parent_location_len], text, add_len);
                    st->parent_location_len += (int)add_len;
                    st->parent_location[st->parent_location_len] = 0;
                }
                return true;
            }
        }
    }

    /* 3. Keyboard Navigation */
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = ev->key.key;

        if (k == SDLK_ESCAPE) {
            if (st->mode == WELCOME_MODE_NEW_PROJECT && (!app->tab_count && !app->project_root[0])) {
                st->mode = WELCOME_MODE_HUB;
                return true;
            }
            if (app->tab_count > 0 || app->project_root[0]) {
                welcome_close(app);
                return true;
            }
        }

        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            if (st->mode == WELCOME_MODE_NEW_PROJECT) {
                welcome_create_project(app);
                return true;
            }
        }

        if (k == SDLK_TAB) {
            st->focused_field = (st->focused_field == 1) ? 2 : 1;
            return true;
        }

        if (k == SDLK_BACKSPACE) {
            if (st->focused_field == 1 && st->project_name_len > 0) {
                st->project_name[--st->project_name_len] = 0;
                return true;
            } else if (st->focused_field == 2 && st->parent_location_len > 0) {
                st->parent_location[--st->parent_location_len] = 0;
                return true;
            }
        }

        if (k == SDLK_UP || k == SDLK_LEFT) {
            if (st->mode == WELCOME_MODE_NEW_PROJECT) {
                st->selected_template = (st->selected_template - 1 + PROJECT_TEMPLATE_COUNT) % PROJECT_TEMPLATE_COUNT;
                const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)st->selected_template);
                snprintf(st->project_name, sizeof(st->project_name), "%s", ti->default_name);
                st->project_name_len = (int)strlen(st->project_name);
                return true;
            }
        }

        if (k == SDLK_DOWN || k == SDLK_RIGHT) {
            if (st->mode == WELCOME_MODE_NEW_PROJECT) {
                st->selected_template = (st->selected_template + 1) % PROJECT_TEMPLATE_COUNT;
                const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)st->selected_template);
                snprintf(st->project_name, sizeof(st->project_name), "%s", ti->default_name);
                st->project_name_len = (int)strlen(st->project_name);
                return true;
            }
        }

        return true;
    }

    return true;
}
