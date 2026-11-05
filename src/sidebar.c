/**
 * sidebar.c – project navigator (folder tree on the left)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>

#include "sidebar.h"
#include "tab_bar.h"
#include "project_icons.h"
#include "theme.h"

typedef struct ProjectScan {
    ProjectEntry *entries;
    size_t count;
    size_t capacity;
    bool allocation_failed;
} ProjectScan;

static SDL_EnumerationResult project_scan_entry(void *userdata, const char *dirname,
                                                const char *filename)
{
    ProjectScan *scan = userdata;
    if (!filename[0] || strcmp(filename, ".") == 0 ||
        strcmp(filename, "..") == 0 || strcmp(filename, ".git") == 0)
        return SDL_ENUM_CONTINUE;

    char path[PROJECT_PATH_MAX];
    int path_len = snprintf(path, sizeof(path), "%s%s", dirname, filename);
    if (path_len < 0 || (size_t)path_len >= sizeof(path)) {
        fprintf(stderr, "Project entry path is too long: %s%s\n", dirname, filename);
        return SDL_ENUM_CONTINUE;
    }

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        fprintf(stderr, "Could not inspect project entry %s: %s\n", path, SDL_GetError());
        return SDL_ENUM_CONTINUE;
    }
    if (info.type != SDL_PATHTYPE_DIRECTORY && info.type != SDL_PATHTYPE_FILE)
        return SDL_ENUM_CONTINUE;

    if (scan->count == scan->capacity) {
        size_t capacity = scan->capacity ? scan->capacity * 2 : 32;
        ProjectEntry *entries = realloc(scan->entries, capacity * sizeof(*entries));
        if (!entries) {
            scan->allocation_failed = true;
            return SDL_ENUM_FAILURE;
        }
        scan->entries = entries;
        scan->capacity = capacity;
    }

    ProjectEntry *entry = &scan->entries[scan->count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", filename);
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->directory = info.type == SDL_PATHTYPE_DIRECTORY;
    entry->child_start = -1;
    return SDL_ENUM_CONTINUE;
}

static int project_entry_compare(const void *a, const void *b)
{
    const ProjectEntry *left = a;
    const ProjectEntry *right = b;
    if (left->directory != right->directory)
        return right->directory - left->directory;
    return strcmp(left->name, right->name);
}

static bool project_reserve_entries(App *app, size_t needed)
{
    if (needed <= app->project_entry_capacity)
        return true;
    size_t capacity = app->project_entry_capacity ? app->project_entry_capacity : 32;
    while (capacity < needed) {
        if (capacity > (size_t)INT_MAX / 2)
            return false;
        capacity *= 2;
    }
    ProjectEntry *entries = realloc(app->project_entries, capacity * sizeof(*entries));
    if (!entries)
        return false;
    app->project_entries = entries;
    app->project_entry_capacity = capacity;
    return true;
}

static void project_collect_visible(App *app, int first, int count)
{
    for (int i = first; i < first + count; ++i) {
        if (app->project_visible_count >= app->project_visible_capacity)
            return;
        app->project_visible[app->project_visible_count++] = i;
        ProjectEntry *entry = &app->project_entries[i];
        if (entry->directory && entry->expanded && entry->loaded)
            project_collect_visible(app, entry->child_start, entry->child_count);
    }
}

static void project_update_visible(App *app)
{
    if (app->project_visible_capacity < app->project_entry_count) {
        int *visible = realloc(app->project_visible,
                               app->project_entry_count * sizeof(*visible));
        if (!visible && app->project_entry_count > 0) {
            fprintf(stderr, "Could not allocate project navigator rows\n");
            app->project_visible_count = 0;
            return;
        }
        app->project_visible = visible;
        app->project_visible_capacity = app->project_entry_count;
    }
    app->project_visible_count = 0;
    if (app->project_root_loaded && app->project_root_expanded)
        project_collect_visible(app, app->project_root_child_start,
                                app->project_root_child_count);
    int max_scroll = (int)app->project_visible_count;
    if (app->project_scroll > max_scroll)
        app->project_scroll = max_scroll;
}

static bool project_load_children(App *app, int parent)
{
    if (parent < 0 ? app->project_root_loaded
                   : app->project_entries[parent].loaded)
        return true;

    const char *directory = parent < 0 ? app->project_root
                                       : app->project_entries[parent].path;
    ProjectScan scan = {0};
    if (!SDL_EnumerateDirectory(directory, project_scan_entry, &scan)) {
        fprintf(stderr, "Could not list project directory %s: %s%s\n",
                directory, SDL_GetError(),
                scan.allocation_failed ? " (out of memory)" : "");
        free(scan.entries);
        return false;
    }
    if (scan.count > 1)
        qsort(scan.entries, scan.count, sizeof(*scan.entries), project_entry_compare);
    if (scan.count > (size_t)INT_MAX - app->project_entry_count ||
        !project_reserve_entries(app, app->project_entry_count + scan.count)) {
        fprintf(stderr, "Could not allocate project navigator entries\n");
        free(scan.entries);
        return false;
    }

    int first = (int)app->project_entry_count;
    for (size_t i = 0; i < scan.count; ++i) {
        ProjectEntry entry = scan.entries[i];
        entry.parent = parent;
        app->project_entries[app->project_entry_count++] = entry;
    }
    free(scan.entries);

    if (parent < 0) {
        app->project_root_child_start = first;
        app->project_root_child_count = (int)scan.count;
        app->project_root_loaded = true;
        app->project_root_expanded = true;
    } else {
        app->project_entries[parent].child_start = first;
        app->project_entries[parent].child_count = (int)scan.count;
        app->project_entries[parent].loaded = true;
    }
    project_update_visible(app);
    return true;
}

bool project_set_root(App *app, const char *path)
{
    if (!path || !path[0])
        return false;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        fprintf(stderr, "Project folder is not a readable directory: %s\n", path);
        return false;
    }

    int path_len = snprintf(app->project_root, sizeof(app->project_root), "%s", path);
    if (path_len < 0 || (size_t)path_len >= sizeof(app->project_root)) {
        fprintf(stderr, "Project folder path is too long\n");
        app->project_root[0] = 0;
        return false;
    }
    while (path_len > 1 && app->project_root[path_len - 1] == '/')
        app->project_root[--path_len] = 0;
    const char *name = strrchr(app->project_root, '/');
    name = name ? name + 1 : app->project_root;
    snprintf(app->project_name, sizeof(app->project_name), "%.*s",
             (int)sizeof(app->project_name) - 1, name[0] ? name : "/");

    app->project_entry_count = 0;
    app->project_root_loaded = false;
    app->project_root_expanded = true;
    app->project_root_child_start = 0;
    app->project_root_child_count = 0;
    app->project_scroll = 0;
    project_update_visible(app);
    return project_load_children(app, -1);
}

bool project_open_initial(App *app, const char *path)
{
    char folder[PROJECT_PATH_MAX];
    if (path && path[0]) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
            return project_set_root(app, path);

        int path_len = snprintf(folder, sizeof(folder), "%s", path);
        if (path_len >= 0 && (size_t)path_len < sizeof(folder)) {
            char *slash = strrchr(folder, '/');
            if (slash) {
                if (slash == folder)
                    slash[1] = 0;
                else
                    *slash = 0;
                return project_set_root(app, folder);
            }
        }
    }

    char *cwd = SDL_GetCurrentDirectory();
    if (!cwd) {
        fprintf(stderr, "Could not determine current directory: %s\n", SDL_GetError());
        return false;
    }
    bool ok = project_set_root(app, cwd);
    SDL_free(cwd);
    return ok;
}

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

static float project_row_height(const App *app)
{
    return app->font.line_height + 5.0f;
}

static float project_tree_top(const App *app, float tab_h)
{
    return tab_h + PROJECT_ROOT_TOP + project_row_height(app) + 2.0f;
}

static Icon *project_file_icon(App *app, const ProjectEntry *entry,
                               uint8_t tint[4])
{
    const Theme *th = theme_current();
    tint[3] = 255;
    if (entry->directory) {
        if (th->syntax[HL_TYPE][3] > 0) {
            memcpy(tint, th->syntax[HL_TYPE], 4);
        } else {
            tint[0] = 229; tint[1] = 192; tint[2] = 123;
        }
        return entry->expanded ? &app->project_folder_open_icon
                               : &app->project_folder_icon;
    } else {
        const char *ext = strrchr(entry->name, '.');
        if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".C") == 0 ||
                    strcmp(ext, ".h") == 0 || strcmp(ext, ".H") == 0)) {
            memcpy(tint, th->syntax[HL_FUNCTION], 4);
            return &app->project_file_code_icon;
        } else if (ext && (strcmp(ext, ".json") == 0 ||
                           strcmp(ext, ".vert") == 0 ||
                           strcmp(ext, ".frag") == 0 ||
                           strcmp(ext, ".lua") == 0 ||
                           strcmp(ext, ".scm") == 0)) {
            memcpy(tint, th->syntax[HL_OPERATOR], 4);
            return &app->project_file_generic_code_icon;
        } else if (ext && (strcmp(ext, ".md") == 0 || strcmp(ext, ".txt") == 0)) {
            memcpy(tint, th->sidebar_muted, 4);
            return &app->project_file_book_icon;
        } else {
            memcpy(tint, th->sidebar_text, 4);
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
    const Theme *th = theme_current();
    const uint8_t *panel    = th->sidebar_bg;
    const uint8_t *edge     = th->sidebar_edge;
    const uint8_t *heading  = th->sidebar_heading;
    const uint8_t *text     = th->sidebar_text;
    const uint8_t *muted    = th->sidebar_muted;
    const uint8_t *selected = th->sidebar_selected;
    const uint8_t *hover    = th->sidebar_hover;
    const uint8_t *active_text = th->caret;
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
        icon_draw(&app->icons, &app->project_plus_icon, ctx,
                  button_x + 6.0f, button_y + 5.0f, 14.0f, 14.0f,
                  button_hovered ? heading : muted);
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
        uint8_t folder_tint[4];
        if (th->syntax[HL_TYPE][3] > 0) {
            memcpy(folder_tint, th->syntax[HL_TYPE], 4);
        } else {
            folder_tint[0] = 229; folder_tint[1] = 192; folder_tint[2] = 123; folder_tint[3] = 255;
        }
        icon_draw(&app->icons, chevron, ctx, 10.0f,
                  (float)(int)(root_y + (row_h - 14.0f) * 0.5f),
                  14.0f, 14.0f, muted);
        icon_draw(&app->icons, &app->project_folder_open_icon, ctx, 27.0f,
                  (float)(int)(root_y + (row_h - 16.0f) * 0.5f),
                  16.0f, 16.0f, folder_tint);
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
    int active_parent = -2;
    if (active && active->path[0]) {
        for (size_t i = 0; i < app->project_entry_count; ++i) {
            if (!app->project_entries[i].directory &&
                strcmp(app->project_entries[i].path, active->path) == 0) {
                active_parent = app->project_entries[i].parent;
                break;
            }
        }
    }
    uint8_t guide[4] = { 70, 75, 84, 30 };
    uint8_t guide_active[4] = { 70, 75, 84, 90 };

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
            vk_draw_rounded_rect(&app->draw, 5.0f, y, sidebar_w - 10.0f, row_h,
                                 4.0f, 1.0f, selected);
        else if (row_hovered)
            vk_draw_rounded_rect(&app->draw, 5.0f, y, sidebar_w - 10.0f, row_h,
                                 4.0f, 1.0f, hover);

        enum { MAX_GUIDE_DEPTH = 64 };
        int ancestors[MAX_GUIDE_DEPTH];
        int depth = 0;
        for (int parent = entry->parent; parent >= 0;
             parent = app->project_entries[parent].parent) {
            if (depth < MAX_GUIDE_DEPTH)
                ancestors[depth] = parent;
            depth++;
        }
        float indent = 12.0f + depth * 14.0f;

        /* ancestors[] runs nearest-first; level d's directory is ancestors[depth-1-d]. */
        for (int d = 0; d < depth && d < MAX_GUIDE_DEPTH; ++d) {
            int dir = (depth - 1 - d) < MAX_GUIDE_DEPTH ? ancestors[depth - 1 - d] : -1;
            float gx = 16.0f + d * 14.0f;
            vk_draw_rounded_rect(&app->draw, gx, y, 1.0f, row_h, 0.0f, 1.0f,
                                 dir == active_parent ? guide_active : guide);
        }

        float baseline = y + (row_h + app->font.ascent + app->font.descent) * 0.5f;
        float chevron_y = (float)(int)(y + (row_h - 12.0f) * 0.5f);
        float glyph_y = (float)(int)(y + (row_h - 16.0f) * 0.5f);
        bool is_expanded_directory = entry->directory && entry->expanded;
        if (entry->directory) {
            if (app->project_icons_ok) {
                Icon *chevron = is_expanded_directory
                    ? &app->project_chevron_down_icon
                    : &app->project_chevron_right_icon;
                icon_draw(&app->icons, chevron, ctx, indent - 2.0f, chevron_y,
                          12.0f, 12.0f, muted);
            } else {
                font_draw(&app->font, ctx, indent, baseline,
                          is_expanded_directory ? "v" : ">", muted);
            }
        }
        float icon_x = indent + 14.0f;
        uint8_t glyph_color[4];
        Icon *glyph = project_file_icon(app, entry, glyph_color);
        if (app->project_icons_ok) {
            icon_draw(&app->icons, glyph, ctx, icon_x, glyph_y,
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

static void on_project_dialog(void *userdata, const char * const *filelist, int filter)
{
    (void)filter;
    App *app = userdata;
    app->project_dialog_open = false;
    if (!filelist || !filelist[0])
        return;
    snprintf(app->pending_project, sizeof(app->pending_project), "%s", filelist[0]);
}

void project_show_folder_dialog(App *app)
{
    if (app->project_dialog_open)
        return;
    app->project_dialog_open = true;
    SDL_ShowOpenFolderDialog(on_project_dialog, app, app->window,
                             app->project_root[0] ? app->project_root : NULL, false);
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
        update_window_title(app);
    }
    return true;
}

bool project_handle_wheel(App *app, float wheel_y, float tab_h,
                          float status_h, float window_h)
{
    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    float rows_top = project_tree_top(app, tab_h);
    if (mouse_x < 0.0f || mouse_x >= PROJECT_SIDEBAR_WIDTH ||
        mouse_y < rows_top || mouse_y >= window_h - status_h)
        return false;

    app->project_scroll -= (int)wheel_y * 3;
    int rows_visible = (int)((window_h - status_h - 4.0f - rows_top) /
                             project_row_height(app));
    int max_scroll = (int)app->project_visible_count - rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (app->project_scroll < 0) app->project_scroll = 0;
    if (app->project_scroll > max_scroll)
        app->project_scroll = max_scroll;
    return true;
}

void project_free(App *app)
{
    free(app->project_entries);
    free(app->project_visible);
    app->project_entries = NULL;
    app->project_visible = NULL;
    app->project_entry_count = 0;
    app->project_entry_capacity = 0;
    app->project_visible_count = 0;
    app->project_visible_capacity = 0;
}
