#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_tex;

layout(push_constant) uniform Push {
    vec2 direction;   /* (1/w, 0) or (0, 1/h) */
    float sigma;      /* Gaussian sigma in pixels */
    float _pad;
} pc;

void main()
{
    /* 9-tap separable Gaussian, weights from sigma */
    float sigma = max(pc.sigma, 0.5);
    float two_sigma2 = 2.0 * sigma * sigma;

    vec4 sum = vec4(0.0);
    float wsum = 0.0;

    /* radius ≈ 3*sigma, clamped for performance */
    int radius = int(ceil(sigma * 3.0));
    radius = clamp(radius, 1, 32);

    for (int i = -radius; i <= radius; ++i) {
        float fi = float(i);
        float w = exp(-(fi * fi) / two_sigma2);
        vec2 uv = v_uv + pc.direction * fi;
        sum += texture(u_tex, uv) * w;
        wsum += w;
    }

    out_color = sum / max(wsum, 1e-4);
}
