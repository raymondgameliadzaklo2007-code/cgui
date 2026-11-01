#version 450

layout(push_constant) uniform Push {
    vec4 color;
    vec2 screen;
    vec2 tune;      /* x = weight (1 = default, >1 bolder), y = contrast 0..1 */
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

/*
 * Grayscale AA tuned for a UNORM swapchain (blending happens on gamma-encoded
 * values). FreeType coverage is linear, so a plain blend makes light-on-dark
 * text too thin and dark-on-light text too heavy. The coverage exponent is
 * therefore chosen from the foreground luminance:
 *
 *   dark text   -> mild boost  (slightly fuller than physically correct,
 *                               which is what Core Text's smoothing looks like)
 *   light text  -> strong boost (compensates the gamma-space blend)
 *
 * A gentle S-curve then steepens the edge ramp so stems read crisper without
 * turning into aliased 1-bit text. Weight and contrast come from font.c
 * (font_set_tuning) via push constants.
 */
const float EXP_DARK_TEXT  = 0.80;
const float EXP_LIGHT_TEXT = 0.55;

void main()
{
    float c = clamp(texture(u_atlas, v_uv).r, 0.0, 1.0);

    /* Gamma-encoded luminance of the text colour (matches what gets blended). */
    float lum = dot(v_color.rgb, vec3(0.2126, 0.7152, 0.0722));

    float e = mix(EXP_DARK_TEXT, EXP_LIGHT_TEXT, lum) / max(pc.tune.x, 0.1);
    float a = pow(c, e);
    a = mix(a, a * a * (3.0 - 2.0 * a), clamp(pc.tune.y, 0.0, 1.0));

    out_color = vec4(v_color.rgb, v_color.a * a);
}
