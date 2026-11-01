#version 450

layout(push_constant) uniform Push {
    vec4 rect;     /* x,y,w,h pixels */
    vec4 color;    /* multiply colour */
    vec2 screen;   /* w,h */
    vec2 uv_scale; /* usually 1,1 */
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    vec2 corners[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 uv = corners[gl_VertexIndex];
    v_uv = uv * pc.uv_scale;
    v_color = pc.color;

    vec2 pos = pc.rect.xy + uv * pc.rect.zw;
    vec2 ndc = vec2(
        (pos.x / pc.screen.x) * 2.0 - 1.0,
        (pos.y / pc.screen.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
