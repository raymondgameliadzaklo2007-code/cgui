/**
 * palette.h – command palette (Ctrl+Shift+P)
 */

#ifndef CGUI_PALETTE_H
#define CGUI_PALETTE_H

#include "app.h"

typedef struct Command {
    const char *label;
    const char *shortcut;
    void (*run)(App *app);
} Command;

/* Defined in main.c */
extern const Command app_commands[];
extern const int app_command_count;

void palette_show(App *app);
void draw_palette(App *app, VkContext *ctx);
/** Call while app->palette_open. Returns true when the event was consumed. */
bool palette_handle_event(App *app, const SDL_Event *ev);

#endif /* CGUI_PALETTE_H */
