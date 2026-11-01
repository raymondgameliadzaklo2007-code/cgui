/**
 * style.h – Simple visual style for widgets
 *
 * Intentionally small. Covers panels, buttons, labels and soft shadows.
 * Expand later only when real need appears.
 */

#ifndef STYLE_H
#define STYLE_H

#include <stdint.h>

typedef struct Style {
    /* Layout hints (0 = auto / unused) */
    float padding;
    float width;
    float height;

    /* Appearance */
    float   corner_radius;
    uint8_t bg[4];              /* RGBA 0–255 */
    uint8_t border_color[4];
    float   border_width;

    /* Soft drop shadow */
    float   shadow_offset_y;
    float   shadow_blur;
    uint8_t shadow_color[4];

    /* Text (used only by text-bearing widgets) */
    float   text_size;
    uint8_t text_color[4];
} Style;

/** Clean macOS-ish default (white card, soft shadow, 10 px radius). */
Style style_default(void);

#endif /* STYLE_H */
