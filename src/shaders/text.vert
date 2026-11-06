#version 450

// Vertex attributes: standard unit quad [0, 1] x [0, 1]
layout(location = 0) in vec2 in_unit_pos;

// Instance attributes
layout(location = 1) in vec4 in_rect;   // x, y, width, height (screen pixels)
layout(location = 2) in vec4 in_uv;     // u0, v0, u1, v1 (atlas UV coordinates)
layout(location = 3) in vec4 in_color;  // r, g, b, a (text color)
layout(location = 4) in vec4 in_params; // x = px_range, y = screen_w, z = screen_h, w = unused

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;
layout(location = 2) out vec2 frag_screen_size;
layout(location = 3) out float frag_px_range;

void main() {
    float screen_w = in_params.y;
    float screen_h = in_params.z;
    frag_px_range = in_params.x;

    // Bilinearly interpolate UV across the unit quad
    frag_uv = mix(in_uv.xy, in_uv.zw, in_unit_pos);
    frag_color = in_color;
    frag_screen_size = vec2(screen_w, screen_h);

    // Compute pixel position on screen
    vec2 pixel_pos = in_rect.xy + in_unit_pos * in_rect.zw;

    // Convert pixel position to normalized device coordinates (NDC: [-1, 1], Y down)
    vec2 ndc = (pixel_pos / vec2(screen_w, screen_h)) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
