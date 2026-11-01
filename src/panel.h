/**
 * panel.h – Styled panel (rounded rect + soft shadow)
 */

#ifndef PANEL_H
#define PANEL_H

#include "gui.h"
#include "vk_draw.h"

typedef struct Panel {
    Widget base;   /* MUST be first */
} Panel;

Panel *panel_create(Rect bounds, Style style);
void   panel_draw(Widget *self, struct VkContext *vk);

/* The active draw context is set by main each frame before walking the tree. */
void panel_set_draw_context(VkDraw *draw);

#endif /* PANEL_H */
