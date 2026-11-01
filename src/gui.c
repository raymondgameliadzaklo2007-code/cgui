/**
 * gui.c – Scene-graph implementation
 */

#include "gui.h"

#include <stdlib.h>
#include <string.h>

Widget *widget_create(void)
{
    Widget *w = calloc(1, sizeof(Widget));
    if (!w) {
        return NULL;
    }

    w->draw         = widget_draw_noop;
    w->handle_event = widget_event_noop;
    w->destroy      = widget_destroy_noop;
    w->style        = style_default();

    return w;
}

bool widget_add_child(Widget *parent, Widget *child)
{
    if (!parent || !child) {
        return false;
    }

    if (parent->child_count >= parent->child_capacity) {
        size_t new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        Widget **new_arr = realloc(parent->children, new_cap * sizeof(Widget *));
        if (!new_arr) {
            return false;
        }
        parent->children = new_arr;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return true;
}

void widget_draw_noop(Widget *self, struct VkContext *vk)
{
    (void)self;
    (void)vk;
}

void widget_event_noop(Widget *self, const void *ev)
{
    (void)self;
    (void)ev;
}

void widget_destroy_noop(Widget *self)
{
    (void)self;
}

void widget_destroy(Widget *self)
{
    if (!self) {
        return;
    }

    for (size_t i = 0; i < self->child_count; ++i) {
        widget_destroy(self->children[i]);
    }
    free(self->children);

    if (self->destroy) {
        self->destroy(self);
    }

    free(self);
}
