#ifndef PROJECT_SIDEBAR_H
#define PROJECT_SIDEBAR_H

#include <stdbool.h>

#include "app/app.h"

#define PROJECT_SIDEBAR_WIDTH 250.0f

float project_row_height(const App *app);
float project_tree_top(const App *app, float tab_h);
bool project_icons_load(App *app);
void project_icons_destroy(App *app);
void draw_project_sidebar(App *app, VkContext *ctx, float window_h,
                          float tab_h, float status_h);
bool project_handle_click(App *app, float x, float y, float tab_h,
                          float status_h, float window_h);

#endif
