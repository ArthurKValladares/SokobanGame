#version 460

layout(set = 0, binding = 6) uniform sampler2D ssaoTexture;
layout(location = 0) out vec4 outColor;

// params: x = strength, w = visualize (1 draws the raw AO buffer).
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
} pc;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(ssaoTexture, 0));
    vec2 uv = gl_FragCoord.xy * texel;

    // 5x5 box blur smooths the per-pixel noise from the AO pass.
    float ao = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ao += texture(ssaoTexture, uv + vec2(float(x), float(y)) * texel).r;
        }
    }
    ao /= 25.0;

    if (pc.params.w > 0.5) {
        // Debug visualization: raw blurred AO, drawn without blending.
        outColor = vec4(ao, ao, ao, 1.0);
        return;
    }

    // Multiply blend: the framebuffer is scaled by this output.
    float factor = mix(1.0, ao, clamp(pc.params.x, 0.0, 1.0));
    outColor = vec4(factor, factor, factor, 1.0);
}
