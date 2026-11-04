/**
 * find_bar.h – find / replace bar and go-to-line bar
 */

#ifndef CGUI_FIND_BAR_H
#define CGUI_FIND_BAR_H

#include "app.h"

void find_bar_open(App *app, bool replace);
void draw_find_bar(App *app, VkContext *ctx);
/** Call while app->find_open. Returns true when the event was consumed. */
bool find_bar_handle_event(App *app, const SDL_Event *ev);

void goto_bar_open(App *app);
void draw_goto_bar(App *app, VkContext *ctx);
/** Call while app->goto_open. Returns true when the event was consumed. */
bool goto_bar_handle_event(App *app, const SDL_Event *ev);

#endif /* CGUI_FIND_BAR_H */
