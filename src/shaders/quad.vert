#version 450

// Vertex attributes: standard unit quad (0..1)
layout(location = 0) in vec2 in_unit_pos;

// Instance attributes
layout(location = 1) in vec4 in_rect;         // x, y, width, height (screen pixels)
layout(location = 2) in vec4 in_color;        // r, g, b, a (fill)
layout(location = 3) in vec4 in_border_color; // r, g, b, a (border)
layout(location = 4) in vec4 in_params;       // border_width, corner_radius, screen_width, screen_height

layout(location = 0) out vec2 frag_local_pos;
layout(location = 1) out vec2 frag_rect_size;
layout(location = 2) out vec4 frag_color;
layout(location = 3) out vec4 frag_border_color;
layout(location = 4) out vec2 frag_params;    // border_width, corner_radius

void main() {
    float screen_w = in_params.z;
    float screen_h = in_params.w;

    // Local pixel coordinates inside the element rectangle (origin at top-left)
    frag_local_pos = in_unit_pos * in_rect.zw;
    frag_rect_size = in_rect.zw;
    frag_color = in_color;
    frag_border_color = in_border_color;
    frag_params = in_params.xy; // border_width, corner_radius

    // Physical screen position in pixels
    vec2 pixel_pos = in_rect.xy + frag_local_pos;

    // Convert pixel position to normalized device coordinates (NDC: [-1, 1], Y down)
    vec2 ndc = (pixel_pos / vec2(screen_w, screen_h)) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
