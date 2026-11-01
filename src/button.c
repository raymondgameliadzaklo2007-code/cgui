/**
 * button.c – Button implementation
 */

#include "button.h"

#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

static VkDraw *g_draw = NULL;

void button_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}

static bool point_in_rect(float px, float py, const Rect *r)
{
    return px >= r->x && py >= r->y &&
           px <  r->x + r->w && py <  r->y + r->h;
}

void button_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) {
        return;
    }

    Button *btn = (Button *)self;
    Style style = self->style;   /* copy so we can override bg */

    if (btn->pressed && btn->press_bg[3] > 0) {
        memcpy(style.bg, btn->press_bg, 4);
    } else if (btn->hovered && btn->hover_bg[3] > 0) {
        memcpy(style.bg, btn->hover_bg, 4);
    }

    const Rect *b = &self->bounds;
    vk_draw_style_box(g_draw, b->x, b->y, b->w, b->h, &style);

    /* Text rendering comes later – label is stored for the next step. */
    (void)btn->label;

    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->draw) {
            child->draw(child, vk);
        }
    }
}

void button_event(Widget *self, const void *ev_ptr)
{
    if (!self || !ev_ptr) {
        return;
    }

    Button *btn = (Button *)self;
    const SDL_Event *ev = (const SDL_Event *)ev_ptr;

    float mx = 0, my = 0;
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        mx = (float)ev->motion.x;
        my = (float)ev->motion.y;
        btn->hovered = point_in_rect(mx, my, &self->bounds);
        if (!btn->hovered) {
            btn->pressed = false;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               ev->button.button == SDL_BUTTON_LEFT) {
        mx = (float)ev->button.x;
        my = (float)ev->button.y;
        if (point_in_rect(mx, my, &self->bounds)) {
            btn->pressed = true;
            btn->hovered = true;
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
               ev->button.button == SDL_BUTTON_LEFT) {
        mx = (float)ev->button.x;
        my = (float)ev->button.y;
        bool inside = point_in_rect(mx, my, &self->bounds);
        if (btn->pressed && inside && btn->on_click) {
            btn->on_click(btn, btn->userdata);
        }
        btn->pressed = false;
        btn->hovered = inside;
    }
}

Button *button_create(Rect bounds, Style style, const char *label)
{
    Button *b = calloc(1, sizeof(Button));
    if (!b) {
        return NULL;
    }

    b->base.bounds       = bounds;
    b->base.style        = style;
    b->base.draw         = button_draw;
    b->base.handle_event = button_event;
    b->base.destroy      = widget_destroy_noop;

    b->label = label;

    /* Default macOS-ish interaction colours (light grey → slightly darker) */
    b->hover_bg[0] = 245; b->hover_bg[1] = 245; b->hover_bg[2] = 247; b->hover_bg[3] = 255;
    b->press_bg[0] = 230; b->press_bg[1] = 230; b->press_bg[2] = 235; b->press_bg[3] = 255;

    return b;
}

void button_set_click_handler(Button *btn, ButtonClickFn fn, void *userdata)
{
    if (!btn) return;
    btn->on_click = fn;
    btn->userdata = userdata;
}

void button_set_interaction_colors(Button *btn,
                                   const uint8_t hover_bg[4],
                                   const uint8_t press_bg[4])
{
    if (!btn) return;
    if (hover_bg) memcpy(btn->hover_bg, hover_bg, 4);
    if (press_bg) memcpy(btn->press_bg, press_bg, 4);
}
