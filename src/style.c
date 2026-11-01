/**
 * style.c – Default style values
 */

#include "style.h"

Style style_default(void)
{
    return (Style){
        .padding         = 0.0f,
        .width           = 0.0f,
        .height          = 0.0f,

        .corner_radius   = 10.0f,
        .bg              = { 255, 255, 255, 255 },
        .border_color    = { 0, 0, 0, 25 },
        .border_width    = 0.5f,

        .shadow_offset_y = 4.0f,
        .shadow_blur     = 14.0f,
        .shadow_color    = { 0, 0, 0, 50 },

        .text_size       = 13.0f,
        .text_color      = { 0, 0, 0, 255 },
    };
}
