#include <stdio.h>
#include <string.h>

#include "app/tabs.h"

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

static int tab_find_path(App *app, const char *path)
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
