#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>

#include "project/project_tree.h"

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

void project_update_visible(App *app)
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

bool project_load_children(App *app, int parent)
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
