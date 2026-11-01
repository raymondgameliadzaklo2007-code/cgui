/**
 * button.h – Interactive button widget
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "gui.h"
#include "vk_draw.h"

typedef struct Button Button;

typedef void (*ButtonClickFn)(Button *self, void *userdata);

struct Button {
    Widget base;              /* MUST be first */

    const char *label;
    bool hovered;
    bool pressed;

    uint8_t hover_bg[4];
    uint8_t press_bg[4];

    ButtonClickFn on_click;
    void *userdata;
};

Button *button_create(Rect bounds, Style style, const char *label);

void button_set_click_handler(Button *btn, ButtonClickFn fn, void *userdata);

void button_set_interaction_colors(Button *btn,
                                   const uint8_t hover_bg[4],
                                   const uint8_t press_bg[4]);

void button_draw(Widget *self, struct VkContext *vk);
void button_event(Widget *self, const void *ev);

void button_set_draw_context(VkDraw *draw);

#endif /* BUTTON_H */
