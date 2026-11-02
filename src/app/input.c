#include "app/dialogs.h"
#include "app/input.h"
#include "app/overlay_input.h"
#include "app/tabs.h"
#include "project/project_sidebar.h"
#include "project/project_tree.h"

static bool handle_shortcut(App *app, const SDL_KeyboardEvent *key)
{
    SDL_Keymod mod = key->mod;
    bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    bool shift = (mod & SDL_KMOD_SHIFT) != 0;

    if (key->key == SDLK_F1) {
        app->picker_open = !app->picker_open;
        app->picker_sel = app->font_index;
        return true;
    }
    if (app->picker_open || !ctrl)
        return false;

    switch (key->key) {
    case SDLK_O:
        if (shift)
            project_show_folder_dialog(app);
        else
            file_open_dialog(app);
        return true;
    case SDLK_F:
        app->find_open = true;
        app->replace_mode = false;
        app->find_field = 0;
        app->picker_open = false;
        app->find_status[0] = 0;
        return true;
    case SDLK_H:
        app->find_open = true;
        app->replace_mode = true;
        app->find_field = 0;
        app->picker_open = false;
        app->find_status[0] = 0;
        return true;
    case SDLK_N:
        tab_add(app, NULL, "");
        return true;
    case SDLK_W:
        tab_close_at(app, app->tab_active);
        return true;
    case SDLK_TAB:
        if (app->tab_count > 0) {
            app->tab_active = (app->tab_active + (shift ? app->tab_count - 1 : 1)) % app->tab_count;
            tab_update_title(app);
        }
        return true;
    case SDLK_G:
        app->goto_open = true;
        app->find_open = false;
        app->picker_open = false;
        app->goto_len = 0;
        app->goto_buf[0] = 0;
        return true;
    case SDLK_S:
        if (shift || !(app_ed(app) && app_ed(app)->path[0])) {
            file_save_as_dialog(app);
        } else {
            editor_save_file(app_ed(app), NULL);
            tab_update_title(app);
        }
        return true;
    default:
        return false;
    }
}

static void handle_tab_click(App *app, float mx)
{
    float x = 0.0f;
    for (int i = 0; i < app->tab_count; ++i) {
        float tw = tab_width(app, i);
        if (mx >= x && mx < x + tw) {
            if (mx >= x + tw - 24.0f) {
                tab_close_at(app, i);
            } else {
                app->tab_active = i;
                tab_update_title(app);
            }
            return;
        }
        x += tw;
    }
}

static void handle_sidebar_wheel(App *app, const SDL_MouseWheelEvent *wheel)
{
    app->project_scroll -= (int)wheel->y * 3;
    int rows_visible = (int)(((float)app->vk.swapchain_extent.height -
                              STATUS_BAR_HEIGHT - 4.0f -
                              project_tree_top(app, TAB_BAR_HEIGHT)) /
                             project_row_height(app));
    int max_scroll = (int)app->project_visible_count - rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (app->project_scroll < 0) app->project_scroll = 0;
    if (app->project_scroll > max_scroll)
        app->project_scroll = max_scroll;
}

void input_handle_event(App *app, SDL_Event *ev)
{
    if (ev->type == SDL_EVENT_QUIT || ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (!app->quit_pending) {
            app->quit_pending = true;
            if (!app->save_dialog_open)
                continue_quit(app);
        }
        return;
    }
    if (ev->type == SDL_EVENT_WINDOW_RESIZED ||
        ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        app->framebuffer_resized = true;
        return;
    }
    if (app->save_dialog_open)
        return;

    if (ev->type == SDL_EVENT_KEY_DOWN && handle_shortcut(app, &ev->key))
        return;

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        ev->button.y < TAB_BAR_HEIGHT &&
        !app->find_open && !app->goto_open && !app->picker_open) {
        handle_tab_click(app, (float)ev->button.x);
        return;
    }

    if (app->goto_open && overlay_goto_event(app, ev))
        return;
    if (app->find_open && overlay_find_event(app, ev))
        return;
    if (app->picker_open) {
        overlay_picker_event(app, ev);
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        project_handle_click(app, (float)ev->button.x, (float)ev->button.y,
                             TAB_BAR_HEIGHT, STATUS_BAR_HEIGHT,
                             (float)app->vk.swapchain_extent.height))
        return;

    if (ev->type == SDL_EVENT_MOUSE_WHEEL && !app->find_open && !app->goto_open) {
        float mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        if (mouse_x >= 0.0f && mouse_x < PROJECT_SIDEBAR_WIDTH &&
            mouse_y >= project_tree_top(app, TAB_BAR_HEIGHT) &&
            mouse_y < (float)app->vk.swapchain_extent.height - STATUS_BAR_HEIGHT) {
            handle_sidebar_wheel(app, &ev->wheel);
            return;
        }
    }

    Editor *ed = app_ed(app);
    if (ed) editor_handle_event(ed, ev);
}
