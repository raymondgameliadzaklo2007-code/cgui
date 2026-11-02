#include <stdio.h>

#include "app/tabs.h"
#include "ui/search_bars.h"

void draw_find_bar(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float bar_h = app->replace_mode ? 72.0f : 44.0f;
    uint8_t bg[4] = { 236, 236, 240, 255 };
    uint8_t field[4] = { 255, 255, 255, 255 };
    uint8_t text_c[4] = { 30, 30, 30, 255 };
    uint8_t dim[4] = { 110, 110, 120, 255 };
    uint8_t accent[4] = { 0, 122, 255, 255 };

    vk_draw_rounded_rect(&app->draw, 0, 0, w, bar_h, 0.0f, 1.0f, bg);

    float fx = 12.0f, fy = 8.0f, fw = w - 24.0f, fh = 28.0f;
    vk_draw_rounded_rect(&app->draw, fx, fy, fw, fh, 6.0f, 1.0f, field);

    float baseline = fy + 6.0f + app->font.ascent;
    const char *fq = app->find_len > 0 ? app->find_query : "Find…";
    font_draw(&app->font, ctx, fx + 10, baseline, fq, app->find_len > 0 ? text_c : dim);
    if (app->find_field == 0) {
        float tw = font_measure(&app->font, app->find_len > 0 ? app->find_query : "");
        vk_draw_rounded_rect(&app->draw, fx + 10 + tw + 1, fy + 5, 2, fh - 10, 1, 1, accent);
    }

    if (app->replace_mode) {
        float fy2 = fy + 32.0f;
        vk_draw_rounded_rect(&app->draw, fx, fy2, fw, fh, 6.0f, 1.0f, field);
        float b2 = fy2 + 6.0f + app->font.ascent;
        const char *rq = app->replace_len > 0 ? app->replace_query : "Replace with…";
        font_draw(&app->font, ctx, fx + 10, b2, rq, app->replace_len > 0 ? text_c : dim);
        if (app->find_field == 1) {
            float tw = font_measure(&app->font, app->replace_len > 0 ? app->replace_query : "");
            vk_draw_rounded_rect(&app->draw, fx + 10 + tw + 1, fy2 + 5, 2, fh - 10, 1, 1, accent);
        }
    }

    if (app->find_status[0]) {
        font_draw(&app->font, ctx, fx + 10, bar_h + app->font.ascent + 2,
                  app->find_status, dim);
    }
}

void draw_goto_bar(App *app, VkContext *ctx)
{
    float w = (float)ctx->swapchain_extent.width;
    float bar_h = 44.0f;
    uint8_t bg[4] = { 236, 236, 240, 255 };
    uint8_t field[4] = { 255, 255, 255, 255 };
    uint8_t text_c[4] = { 30, 30, 30, 255 };
    uint8_t dim[4] = { 110, 110, 120, 255 };
    uint8_t accent[4] = { 0, 122, 255, 255 };

    vk_draw_rounded_rect(&app->draw, 0, 0, w, bar_h, 0.0f, 1.0f, bg);
    float fx = 12.0f, fy = 8.0f, fw = w - 24.0f, fh = 28.0f;
    vk_draw_rounded_rect(&app->draw, fx, fy, fw, fh, 6.0f, 1.0f, field);

    float baseline = fy + 6.0f + app->font.ascent;
    char prompt[64];
    int cur = editor_current_line(app_ed(app));
    int tot = editor_line_count(app_ed(app));
    if (app->goto_len > 0)
        snprintf(prompt, sizeof(prompt), "Go to line: %s", app->goto_buf);
    else
        snprintf(prompt, sizeof(prompt), "Go to line… (current %d / %d)", cur, tot);
    font_draw(&app->font, ctx, fx + 10, baseline, prompt,
              app->goto_len > 0 ? text_c : dim);

    float tw = font_measure(&app->font, prompt);
    vk_draw_rounded_rect(&app->draw, fx + 10 + tw + 1, fy + 5, 2, fh - 10, 1, 1, accent);
}
