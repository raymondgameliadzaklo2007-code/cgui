#ifndef INPUT_H
#define INPUT_H

#include <SDL3/SDL.h>

#include "app/app.h"

void input_handle_event(App *app, SDL_Event *ev);

#endif
