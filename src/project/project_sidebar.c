#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "app/dialogs.h"
#include "app/tabs.h"
#include "project/project_sidebar.h"
#include "project/project_tree.h"
#include "project_icons.h"

#define PROJECT_HEADER_HEIGHT 28.0f
#define PROJECT_ROOT_TOP 30.0f

static void project_fit_name(App *app, const char *name, float max_width,
                             char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", name);
    if (font_measure(&app->font, out) <= max_width)
        return;

    size_t len = strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xc0) == 0x80)
            len--;
        out[len] = 0;
        if (font_measure(&app->font, out) + font_measure(&app->font, "...") <= max_width)
            break;
    }
    size_t used = strlen(out);
    if (used + 4 <= out_size)
        memcpy(out + used, "...", 4);
}

float project_row_height(const App *app)
{
    return app->font.line_height + 5.0f;
}

float project_tree_top(const App *app, float tab_h)
{
    return tab_h + PROJECT_ROOT_TOP + project_row_height(app) + 2.0f;
}

static Icon *project_file_icon(App *app, const ProjectEntry *entry,
                               uint8_t tint[4])
{
    tint[3] = 255;
    if (entry->directory) {
        tint[0] = 148; tint[1] = 153; tint[2] = 160;
        return entry->expanded ? &app->project_folder_open_icon
                               : &app->project_folder_icon;
    } else {
        tint[0] = 72; tint[1] = 75; tint[2] = 81;
        const char *ext = strrchr(entry->name, '.');
        if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".C") == 0 ||
                    strcmp(ext, ".h") == 0 || strcmp(ext, ".H") == 0)) {
            return &app->project_file_code_icon;
        } else if (ext && (strcmp(ext, ".json") == 0 ||
                           strcmp(ext, ".vert") == 0 ||
                           strcmp(ext, ".frag") == 0)) {
            return &app->project_file_generic_code_icon;
        } else if (ext && strcmp(ext, ".md") == 0) {
            return &app->project_file_book_icon;
        } else {
            return &app->project_file_text_icon;
        }
    }
}

void project_icons_destroy(App *app)
{
    icon_destroy(&app->project_folder_icon, &app->vk);
    icon_destroy(&app->project_folder_open_icon, &app->vk);
    icon_destroy(&app->project_file_code_icon, &app->vk);
    icon_destroy(&app->project_file_text_icon, &app->vk);
    icon_destroy(&app->project_file_generic_code_icon, &app->vk);
    icon_destroy(&app->project_file_book_icon, &app->vk);
    icon_destroy(&app->project_chevron_right_icon, &app->vk);
    icon_destroy(&app->project_chevron_down_icon, &app->vk);
    icon_destroy(&app->project_plus_icon, &app->vk);
    app->project_icons_ok = false;
}

bool project_icons_load(App *app)
{
    bool loaded =
        icon_load_svg_memory(&app->project_folder_icon, &app->vk,
                             project_folder_svg, sizeof(project_folder_svg)) &&
        icon_load_svg_memory(&app->project_folder_open_icon, &app->vk,
                             project_folder_open_svg, sizeof(project_folder_open_svg)) &&
        icon_load_svg_memory(&app->project_file_code_icon, &app->vk,
                             project_file_code_svg, sizeof(project_file_code_svg)) &&
        icon_load_svg_memory(&app->project_file_text_icon, &app->vk,
                             project_file_text_svg, sizeof(project_file_text_svg)) &&
        icon_load_svg_memory(&app->project_file_generic_code_icon, &app->vk,
                             project_file_generic_code_svg,
                             sizeof(project_file_generic_code_svg)) &&
        icon_load_svg_memory(&app->project_file_book_icon, &app->vk,
                             project_file_book_svg, sizeof(project_file_book_svg)) &&
        icon_load_svg_memory(&app->project_chevron_right_icon, &app->vk,
                             project_chevron_right_svg, sizeof(project_chevron_right_svg)) &&
        icon_load_svg_memory(&app->project_chevron_down_icon, &app->vk,
                             project_chevron_down_svg, sizeof(project_chevron_down_svg)) &&
        icon_load_svg_memory(&app->project_plus_icon, &app->vk,
                             project_plus_svg, sizeof(project_plus_svg));
    if (!loaded) {
        fprintf(stderr, "Project navigator icons could not be loaded\n");
        project_icons_destroy(app);
        return false;
    }
    app->project_icons_ok = true;
    return true;
}

