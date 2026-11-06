/**
 * welcome.c – JetBrains / Xcode style Welcome & Project Generator Screen for cgui
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

    /* Write back with new path at top */
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

        /* Verify folder exists */
        SDL_PathInfo info;
        if (SDL_GetPathInfo(line, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            snprintf(st->recent_paths[st->recent_count], sizeof(st->recent_paths[0]), "%s", line);

            /* Extract base name */
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

    st->active_tab = WELCOME_TAB_NEW_PROJECT;
    st->selected_template = PROJECT_TEMPLATE_C_MESON;
    st->focused_field = 1; /* Name field focused by default */

    const ProjectTemplateInfo *info = project_template_get_info(st->selected_template);
    snprintf(st->project_name, sizeof(st->project_name), "%s", info->default_name);
    st->project_name_len = (int)strlen(st->project_name);

    /* Default location: ~/Projects or HOME */
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
        snprintf(st->status_msg, sizeof(st->status_msg), "Error: Name and Location cannot be empty.");
        st->status_ms = SDL_GetTicks();
        return false;
    }

    char target_dir[1024];
    snprintf(target_dir, sizeof(target_dir), "%s/%s", st->parent_location, st->project_name);

    /* Check if target folder already exists and is non-empty */
    SDL_PathInfo info;
    if (SDL_GetPathInfo(target_dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        /* Already exists, scaffold files directly inside */
    } else {
        (void)mkdir(target_dir, 0755);
    }

    /* Scaffold files */
    if (!project_scaffold(target_dir, (ProjectTemplate)st->selected_template, st->project_name)) {
        snprintf(st->status_msg, sizeof(st->status_msg), "Error creating project files.");
        st->status_ms = SDL_GetTicks();
        return false;
    }

    /* Add to recents */
    welcome_add_recent(target_dir);

    /* Open in IDE */
    project_set_root(app, target_dir);
    project_config_detect(&app->project_cfg, app->project_root);

    /* Open primary entry file in new tab */
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
    Theme *th = theme_current();

    const uint8_t *window_bg = th->bg;
    const uint8_t *panel_bg  = th->sidebar_bg;
    const uint8_t *border    = th->sidebar_edge;

    float ww = (float)ctx->swapchain_extent.width;
    float wh = (float)ctx->swapchain_extent.height;

    /* Cursor blink */
    uint64_t now = SDL_GetTicks();
    if (now - st->last_blink_ms >= 500) {
        st->cursor_blink = !st->cursor_blink;
        st->last_blink_ms = now;
    }

    /* Background */
    vk_draw_rounded_rect(&app->draw, 0.0f, 0.0f, ww, wh, 0.0f, 1.0f, window_bg);

    float mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    /* Top Navigation Bar */
    float top_h = 50.0f;
    vk_draw_rounded_rect(&app->draw, 0.0f, 0.0f, ww, top_h, 0.0f, 1.0f, panel_bg);
    vk_draw_rounded_rect(&app->draw, 0.0f, top_h - 1.0f, ww, 1.0f, 0.0f, 1.0f, border);

    /* If user has tabs open or active project, show "Back to Editor" button */
    bool can_go_back = (app->tab_count > 0 || app->project_root[0]);
    if (can_go_back) {
        float btn_w = 140.0f, btn_h = 28.0f;
        float bx = 16.0f, by = (top_h - btn_h) * 0.5f;
        bool btn_hover = (mx >= bx && mx < bx + btn_w && my >= by && my < by + btn_h);
        vk_draw_rounded_rect(&app->draw, bx, by, btn_w, btn_h, 5.0f, 1.0f,
                             btn_hover ? th->sidebar_hover : window_bg);
        float baseline = by + (btn_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, bx + 10.0f, baseline, "← Back to Editor",
                  btn_hover ? th->sidebar_text : th->sidebar_muted);
    }

    /* Branding Title */
    float brand_x = can_go_back ? 175.0f : 24.0f;
    float brand_baseline = (top_h + app->font.ascent + app->font.descent) * 0.5f;
    font_draw(&app->font, ctx, brand_x, brand_baseline, "cgui", th->sidebar_text);
    font_draw(&app->font, ctx, brand_x + 40.0f, brand_baseline, "v0.1.0  |  Project & Workspace Hub", th->sidebar_muted);

    /* Layout Geometry: Left Rail (240px) + Right Canvas */
    float rail_w = 250.0f;
    float body_y = top_h;
    float body_h = wh - top_h;

    /* Left Rail Background */
    vk_draw_rounded_rect(&app->draw, 0.0f, body_y, rail_w, body_h, 0.0f, 1.0f, panel_bg);
    vk_draw_rounded_rect(&app->draw, rail_w - 1.0f, body_y, 1.0f, body_h, 0.0f, 1.0f, border);

    /* Left Rail Top Tabs: [ ⚡ New Project ] and [ 🕒 Recent Projects ] */
    float nav_y = body_y + 16.0f;
    float nav_item_h = 34.0f;

    /* 1. New Project Tab Button */
    {
        bool is_act = (st->active_tab == WELCOME_TAB_NEW_PROJECT);
        bool hov = (mx >= 12.0f && mx < rail_w - 12.0f && my >= nav_y && my < nav_y + nav_item_h);
        if (is_act) {
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, nav_item_h, 6.0f, 1.0f, th->sidebar_selected);
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y + 6.0f, 3.0f, nav_item_h - 12.0f, 1.5f, 1.0f, th->caret);
        } else if (hov) {
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, nav_item_h, 6.0f, 1.0f, th->sidebar_hover);
        }
        float b = nav_y + (nav_item_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, 26.0f, b, "+ New Project", is_act ? th->sidebar_text : th->sidebar_muted);
    }
    nav_y += nav_item_h + 4.0f;

    /* 2. Recent Projects Tab Button */
    {
        bool is_act = (st->active_tab == WELCOME_TAB_RECENTS);
        bool hov = (mx >= 12.0f && mx < rail_w - 12.0f && my >= nav_y && my < nav_y + nav_item_h);
        if (is_act) {
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, nav_item_h, 6.0f, 1.0f, th->sidebar_selected);
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y + 6.0f, 3.0f, nav_item_h - 12.0f, 1.5f, 1.0f, th->caret);
        } else if (hov) {
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, nav_item_h, 6.0f, 1.0f, th->sidebar_hover);
        }
        float b = nav_y + (nav_item_h + app->font.ascent + app->font.descent) * 0.5f;
        char rec_label[64];
        snprintf(rec_label, sizeof(rec_label), "Recent Projects (%d)", st->recent_count);
        font_draw(&app->font, ctx, 26.0f, b, rec_label, is_act ? th->sidebar_text : th->sidebar_muted);
    }
    nav_y += nav_item_h + 4.0f;

    /* 3. Open Existing Folder Button */
    {
        bool hov = (mx >= 12.0f && mx < rail_w - 12.0f && my >= nav_y && my < nav_y + nav_item_h);
        if (hov) {
            vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, nav_item_h, 6.0f, 1.0f, th->sidebar_hover);
        }
        float b = nav_y + (nav_item_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, 26.0f, b, "Open Folder…", th->sidebar_muted);
    }
    nav_y += nav_item_h + 16.0f;

    /* Divider */
    vk_draw_rounded_rect(&app->draw, 16.0f, nav_y, rail_w - 32.0f, 1.0f, 0.0f, 1.0f, border);
    nav_y += 12.0f;

    /* Template List (shown when in New Project mode) */
    if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
        float b = nav_y + app->font.ascent;
        font_draw(&app->font, ctx, 20.0f, b, "GENERATORS", th->sidebar_muted);
        nav_y += 24.0f;

        float tmpl_h = 32.0f;
        for (int i = 0; i < PROJECT_TEMPLATE_COUNT; ++i) {
            const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)i);
            bool is_sel = (st->selected_template == i);
            bool hov = (mx >= 12.0f && mx < rail_w - 12.0f && my >= nav_y && my < nav_y + tmpl_h);

            if (is_sel) {
                vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, tmpl_h, 5.0f, 1.0f, th->sidebar_selected);
            } else if (hov) {
                vk_draw_rounded_rect(&app->draw, 12.0f, nav_y, rail_w - 24.0f, tmpl_h, 5.0f, 1.0f, th->sidebar_hover);
            }

            float row_b = nav_y + (tmpl_h + app->font.ascent + app->font.descent) * 0.5f;

            /* Tag pill */
            char pill[16];
            snprintf(pill, sizeof(pill), "[%s]", ti->tag);
            font_draw(&app->font, ctx, 20.0f, row_b, pill, is_sel ? th->caret : th->sidebar_muted);

            /* Name */
            font_draw(&app->font, ctx, 84.0f, row_b, ti->name, is_sel ? th->sidebar_text : th->sidebar_muted);

            nav_y += tmpl_h + 2.0f;
        }
    }

    /* RIGHT MAIN CONTENT AREA */
    float cx = rail_w + 32.0f;
    float cw = ww - rail_w - 64.0f;
    if (cw < 400.0f) cw = 400.0f;
    float cy = body_y + 24.0f;

    if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
        const ProjectTemplateInfo *cur_ti = project_template_get_info((ProjectTemplate)st->selected_template);

        /* Section Title & Description */
        float b = cy + app->font.ascent;
        font_draw(&app->font, ctx, cx, b, cur_ti->name, th->sidebar_text);
        cy += 26.0f;
        b = cy + app->font.ascent;
        font_draw(&app->font, ctx, cx, b, cur_ti->desc, th->sidebar_muted);
        cy += 36.0f;

        /* Form Card */
        float card_w = cw < 680.0f ? cw : 680.0f;

        /* Field 1: Project Name */
        b = cy + app->font.ascent;
        font_draw(&app->font, ctx, cx, b, "Project Name", th->sidebar_text);
        cy += 20.0f;

        float field_h = 34.0f;
        bool name_focused = (st->focused_field == 1);
        vk_draw_rounded_rect(&app->draw, cx, cy, card_w, field_h, 5.0f, 1.0f, panel_bg);
        vk_draw_rounded_rect(&app->draw, cx, cy, card_w, field_h, 5.0f, 1.0f,
                             name_focused ? th->caret : border);

        float text_b = cy + (field_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, cx + 12.0f, text_b, st->project_name, th->sidebar_text);
        if (name_focused && st->cursor_blink) {
            float text_w = font_measure(&app->font, st->project_name);
            vk_draw_rounded_rect(&app->draw, cx + 12.0f + text_w, cy + 8.0f, 2.0f, field_h - 16.0f,
                                 0.0f, 1.0f, th->caret);
        }
        cy += field_h + 20.0f;

        /* Field 2: Location */
        b = cy + app->font.ascent;
        font_draw(&app->font, ctx, cx, b, "Location", th->sidebar_text);
        cy += 20.0f;

        float browse_btn_w = 90.0f;
        float loc_input_w = card_w - browse_btn_w - 8.0f;
        bool loc_focused = (st->focused_field == 2);
        vk_draw_rounded_rect(&app->draw, cx, cy, loc_input_w, field_h, 5.0f, 1.0f, panel_bg);
        vk_draw_rounded_rect(&app->draw, cx, cy, loc_input_w, field_h, 5.0f, 1.0f,
                             loc_focused ? th->caret : border);

        text_b = cy + (field_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, cx + 12.0f, text_b, st->parent_location, th->sidebar_text);
        if (loc_focused && st->cursor_blink) {
            float text_w = font_measure(&app->font, st->parent_location);
            vk_draw_rounded_rect(&app->draw, cx + 12.0f + text_w, cy + 8.0f, 2.0f, field_h - 16.0f,
                                 0.0f, 1.0f, th->caret);
        }

        /* Browse Button */
        float bx = cx + loc_input_w + 8.0f;
        bool browse_hov = (mx >= bx && mx < bx + browse_btn_w && my >= cy && my < cy + field_h);
        vk_draw_rounded_rect(&app->draw, bx, cy, browse_btn_w, field_h, 5.0f, 1.0f,
                             browse_hov ? th->sidebar_hover : panel_bg);
        vk_draw_rounded_rect(&app->draw, bx, cy, browse_btn_w, field_h, 5.0f, 1.0f, border);
        font_draw(&app->font, ctx, bx + 16.0f, text_b, "Browse…",
                  browse_hov ? th->sidebar_text : th->sidebar_muted);

        cy += field_h + 24.0f;

        /* Summary / Blueprint Card */
        float info_h = 135.0f;
        vk_draw_rounded_rect(&app->draw, cx, cy, card_w, info_h, 6.0f, 1.0f, panel_bg);
        vk_draw_rounded_rect(&app->draw, cx, cy, card_w, info_h, 6.0f, 1.0f, border);

        float iy = cy + 14.0f;
        char buf[256];

        snprintf(buf, sizeof(buf), "Toolchain:      %s", cur_ti->toolchain);
        font_draw(&app->font, ctx, cx + 16.0f, iy + app->font.ascent, buf, th->sidebar_text);
        iy += 24.0f;

        snprintf(buf, sizeof(buf), "Standard:       %s", cur_ti->standard);
        font_draw(&app->font, ctx, cx + 16.0f, iy + app->font.ascent, buf, th->sidebar_text);
        iy += 24.0f;

        snprintf(buf, sizeof(buf), "Entry Point:    %s", cur_ti->entry_file);
        font_draw(&app->font, ctx, cx + 16.0f, iy + app->font.ascent, buf, th->sidebar_text);
        iy += 24.0f;

        snprintf(buf, sizeof(buf), "Shortcut:       Ctrl+B (Build)  |  Shift+F10 (Run in Terminal)");
        font_draw(&app->font, ctx, cx + 16.0f, iy + app->font.ascent, buf, th->caret);

        cy += info_h + 28.0f;

        /* Status error or notice */
        if (st->status_msg[0] && SDL_GetTicks() - st->status_ms < 6000) {
            uint8_t warn_col[4] = { 235, 87, 87, 255 };
            font_draw(&app->font, ctx, cx, cy + app->font.ascent, st->status_msg, warn_col);
        }

        /* Action Buttons */
        float btn_w = 140.0f;
        float btn_h = 36.0f;
        float create_x = cx + card_w - btn_w;
        float cancel_x = create_x - btn_w - 12.0f;

        /* Cancel / Back button */
        bool can_hov = (mx >= cancel_x && mx < cancel_x + btn_w && my >= cy && my < cy + btn_h);
        vk_draw_rounded_rect(&app->draw, cancel_x, cy, btn_w, btn_h, 6.0f, 1.0f,
                             can_hov ? th->sidebar_hover : panel_bg);
        vk_draw_rounded_rect(&app->draw, cancel_x, cy, btn_w, btn_h, 6.0f, 1.0f, border);
        float btn_b = cy + (btn_h + app->font.ascent + app->font.descent) * 0.5f;
        font_draw(&app->font, ctx, cancel_x + 36.0f, btn_b, "Cancel",
                  can_hov ? th->sidebar_text : th->sidebar_muted);

        /* Create Button (Accented) */
        bool cr_hov = (mx >= create_x && mx < create_x + btn_w && my >= cy && my < cy + btn_h);
        vk_draw_rounded_rect(&app->draw, create_x, cy, btn_w, btn_h, 6.0f, 1.0f, th->caret);
        uint8_t cr_text[4] = { 255, 255, 255, 255 };
        if (cr_hov) {
            cr_text[0] = 230; cr_text[1] = 230; cr_text[2] = 230;
        }
        font_draw(&app->font, ctx, create_x + 22.0f, btn_b, "Create Project", cr_text);
    }
    else if (st->active_tab == WELCOME_TAB_RECENTS) {
        /* Recent Projects View */
        float b = cy + app->font.ascent;
        font_draw(&app->font, ctx, cx, b, "Recent Projects", th->sidebar_text);
        cy += 24.0f;
        b = cy + app->font.ascent;
        font_draw(&app->font, ctx, cx, b, "Click any project to load it into the workspace.", th->sidebar_muted);
        cy += 30.0f;

        if (st->recent_count == 0) {
            b = cy + 40.0f + app->font.ascent;
            font_draw(&app->font, ctx, cx, b, "No recent projects found. Click 'New Project' on the left to start.", th->sidebar_muted);
        } else {
            float row_w = cw < 720.0f ? cw : 720.0f;
            float row_h = 44.0f;

            for (int i = 0; i < st->recent_count; ++i) {
                bool hov = (mx >= cx && mx < cx + row_w && my >= cy && my < cy + row_h);
                if (hov) {
                    vk_draw_rounded_rect(&app->draw, cx, cy, row_w, row_h, 6.0f, 1.0f, th->sidebar_hover);
                } else {
                    vk_draw_rounded_rect(&app->draw, cx, cy, row_w, row_h, 6.0f, 1.0f, panel_bg);
                }
                vk_draw_rounded_rect(&app->draw, cx, cy, row_w, row_h, 6.0f, 1.0f, border);

                float nb = cy + 16.0f;
                font_draw(&app->font, ctx, cx + 16.0f, nb, st->recent_names[i], th->sidebar_text);

                float pb = cy + 34.0f;
                font_draw(&app->font, ctx, cx + 16.0f, pb, st->recent_paths[i], th->sidebar_muted);

                cy += row_h + 6.0f;
            }
        }
    }
}

