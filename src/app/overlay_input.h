#ifndef OVERLAY_INPUT_H
#define OVERLAY_INPUT_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "app/app.h"

bool overlay_goto_event(App *app, SDL_Event *ev);
bool overlay_find_event(App *app, SDL_Event *ev);
void overlay_picker_event(App *app, SDL_Event *ev);

#endif
