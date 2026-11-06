#version 450

layout(binding = 0) uniform sampler2D msdf_atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;
layout(location = 2) in vec2 frag_screen_size;
layout(location = 3) in float frag_px_range;

layout(location = 0) out vec4 out_color;

// Median of the 3 distance field channels (MSDF sharp corner reconstruction)
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // Sample the multi-channel distance field texture
    vec3 msd = texture(msdf_atlas, frag_uv).rgb;
    float sd = median(msd.r, msd.g, msd.b);

    // Screen-space derivative anti-aliasing calculation
    vec2 unit_range = vec2(frag_px_range) / vec2(textureSize(msdf_atlas, 0));
    vec2 screen_tex_size = vec2(1.0) / max(fwidth(frag_uv), vec2(0.00001));
    float screen_px_range = max(0.5 * dot(unit_range, screen_tex_size), 1.0);

    // Signed distance in screen pixels
    float screen_px_distance = screen_px_range * (sd - 0.5);

    // Smooth edge alpha factor
    float opacity = clamp(screen_px_distance + 0.5, 0.0, 1.0);

    if (opacity <= 0.0) {
        discard;
    }

    out_color = vec4(frag_color.rgb, frag_color.a * opacity);
}
