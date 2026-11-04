/**
 * sidebar.h – project navigator (folder tree on the left)
 */

#ifndef CGUI_SIDEBAR_H
#define CGUI_SIDEBAR_H

#include "app.h"

#define PROJECT_SIDEBAR_WIDTH 250.0f
#define PROJECT_HEADER_HEIGHT 28.0f
#define PROJECT_ROOT_TOP 30.0f

/** Show `path` (a directory) as the project root. */
bool project_set_root(App *app, const char *path);
/** Pick a root from a CLI argument (dir, or a file's parent) or the cwd. */
bool project_open_initial(App *app, const char *path);
void project_show_folder_dialog(App *app);

bool project_icons_load(App *app);
void project_icons_destroy(App *app);

void draw_project_sidebar(App *app, VkContext *ctx, float window_h,
                          float tab_h, float status_h);
/** Returns true when the click landed inside the sidebar. */
bool project_handle_click(App *app, float x, float y, float tab_h,
                          float status_h, float window_h);
/** Scrolls the tree when the mouse is over it; returns true if consumed. */
bool project_handle_wheel(App *app, float wheel_y, float tab_h,
                          float status_h, float window_h);
/** Release the project tree. */
void project_free(App *app);

#endif /* CGUI_SIDEBAR_H */