bool welcome_handle_event(App *app, const SDL_Event *ev)
{
    if (!app || !app->welcome_open) return false;

    WelcomeState *st = &app->welcome;
    float ww = (float)app->vk.swapchain_extent.width;
    (void)ww;

    float rail_w = 250.0f;
    float top_h = 50.0f;

    /* 1. MOUSE CLICKS */
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float mx = (float)ev->button.x;
        float my = (float)ev->button.y;

        /* Check Top "Back to Editor" button */
        bool can_go_back = (app->tab_count > 0 || app->project_root[0]);
        if (can_go_back) {
            float btn_w = 140.0f, btn_h = 28.0f;
            float bx = 16.0f, by = (top_h - btn_h) * 0.5f;
            if (mx >= bx && mx < bx + btn_w && my >= by && my < by + btn_h) {
                welcome_close(app);
                return true;
            }
        }

        /* Check Left Rail Tabs */
        if (mx < rail_w) {
            float nav_y = top_h + 16.0f;
            float nav_item_h = 34.0f;

            /* Tab 1: New Project */
            if (my >= nav_y && my < nav_y + nav_item_h) {
                st->active_tab = WELCOME_TAB_NEW_PROJECT;
                return true;
            }
            nav_y += nav_item_h + 4.0f;

            /* Tab 2: Recent Projects */
            if (my >= nav_y && my < nav_y + nav_item_h) {
                st->active_tab = WELCOME_TAB_RECENTS;
                welcome_load_recents(st);
                return true;
            }
            nav_y += nav_item_h + 4.0f;

            /* Tab 3: Open Existing Folder */
            if (my >= nav_y && my < nav_y + nav_item_h) {
                project_show_folder_dialog(app);
                return true;
            }
            nav_y += nav_item_h + 28.0f;

            /* Template Rows */
            if (st->active_tab == WELCOME_TAB_NEW_PROJECT && my >= nav_y) {
                float tmpl_h = 32.0f;
                int idx = (int)((my - nav_y) / (tmpl_h + 2.0f));
                if (idx >= 0 && idx < PROJECT_TEMPLATE_COUNT) {
                    st->selected_template = idx;
                    const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)idx);
                    snprintf(st->project_name, sizeof(st->project_name), "%s", ti->default_name);
                    st->project_name_len = (int)strlen(st->project_name);
                    return true;
                }
            }
            return true;
        }

        /* Check Right Main Area */
        float cx = rail_w + 32.0f;
        float cw = 680.0f;
        float cy = top_h + 24.0f;

        if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
            cy += 62.0f; /* past title/desc */

            /* Field 1: Name */
            float field_h = 34.0f;
            cy += 20.0f;
            if (mx >= cx && mx < cx + cw && my >= cy && my < cy + field_h) {
                st->focused_field = 1;
                return true;
            }
            cy += field_h + 20.0f;

            /* Field 2: Location */
            cy += 20.0f;
            float browse_btn_w = 90.0f;
            float loc_input_w = cw - browse_btn_w - 8.0f;
            if (mx >= cx && mx < cx + loc_input_w && my >= cy && my < cy + field_h) {
                st->focused_field = 2;
                return true;
            }

            /* Browse button */
            float bx = cx + loc_input_w + 8.0f;
            if (mx >= bx && mx < bx + browse_btn_w && my >= cy && my < cy + field_h) {
                SDL_ShowOpenFolderDialog(on_browse_folder, app, app->window,
                                         st->parent_location[0] ? st->parent_location : NULL, false);
                return true;
            }

            cy += field_h + 24.0f + 135.0f + 28.0f;

            /* Action Buttons: Cancel and Create */
            float btn_w = 140.0f;
            float btn_h = 36.0f;
            float create_x = cx + cw - btn_w;
            float cancel_x = create_x - btn_w - 12.0f;

            if (mx >= cancel_x && mx < cancel_x + btn_w && my >= cy && my < cy + btn_h) {
                if (can_go_back) welcome_close(app);
                return true;
            }

            if (mx >= create_x && mx < create_x + btn_w && my >= cy && my < cy + btn_h) {
                welcome_create_project(app);
                return true;
            }
        }
        else if (st->active_tab == WELCOME_TAB_RECENTS) {
            cy += 54.0f;
            float row_w = cw;
            float row_h = 44.0f;
            for (int i = 0; i < st->recent_count; ++i) {
                if (mx >= cx && mx < cx + row_w && my >= cy && my < cy + row_h) {
                    project_set_root(app, st->recent_paths[i]);
                    project_config_detect(&app->project_cfg, app->project_root);
                    welcome_add_recent(st->recent_paths[i]);
                    update_window_title(app);
                    welcome_close(app);
                    return true;
                }
                cy += row_h + 6.0f;
            }
        }

        return true;
    }

    /* 2. TEXT INPUT */
    if (ev->type == SDL_EVENT_TEXT_INPUT) {
        if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
            const char *text = ev->text.text;
            if (st->focused_field == 1) {
                /* Project Name */
                size_t add_len = strlen(text);
                if ((size_t)st->project_name_len + add_len < sizeof(st->project_name) - 1) {
                    memcpy(&st->project_name[st->project_name_len], text, add_len);
                    st->project_name_len += (int)add_len;
                    st->project_name[st->project_name_len] = 0;
                }
                return true;
            } else if (st->focused_field == 2) {
                /* Location */
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

    /* 3. KEYBOARD SHORTCUTS */
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = ev->key.key;

        if (k == SDLK_ESCAPE) {
            if (app->tab_count > 0 || app->project_root[0]) {
                welcome_close(app);
                return true;
            }
        }

        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
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

        if (k == SDLK_UP) {
            if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
                st->selected_template = (st->selected_template - 1 + PROJECT_TEMPLATE_COUNT) % PROJECT_TEMPLATE_COUNT;
                const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)st->selected_template);
                snprintf(st->project_name, sizeof(st->project_name), "%s", ti->default_name);
                st->project_name_len = (int)strlen(st->project_name);
                return true;
            }
        }

        if (k == SDLK_DOWN) {
            if (st->active_tab == WELCOME_TAB_NEW_PROJECT) {
                st->selected_template = (st->selected_template + 1) % PROJECT_TEMPLATE_COUNT;
                const ProjectTemplateInfo *ti = project_template_get_info((ProjectTemplate)st->selected_template);
                snprintf(st->project_name, sizeof(st->project_name), "%s", ti->default_name);
                st->project_name_len = (int)strlen(st->project_name);
                return true;
            }
        }

        return true;
    }

    return true; /* consume all events while welcome is open */
}
