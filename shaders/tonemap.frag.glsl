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
// params: x = exposure, y = curve (0 = clamp), z and w unused.
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
} pc;

void main()
{
    // The scene target and the display image are the same extent, so this is
    // a 1:1 fetch with no filtering. Scaling to the swapchain stays a
    // separate blit.
    vec3 color = texelFetch(sceneHdrColor, ivec2(gl_FragCoord.xy), 0).rgb;

    color *= max(pc.params.x, 0.0);

    // F2b deliberately stops here. A straight clamp is what the 8-bit target
    // did implicitly, so the image is near enough unchanged and the range and
    // encode plumbing can be judged on its own. F2c is where a real curve
    // (ACES or Khronos PBR Neutral) and a user-facing exposure control land,
    // selected through params.y so both can be A/B'd against this.
    color = clamp(color, 0.0, 1.0);

    outColor = vec4(color, 1.0);
}
