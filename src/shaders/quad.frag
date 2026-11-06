#version 450

layout(location = 0) in vec2 frag_local_pos;
layout(location = 1) in vec2 frag_rect_size;
layout(location = 2) in vec4 frag_color;
layout(location = 3) in vec4 frag_border_color;
layout(location = 4) in vec2 frag_params; // x = border_width, y = corner_radius

layout(location = 0) out vec4 out_color;

// Signed distance to a box with rounded corners
float rounded_box_sdf(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    float border_width = frag_params.x;
    float corner_radius = frag_params.y;

    // Center coordinates: origin at center of the rectangle
    vec2 half_size = frag_rect_size * 0.5;
    vec2 p = frag_local_pos - half_size;

    // Clamp corner radius to half the minimum dimension
    float r = clamp(corner_radius, 0.0, min(half_size.x, half_size.y));

    // Outer distance to rounded box edge
    float d = rounded_box_sdf(p, half_size, r);

    // Smooth anti-aliased edge factor using screen-space derivatives
    float afwidth = fwidth(d);
    float outer_alpha = clamp(0.5 - d / max(afwidth, 0.0001), 0.0, 1.0);

    if (outer_alpha <= 0.0) {
        discard;
    }

    vec4 final_color = frag_color;

    if (border_width > 0.0 && frag_border_color.a > 0.0) {
        // Inner edge distance for border
        float inner_d = d + border_width;
        float border_factor = clamp(inner_d / max(fwidth(inner_d), 0.0001) + 0.5, 0.0, 1.0);
        final_color = mix(frag_color, frag_border_color, border_factor);
    }

    out_color = vec4(final_color.rgb, final_color.a * outer_alpha);
}
