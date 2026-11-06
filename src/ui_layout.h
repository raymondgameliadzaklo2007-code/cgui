#ifndef CGUI_UI_LAYOUT_H
#define CGUI_UI_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "clay.h"

typedef enum ToolStripeItem {
    TOOL_STRIPE_NONE = 0,
    TOOL_STRIPE_PROJECT,
    TOOL_STRIPE_SEARCH,
    TOOL_STRIPE_GIT,
    TOOL_STRIPE_DEBUG,
    TOOL_STRIPE_SETTINGS,
} ToolStripeItem;

typedef enum DockTabItem {
    DOCK_TAB_TERMINAL = 0,
    DOCK_TAB_PROBLEMS,
    DOCK_TAB_GIT,
} DockTabItem;

typedef struct UiLayout {
    bool initialized;
    void *clay_memory;
    size_t clay_memory_size;
    Clay_Arena arena;

    /* Viewport */
    float width;
    float height;

    /* Tool window states */
    ToolStripeItem active_tool;
    bool sidebar_open;
    float sidebar_width;
    bool sidebar_resizing;

    /* Bottom dock states */
    bool dock_open;
    DockTabItem dock_tab;
    float dock_height;
    bool dock_resizing;

    /* Computed bounding boxes from Clay */
    Clay_BoundingBox stripe_bounds;
    Clay_BoundingBox toolbar_bounds;
    Clay_BoundingBox sidebar_bounds;
    Clay_BoundingBox tabbar_bounds;
    Clay_BoundingBox editor_bounds;
    Clay_BoundingBox dock_bounds;
    Clay_BoundingBox statusbar_bounds;

    /* Toolbar sub-widgets */
    Clay_BoundingBox run_dropdown_bounds;
    Clay_BoundingBox run_btn_bounds;
    Clay_BoundingBox build_btn_bounds;
    Clay_BoundingBox debug_btn_bounds;
    Clay_BoundingBox search_btn_bounds;
    Clay_BoundingBox settings_btn_bounds;
    Clay_BoundingBox branch_btn_bounds;

    /* Stripe icon hit rects */
    Clay_BoundingBox stripe_project_bounds;
    Clay_BoundingBox stripe_search_bounds;
    Clay_BoundingBox stripe_git_bounds;
    Clay_BoundingBox stripe_debug_bounds;
    Clay_BoundingBox stripe_settings_bounds;

    /* Dock header tabs */
    Clay_BoundingBox dock_tab_term_bounds;
    Clay_BoundingBox dock_tab_prob_bounds;
    Clay_BoundingBox dock_close_bounds;
} UiLayout;

struct App;

bool ui_layout_init(UiLayout *ui, float width, float height);
void ui_layout_destroy(UiLayout *ui);
void ui_layout_update(UiLayout *ui, struct App *app, float width, float height,
                      float mouse_x, float mouse_y, bool mouse_down);

/* Hit testing helpers */
static inline bool ui_rect_contains(Clay_BoundingBox b, float x, float y) {
    return x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height;
}

#endif /* CGUI_UI_LAYOUT_H */
