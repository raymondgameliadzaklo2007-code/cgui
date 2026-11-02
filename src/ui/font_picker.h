#ifndef FONT_PICKER_H
#define FONT_PICKER_H

#include <stdbool.h>

#include "app/app.h"

bool reload_font(App *app, const char *path);
void draw_picker(App *app, VkContext *ctx);

#endif
