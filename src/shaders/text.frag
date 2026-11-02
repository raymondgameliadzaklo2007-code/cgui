#version 450

layout(push_constant) uniform Push {
    vec4 color;
    vec4 tune;      /* x = weight (1 = default, >1 bolder), y = contrast 0..1,
                       z = 1 when the swapchain is *_SRGB, w = raster size in device px */
    vec4 bg;        /* rgb = background colour (gamma-encoded), w = 1 when known */
    vec2 screen;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

/*
 * Grayscale AA, two modes.
 *
 * UNORM swapchain (default): blending happens on gamma-encoded values, but
 * FreeType coverage is linear, so a plain blend makes light-on-dark text too
 * thin and dark-on-light text too heavy. The coverage exponent is chosen from
 * the foreground luminance:
 *   dark text  -> mild boost (a touch fuller than physical, like Core Text)
 *   light text -> strong boost (compensates the gamma-space blend)
 *
 * *_SRGB swapchain: the hardware already blends in linear light, so coverage
 * stays near-linear. Dark text gets a little fattening (it looks thin when
 * blended physically), light text none (it looks bold). The text colour must
 * be decoded to linear because the framebuffer re-encodes on write.
 *
 * Size adaptation: the fattening and contrast exist to rescue small text, where
 * every edge pixel matters. As the raster size grows they fade out so large
 * glyphs stay at their true weight (no bloated headings), and below ~13 px
 * the contrast gets an extra nudge to keep tiny stems from going grey.
 *
 * Either way a gentle S-curve steepens the edge ramp so stems read crisper
 * without turning into aliased 1-bit text. Weight / contrast / mode come from
 * font.c (font_set_tuning, font_set_target_srgb).
 */
/* Used when the background colour is known (font_set_background): */
const float EXP_ON_LIGHT_BG      = 0.50;   /* dark text on a lighter background */
const float EXP_ON_DARK_BG       = 1.12;   /* light text on a darker background */

const float EXP_DARK_TEXT        = 0.95;   /* stem darkening (font.c) already adds weight */
const float EXP_LIGHT_TEXT       = 0.55;
const float EXP_DARK_TEXT_SRGB   = 0.40;   /* matches the UNORM look (measured on lavapipe) */
const float EXP_LIGHT_TEXT_SRGB  = 1.05;

float lin1(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float enc1(float c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }

/* Known background: the alpha that makes a *gamma-space* blend land exactly where
   a *linear-light* blend of fg over bg would. This replaces the luminance-only
   heuristic and is correct for any text colour on any background (mid-tone
   syntax colours included). fl / bl are gamma-encoded luminances. */
float linear_light_alpha(float a, float fl, float bl)
{
    float d = fl - bl;
    if (abs(d) < 0.04) return a;                       /* near-identical: nothing to fix */
    float target = enc1(mix(lin1(bl), lin1(fl), a));
    return clamp((target - bl) / d, 0.0, 1.0);
}

vec3 srgb_to_linear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main()
{
    float c = clamp(texture(u_atlas, v_uv).r, 0.0, 1.0);
    float w = max(pc.tune.x, 0.1);

    /* Gamma-encoded luminance of the text colour. */
    float lum = dot(v_color.rgb, vec3(0.2126, 0.7152, 0.0722));

    float ppem  = pc.tune.w;
    float big   = smoothstep(14.0, 48.0, ppem);          /* 0 = small text, 1 = large */
    float tiny  = 1.0 - smoothstep(9.0, 14.0, ppem);     /* 1 = very small text */

    vec3 rgb = v_color.rgb;
    float e_small, e_large;
    if (pc.tune.z > 0.5) {
        rgb = srgb_to_linear(rgb);
        e_small = mix(EXP_DARK_TEXT_SRGB, EXP_LIGHT_TEXT_SRGB, lum);
        e_large = mix(0.95, 1.05, lum);
    } else {
        e_small = mix(EXP_DARK_TEXT, EXP_LIGHT_TEXT, lum);
        e_large = mix(1.0, 0.65, lum);
    }
    float e = mix(e_small, e_large, big) / w;

    /* Background known (UNORM target): correct the blend exactly, then only a mild
       Core Text-style fullness for dark text on top (light text is already full). */
    if (pc.bg.w > 0.5 && pc.tune.z < 0.5) {
        float bl = dot(pc.bg.rgb, vec3(0.2126, 0.7152, 0.0722));
        c = linear_light_alpha(c, lum, bl);
        /* The exact blend is physically right but looks thin for dark-on-light text
           (Core Text deliberately renders that fuller) and a touch heavy for
           light-on-dark. Direction is relative to the real background. */
        float lighter = smoothstep(-0.15, 0.15, lum - bl);        /* 1 = text lighter than bg */
        e = mix(mix(EXP_ON_LIGHT_BG, EXP_ON_DARK_BG, lighter), 1.0, big) / w;
    }

    float k = clamp(pc.tune.y, 0.0, 1.0) * (1.0 - 0.6 * big) * (1.0 + 0.5 * tiny);
    float a = pow(c, e);
    a = mix(a, a * a * (3.0 - 2.0 * a), clamp(k, 0.0, 1.0));

    out_color = vec4(rgb, v_color.a * a);
}
