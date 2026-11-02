#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_messagebox.h>

#include "app/dialogs.h"
#include "app/tabs.h"

static void on_open_dialog(void *userdata, const char * const *filelist, int filter)
{
    (void)filter;
    App *app = userdata;
    if (!filelist || !filelist[0]) return;
    snprintf(app->pending_open, sizeof(app->pending_open), "%s", filelist[0]);
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

void file_open_dialog(App *app)
{
    static const SDL_DialogFileFilter filters[] = {
        { "Source / text", "c;h;cpp;hpp;cc;hh;py;rs;go;js;ts;txt;md;json;toml" },
        { "All files", "*" },
    };
    SDL_ShowOpenFileDialog(on_open_dialog, app, app->window,
                           filters, 2, NULL, false);
}

void file_save_as_dialog(App *app)
{
    show_save_as_dialog(app, app->tab_active, SAVE_INTENT_NORMAL);
}

void project_show_folder_dialog(App *app)
{
    if (app->project_dialog_open)
        return;
    app->project_dialog_open = true;
    SDL_ShowOpenFolderDialog(on_project_dialog, app, app->window,
                             app->project_root[0] ? app->project_root : NULL, false);
}

void tab_close_at(App *app, int index)
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

void continue_quit(App *app)
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
