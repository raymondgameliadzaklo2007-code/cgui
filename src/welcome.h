/**
 * welcome.h – JetBrains / Xcode style Welcome & Project Generator Screen
 */

#ifndef CGUI_WELCOME_H
#define CGUI_WELCOME_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>
#include "project.h"

struct App;
struct VkContext;

typedef enum WelcomeMode {
    WELCOME_MODE_HUB = 0,         /* Xcode Welcome Hub: Actions on left, Recents on right */
    WELCOME_MODE_NEW_PROJECT      /* Template picker and configuration */
} WelcomeMode;

typedef struct WelcomeState {
    WelcomeMode mode;
    int         selected_template;    /* index in ProjectTemplate */
    char       project_name[128];
    int        project_name_len;
    char       parent_location[512];
    int        parent_location_len;
    int        focused_field;        /* 0 = None, 1 = Name, 2 = Location */
    uint64_t   last_blink_ms;
    bool       cursor_blink;

    char       recent_paths[16][1024];
    char       recent_names[16][128];
    int        recent_count;
    int        recent_selected;

    char       status_msg[128];
    uint64_t   status_ms;
} WelcomeState;

void welcome_init(struct App *app);
void welcome_open(struct App *app);
void welcome_close(struct App *app);
void welcome_draw(struct App *app, struct VkContext *ctx);
bool welcome_handle_event(struct App *app, const SDL_Event *ev);

void welcome_add_recent(const char *path);
void welcome_load_recents(WelcomeState *st);

#endif /* CGUI_WELCOME_H */
