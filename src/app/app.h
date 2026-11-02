#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "vk_context.h"
#include "vk_draw.h"
#include "font.h"
#include "fonts.h"
#include "editor.h"
#include "icon.h"

#define MAX_TABS 16
#define PROJECT_PATH_MAX 1024
#define TAB_BAR_HEIGHT 28.0f
#define STATUS_BAR_HEIGHT 26.0f

typedef struct ProjectEntry {
    char name[256];
    char path[PROJECT_PATH_MAX];
    int parent;
    int child_start;
    int child_count;
    bool directory;
    bool expanded;
    bool loaded;
} ProjectEntry;

typedef enum SaveIntent {
    SAVE_INTENT_NORMAL,
    SAVE_INTENT_CLOSE_TAB,
    SAVE_INTENT_QUIT,
} SaveIntent;

typedef struct App {
    SDL_Window *window;
    VkContext   vk;
    VkDraw      draw;
    Font        font;
    FontList    fonts;

    Editor tabs[MAX_TABS];
    int    tab_count;
    int    tab_active;
    unsigned tab_ids[MAX_TABS];
    unsigned next_tab_id;

    char project_root[PROJECT_PATH_MAX];
    char project_name[256];
    ProjectEntry *project_entries;
    size_t project_entry_count;
    size_t project_entry_capacity;
    int project_root_child_start;
    int project_root_child_count;
    bool project_root_loaded;
    bool project_root_expanded;
    int *project_visible;
    size_t project_visible_count;
    size_t project_visible_capacity;
    int project_scroll;
    char pending_project[PROJECT_PATH_MAX];
    bool project_dialog_open;

    IconRenderer icons;
    Icon         icon_close;
    bool         icons_ok;
    Icon         project_folder_icon;
    Icon         project_folder_open_icon;
    Icon         project_file_code_icon;
    Icon         project_file_text_icon;
    Icon         project_file_generic_code_icon;
    Icon         project_file_book_icon;
    Icon         project_chevron_right_icon;
    Icon         project_chevron_down_icon;
    Icon         project_plus_icon;
    bool         project_icons_ok;

    /* Cached text shader SPIR-V for font reloads */
    uint32_t *text_vert;
    size_t    text_vert_n;
    uint32_t *text_frag;
    size_t    text_frag_n;

    int    font_index;
    float  font_size;
    bool   picker_open;
    int    picker_sel;
    int    picker_scroll;

    bool   find_open;
    bool   replace_mode;     /* Ctrl+H */
    char   find_query[256];
    int    find_len;
    char   replace_query[256];
    int    replace_len;
    int    find_field;       /* 0 = find, 1 = replace */
    bool   find_case;
    char   find_status[64];

    bool   goto_open;
    char   goto_buf[32];
    int    goto_len;

    char pending_open[1024];
    char pending_save[1024];
    int pending_save_tab;
    SaveIntent save_intent;
    bool save_dialog_open;
    bool quit_pending;
    bool title_dirty;

    bool running;
    bool framebuffer_resized;
} App;

bool app_init(App *app, SDL_Window *window, int argc, char **argv);
void app_frame(App *app);
void app_shutdown(App *app);

#endif
