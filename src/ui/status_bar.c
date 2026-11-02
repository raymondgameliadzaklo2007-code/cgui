#include <stdio.h>

#include "app/tabs.h"
#include "ui/status_bar.h"

void draw_status_bar(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float h = (float)ctx->swapchain_extent.height;
    const float bar_h = STATUS_BAR_HEIGHT;
    uint8_t bg[4]   = { 246, 246, 248, 255 };
    uint8_t edge[4] = { 210, 210, 215, 255 };
    uint8_t text[4] = { 90, 90, 98, 255 };

    float y = h - bar_h;
    vk_draw_rounded_rect(&app->draw, 0, y, w, bar_h, 0.0f, 1.0f, bg);
    vk_draw_rounded_rect(&app->draw, 0, y, w, 1.0f, 0.0f, 1.0f, edge);

    Editor *ed = app_ed(app);
    char left[sizeof(((Editor *)0)->path) + 8];
    char right[128];
    if (ed) {
        const char *path = ed->path[0] ? ed->path : "Untitled";
        snprintf(left, sizeof(left), "%s%s", ed->dirty ? "• " : "", path);
        snprintf(right, sizeof(right), "Ln %d, Col %d",
                 editor_current_line(ed), editor_current_col(ed));
    } else {
        snprintf(left, sizeof(left), "No buffer");
        right[0] = 0;
    }

    float baseline = y + (bar_h + app->font.ascent + app->font.descent) * 0.5f;
    font_draw(&app->font, ctx, 12.0f, baseline, left, text);
    float rw = font_measure(&app->font, right);
    font_draw(&app->font, ctx, w - rw - 14.0f, baseline, right, text);
}