void draw_project_sidebar(App *app, VkContext *ctx, float window_h,
                         float tab_h, float status_h)
{
    float bottom = window_h - status_h;
    float sidebar_w = PROJECT_SIDEBAR_WIDTH;
    uint8_t panel[4] = { 249, 249, 250, 255 };
    uint8_t edge[4] = { 218, 219, 222, 255 };
    uint8_t heading[4] = { 105, 107, 112, 255 };
    uint8_t text[4] = { 54, 55, 59, 255 };
    uint8_t muted[4] = { 125, 127, 132, 255 };
    uint8_t selected[4] = { 61, 133, 220, 32 };
    uint8_t hover[4] = { 70, 75, 84, 16 };
    uint8_t active_text[4] = { 31, 94, 164, 255 };
    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    vk_draw_rounded_rect(&app->draw, 0, tab_h, sidebar_w, bottom - tab_h,
                         0.0f, 1.0f, panel);
    vk_draw_rounded_rect(&app->draw, sidebar_w - 1.0f, tab_h, 1.0f,
                         bottom - tab_h, 0.0f, 1.0f, edge);

    float header_baseline = tab_h + (PROJECT_HEADER_HEIGHT +
                                     app->font.ascent + app->font.descent) * 0.5f;
    font_draw(&app->font, ctx, 14.0f, header_baseline, "PROJECT", heading);
    float button_x = sidebar_w - 34.0f;
    float button_y = tab_h + 3.0f;
    bool button_hovered = mouse_x >= button_x && mouse_x < button_x + 26.0f &&
                          mouse_y >= button_y && mouse_y < button_y + 22.0f;
    if (button_hovered)
        vk_draw_rounded_rect(&app->draw, button_x, button_y, 26.0f, 22.0f,
                             4.0f, 1.0f, hover);
    if (app->project_icons_ok) {
        uint8_t tint[4] = { 120, 124, 130, 255 };
        icon_draw(&app->icons, &app->project_plus_icon, ctx,
                  button_x + 6.0f, button_y + 5.0f, 14.0f, 14.0f, tint);
    } else {
        font_draw(&app->font, ctx, button_x + 8.0f, header_baseline, "+", muted);
    }

    float root_y = tab_h + PROJECT_ROOT_TOP;
    float row_h = project_row_height(app);
    float root_baseline = root_y + (row_h + app->font.ascent + app->font.descent) * 0.5f;
    bool root_hovered = mouse_x >= 4.0f && mouse_x < sidebar_w - 4.0f &&
                        mouse_y >= root_y && mouse_y < root_y + row_h;
    if (root_hovered)
        vk_draw_rounded_rect(&app->draw, 5.0f, root_y, sidebar_w - 10.0f, row_h,
                             4.0f, 1.0f, hover);
    if (app->project_icons_ok) {
        Icon *chevron = app->project_root_expanded ? &app->project_chevron_down_icon
                                                  : &app->project_chevron_right_icon;
        uint8_t chevron_tint[4] = { 120, 122, 128, 255 };
        uint8_t folder_tint[4] = { 148, 153, 160, 255 };
        icon_draw(&app->icons, chevron, ctx, 10.0f, root_y + 4.0f,
                  14.0f, 14.0f, chevron_tint);
        icon_draw(&app->icons, &app->project_folder_open_icon, ctx,
                  27.0f, root_y + 3.0f, 16.0f, 16.0f, folder_tint);
    } else {
        font_draw(&app->font, ctx, 12.0f, root_baseline,
                  app->project_root_expanded ? "v" : ">", muted);
        uint8_t root_icon[4] = { 196, 148, 68, 255 };
        font_draw(&app->font, ctx, 27.0f, root_baseline, "D", root_icon);
    }
    char root_label[256];
    project_fit_name(app, app->project_name[0] ? app->project_name : "No project open",
                     sidebar_w - 70.0f, root_label, sizeof(root_label));
    font_draw(&app->font, ctx, 45.0f, root_baseline, root_label, text);

    float rows_top = project_tree_top(app, tab_h);
    float rows_bottom = bottom - 4.0f;
    int rows_visible = (int)((rows_bottom - rows_top) / row_h);
    if (rows_visible < 0) rows_visible = 0;
    int max_scroll = (int)app->project_visible_count - rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (app->project_scroll > max_scroll)
        app->project_scroll = max_scroll;

    Editor *active = app_ed(app);
    for (int row = 0; row < rows_visible; ++row) {
        int visible_index = app->project_scroll + row;
        if ((size_t)visible_index >= app->project_visible_count)
            break;
        ProjectEntry *entry = &app->project_entries[app->project_visible[visible_index]];
        float y = rows_top + row * row_h;
        bool is_active = active && active->path[0] && strcmp(active->path, entry->path) == 0;
        bool row_hovered = mouse_x >= 4.0f && mouse_x < sidebar_w - 4.0f &&
                           mouse_y >= y && mouse_y < y + row_h;
        if (is_active)
            vk_draw_rounded_rect(&app->draw, 5.0f, y,
                                 sidebar_w - 10.0f, row_h, 4.0f, 1.0f, selected);
        else if (row_hovered)
            vk_draw_rounded_rect(&app->draw, 5.0f, y,
                                 sidebar_w - 10.0f, row_h, 4.0f, 1.0f, hover);

        int depth = 0;
        for (int parent = entry->parent; parent >= 0;
             parent = app->project_entries[parent].parent)
            depth++;
        float indent = 12.0f + depth * 14.0f;

        float baseline = y + (row_h + app->font.ascent + app->font.descent) * 0.5f;
        bool is_expanded_directory = entry->directory && entry->expanded;
        if (entry->directory) {
            if (app->project_icons_ok) {
                Icon *chevron = is_expanded_directory
                    ? &app->project_chevron_down_icon
                    : &app->project_chevron_right_icon;
                uint8_t chevron_tint[4] = { 132, 134, 139, 255 };
                icon_draw(&app->icons, chevron, ctx, indent - 2.0f, y + 3.0f,
                          12.0f, 12.0f, chevron_tint);
            } else {
                font_draw(&app->font, ctx, indent, baseline,
                          is_expanded_directory ? "v" : ">", muted);
            }
        }
        float icon_x = indent + 14.0f;
        uint8_t glyph_color[4];
        Icon *glyph = project_file_icon(app, entry, glyph_color);
        if (app->project_icons_ok) {
            icon_draw(&app->icons, glyph, ctx, icon_x, y + 1.0f,
                      16.0f, 16.0f, glyph_color);
        } else {
            font_draw(&app->font, ctx, icon_x, baseline,
                      entry->directory ? "D" : "-", glyph_color);
        }
        float label_x = icon_x + 19.0f;
        char label[256];
        project_fit_name(app, entry->name, sidebar_w - label_x - 12.0f,
                         label, sizeof(label));
        font_draw(&app->font, ctx, label_x, baseline, label,
                  is_active ? active_text : text);
    }
}

