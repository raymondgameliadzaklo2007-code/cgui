#version 450

layout(push_constant) uniform Push {
    vec4  rect;       /* x, y, w, h in pixels (top-left origin) */
    vec4  color;      /* RGBA 0–1 */
    vec4  params;     /* x = corner_radius, y = soft_or_neg_sigma, z = screen_w, w = screen_h */
} pc;

layout(location = 0) out vec2  v_local;
layout(location = 1) out vec2  v_half;
layout(location = 2) out vec4  v_color;
layout(location = 3) out float v_radius;
layout(location = 4) out float v_soft;

void main()
{
    vec2 corners[6] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );
    vec2 uv = corners[gl_VertexIndex];

    /* Pad geometry so soft shadow / AA is not clipped */
    float pad = abs(pc.params.y) * 3.5 + 2.0;
    vec2 half0 = 0.5 * pc.rect.zw;
    vec2 centre = pc.rect.xy + half0;

    /* Local coords in the padded quad, still relative to the *original* centre */
    vec2 padded_half = half0 + vec2(pad);
    vec2 local = (uv * 2.0 - 1.0) * padded_half;  /* -padded_half … +padded_half */
    vec2 pos_px = centre + local;

    float sw = pc.params.z;
    float sh = pc.params.w;
    vec2 ndc = vec2(
        (pos_px.x / sw) * 2.0 - 1.0,
        (pos_px.y / sh) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);

    v_local  = local;           /* SDF evaluated in original-size local space */
    v_half   = half0;
    v_color  = pc.color;
    v_radius = pc.params.x;
    v_soft   = pc.params.y;
}
