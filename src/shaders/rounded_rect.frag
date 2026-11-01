#version 450

layout(location = 0) in vec2  v_local;
layout(location = 1) in vec2  v_half;
layout(location = 2) in vec4  v_color;
layout(location = 3) in float v_radius;
layout(location = 4) in float v_soft;

layout(location = 0) out vec4 out_color;

/*
 * params encoding (from vertex shader):
 *   v_soft > 0  → crisp body with AA width = v_soft
 *   v_soft < 0  → shadow mode, sigma = -v_soft
 *                 alpha falls off as a Gaussian of the SDF outside the shape
 */

float sd_rounded_box(vec2 p, vec2 half_size, float r)
{
    vec2 q = abs(p) - half_size + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    float r = min(v_radius, min(v_half.x, v_half.y));
    float d = sd_rounded_box(v_local, v_half, r);

    float alpha;

    if (v_soft < 0.0) {
        /* ---- Soft shadow (Gaussian of distance outside the edge) ---- */
        float sigma = max(-v_soft, 0.5);
        /* Inside the shape: full shadow mask; outside: Gaussian decay */
        if (d <= 0.0) {
            alpha = 1.0;
        } else {
            float x = d / sigma;
            alpha = exp(-0.5 * x * x);
        }
    } else {
        /* ---- Solid body with anti-aliased edge ---- */
        float soft = max(v_soft, 0.75);
        alpha = 1.0 - smoothstep(-soft, soft, d);
    }

    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
