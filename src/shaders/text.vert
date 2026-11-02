#version 450

/* One instance per glyph. rect/uv come from font.c's instance ring; everything
   shared by the whole draw call lives in push constants. */
layout(push_constant) uniform Push {
    vec4 color;     /* RGBA of the whole string */
    vec4 tune;      /* x = weight, y = contrast, z = sRGB target, w = ppem (fragment stage) */
    vec4 bg;        /* rgb = background colour, w = 1 when known (fragment stage) */
    vec2 screen;    /* w, h in device pixels */
} pc;

layout(location = 0) in vec4 i_rect;   /* x, y, w, h in *device* pixels (integer-snapped) */
layout(location = 1) in vec4 i_uv;     /* u0, v0, u1, v1 */

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 uv = corners[gl_VertexIndex];

    /* Belt and braces: glyph edges must sit on pixel boundaries so each
       fragment centre samples exactly one atlas texel (no bilinear blur). */
    vec2 pos = floor(i_rect.xy + uv * i_rect.zw + 0.5);

    v_uv = mix(i_uv.xy, i_uv.zw, uv);
    v_color = pc.color;

    vec2 ndc = vec2(
        (pos.x / pc.screen.x) * 2.0 - 1.0,
        (pos.y / pc.screen.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
