/**
 * gui.h – Core retained-mode scene graph
 *
 * Widget is the base type. Concrete widgets embed it as their first member
 * so that Widget* ↔ Concrete* casts are safe.
 */

#ifndef GUI_H
#define GUI_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "style.h"

struct VkContext;   /* forward declaration */

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

typedef struct Rect {
    float x, y, w, h;
} Rect;

/* ------------------------------------------------------------------ */
/* Widget                                                              */
/* ------------------------------------------------------------------ */

typedef struct Widget Widget;

typedef void (*WidgetDrawFn)(Widget *self, struct VkContext *vk);
typedef void (*WidgetEventFn)(Widget *self, const void *ev);
typedef void (*WidgetDestroyFn)(Widget *self);

struct Widget {
    Rect   bounds;
    Style  style;

    Widget *parent;
    Widget **children;
    size_t  child_count;
    size_t  child_capacity;

    WidgetDrawFn    draw;
    WidgetEventFn   handle_event;
    WidgetDestroyFn destroy;
};

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

Widget *widget_create(void);
bool    widget_add_child(Widget *parent, Widget *child);

void widget_draw_noop(Widget *self, struct VkContext *vk);
void widget_event_noop(Widget *self, const void *ev);
void widget_destroy_noop(Widget *self);

void widget_destroy(Widget *self);

#endif /* GUI_H */
