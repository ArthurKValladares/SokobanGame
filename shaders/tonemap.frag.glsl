#version 460

// The last step before the swapchain, and the only place in the frame where
// linear scene light becomes a presentable colour.
//
// The scene target is a float image now, so nothing clips at 1.0 while the
// scene is being lit. This pass is what brings that range back down, and the
// display image it writes is an _SRGB format, so the hardware performs the
// linear -> sRGB encode on write. That is the *single* encode in the frame.
// The scene target used to be _SRGB itself and encoded once per draw; if this
// pass ever writes to a UNORM target, or applies its own pow(), the result is
// a colour-space bug that reads like a badly chosen tonemap curve.
layout(set = 0, binding = 11) uniform sampler2D sceneHdrColor;

layout(location = 0) out vec4 outColor;

// GpuDrawInstance::color sits at byte 128, which is the slot every other
// fullscreen pass reads its parameters from.
// params: x = exposure in EV, y = curve (0 = clamp, 1 = Khronos PBR
// Neutral), z and w unused.
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
} pc;

// Khronos PBR Neutral reference curve. Input and output are both linear Rec.
// 709; the sRGB display attachment performs the only transfer-function encode.
vec3 pbrNeutralToneMap(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 /
        (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}

void main()
{
    // The scene target and the display image are the same extent, so this is
    // a 1:1 fetch with no filtering. Scaling to the swapchain stays a
    // separate blit.
    vec3 color = texelFetch(sceneHdrColor, ivec2(gl_FragCoord.xy), 0).rgb;

    color = max(color, vec3(0.0)) * exp2(pc.params.x);
    color = pc.params.y < 0.5
        ? clamp(color, 0.0, 1.0)
        : pbrNeutralToneMap(color);
    // The reference curve is already bounded. The clamp only contains
    // floating-point roundoff and keeps the Debug comparison equally robust.
    color = clamp(color, 0.0, 1.0);

    outColor = vec4(color, 1.0);
}
