#version 450
layout(push_constant) uniform Push {
    vec4 rect;   /* x y w h device px */
    vec4 color;
    vec2 screen;
    vec2 _pad;
} pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main() {
    vec2 c[6] = vec2[](
        vec2(0,0), vec2(1,0), vec2(0,1),
        vec2(1,0), vec2(1,1), vec2(0,1)
    );
    vec2 uv = c[gl_VertexIndex];
    vec2 pos = pc.rect.xy + uv * pc.rect.zw;
    v_uv = uv;
    v_color = pc.color;
    gl_Position = vec4(vec2(pos.x/pc.screen.x*2.0-1.0, pos.y/pc.screen.y*2.0-1.0), 0.0, 1.0);
}
