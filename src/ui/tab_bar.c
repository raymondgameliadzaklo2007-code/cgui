#include <stdio.h>
#include <string.h>

#include "app/tabs.h"
#include "ui/tab_bar.h"

void draw_tab_bar(App *app, VkContext *ctx)
{
    /* Closer to Xcode: flush active tab, close control on active, hairline rules */
    float w = (float)ctx->swapchain_extent.width;
    const float bar_h = TAB_BAR_HEIGHT;

    uint8_t strip[4]   = { 232, 232, 232, 255 };
    uint8_t content[4] = { 252, 252, 252, 255 };
    uint8_t rule[4]    = { 198, 198, 198, 255 };
    uint8_t bottom[4]  = { 180, 180, 180, 255 };
    uint8_t text_act[4]= { 0, 0, 0, 255 };
    uint8_t text_in[4] = { 96, 96, 96, 255 };
    vk_draw_rounded_rect(&app->draw, 0, 0, w, bar_h, 0.0f, 1.0f, strip);
    vk_draw_rounded_rect(&app->draw, 0, bar_h - 1.0f, w, 1.0f, 0.0f, 1.0f, bottom);

    float widths[MAX_TABS];
    float x = 0.0f;
    int visible = 0;
    for (int i = 0; i < app->tab_count && i < MAX_TABS; ++i) {
        float tw = tab_width(app, i);
        if (x + tw > w - 4.0f && i > 0) break;
        widths[i] = tw;
        x += tw;
        visible = i + 1;
    }

    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    x = 0.0f;
    for (int i = 0; i < visible; ++i) {
        float tw = widths[i];
        bool on = (i == app->tab_active);

        if (on) {
            vk_draw_rounded_rect(&app->draw, x, 0, tw, bar_h, 0.0f, 1.0f, content);
            if (i > 0)
                vk_draw_rounded_rect(&app->draw, x, 0, 1.0f, bar_h, 0.0f, 1.0f, rule);
            vk_draw_rounded_rect(&app->draw, x + tw - 1.0f, 0, 1.0f, bar_h, 0.0f, 1.0f, rule);
        } else if (i + 1 < visible && (i + 1) != app->tab_active) {
            vk_draw_rounded_rect(&app->draw, x + tw - 1.0f, 7.0f, 1.0f, bar_h - 14.0f,
                                 0.0f, 1.0f, rule);
        }

        char label[256];
        const char *name = tab_label(app, i, label, sizeof(label));
        if (app->tabs[i].dirty && app->tabs[i].path[0])
            snprintf(label, sizeof(label), "• %s", name);
        else if (name != label)
            snprintf(label, sizeof(label), "%s", name);

        float baseline = (bar_h + app->font.ascent + app->font.descent) * 0.5f;
        /* left-aligned with padding (Xcode shifts toward centre-left when close present) */
        float tx = x + 12.0f;
        font_draw(&app->font, ctx, tx, baseline, label, on ? text_act : text_in);

        /* Reveal the Lucide close icon on tab hover. */
        bool tab_hovered = mouse_y >= 0.0f && mouse_y < bar_h &&
                           mouse_x >= x && mouse_x < x + tw;
        if (tab_hovered) {
            float close_x = x + tw - 24.0f;
            bool close_hovered = mouse_x >= close_x && mouse_x < close_x + 18.0f;
            if (close_hovered) {
                uint8_t hover_bg[4] = { 110, 110, 110, 24 };
                vk_draw_rounded_rect(&app->draw, close_x, 5.0f, 18.0f, 18.0f,
                                     4.0f, 1.0f, hover_bg);
            }
            if (app->icons_ok && app->icon_close.ready) {
                uint8_t tint[4] = { 96, 96, 96, 255 };
                icon_draw(&app->icons, &app->icon_close, ctx,
                          x + tw - 22.0f, 7.0f, 14.0f, 14.0f, tint);
            } else {
                font_draw(&app->font, ctx, x + tw - 21.0f, baseline, "x", text_in);
            }
        }

        x += tw;
    }
}
