/**
 * panel.c – Panel draws via the SDF UI pipeline
 */

#include "panel.h"

#include <stdlib.h>

static VkDraw *g_draw = NULL;

void panel_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}

void panel_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) {
        return;
    }

    const Rect *b = &self->bounds;
    vk_draw_style_box(g_draw, b->x, b->y, b->w, b->h, &self->style);

    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->draw) {
            child->draw(child, vk);
        }
    }
}

Panel *panel_create(Rect bounds, Style style)
{
    Panel *p = calloc(1, sizeof(Panel));
    if (!p) {
        return NULL;
    }

    p->base.bounds       = bounds;
    p->base.style        = style;
    p->base.draw         = panel_draw;
    p->base.handle_event = widget_event_noop;
    p->base.destroy      = widget_destroy_noop;

    return p;
}