bool project_handle_click(App *app, float x, float y, float tab_h,
                          float status_h, float window_h)
{
    if (x < 0.0f || x >= PROJECT_SIDEBAR_WIDTH ||
        y < tab_h || y >= window_h - status_h)
        return false;

    float button_x = PROJECT_SIDEBAR_WIDTH - 34.0f;
    if (y >= tab_h + 3.0f && y < tab_h + 25.0f && x >= button_x) {
        project_show_folder_dialog(app);
        return true;
    }

    float row_h = project_row_height(app);
    float root_y = tab_h + PROJECT_ROOT_TOP;
    if (y < root_y)
        return true;
    if (y < root_y + row_h) {
        app->project_root_expanded = !app->project_root_expanded;
        project_update_visible(app);
        return true;
    }

    float rows_top = project_tree_top(app, tab_h);
    if (y < rows_top || !app->project_root_expanded)
        return true;
    int rows_visible = (int)((window_h - status_h - 4.0f - rows_top) / row_h);
    if (rows_visible < 0) rows_visible = 0;
    int max_scroll = (int)app->project_visible_count - rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (app->project_scroll > max_scroll)
        app->project_scroll = max_scroll;

    int row = (int)((y - rows_top) / row_h);
    int visible_index = app->project_scroll + row;
    if (row < 0 || row >= rows_visible ||
        visible_index < 0 || (size_t)visible_index >= app->project_visible_count)
        return true;

    int index = app->project_visible[visible_index];
    ProjectEntry *entry = &app->project_entries[index];
    if (entry->directory) {
        entry->expanded = !entry->expanded;
        if (entry->expanded && !project_load_children(app, index))
            entry->expanded = false;
        project_update_visible(app);
    } else {
        tab_add(app, entry->path, NULL);
        tab_update_title(app);
    }
    return true;
}
