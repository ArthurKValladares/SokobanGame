#version 460

// Applies the occlusion buffer to the scene.
//
// This used to be a multiply blend over the finished image, which darkened
// direct sunlight along with everything else. Screen-space occlusion is an
// estimate of *ambient* visibility, so it scales the ambient share of each
// pixel and nothing else. That share arrives in the scene target's alpha,
// written by the opaque pipelines - see writeAmbientMask in triangle.frag.
//
// A shader cannot read and write the same attachment, so the pass samples the
// snapshot copyResolvedSceneColor leaves in sceneColor and writes the scene
// target unblended. That copy is the price of having no depth prepass and no
// G-buffer to reconstruct an ambient term from; either would remove it.
//
// It runs before the preview inset and the level transition, as the blended
// version did, because the occlusion buffer was built from the main view's
// depth and means nothing anywhere else.

layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 6) uniform sampler2D ssaoTexture;
layout(location = 0) out vec4 outColor;

// params: x = strength, w = debug view (1 draws the raw occlusion buffer,
// 2 draws the ambient mask this pass scales itself by).
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
} pc;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(ssaoTexture, 0));
    vec2 uv = gl_FragCoord.xy * texel;

    // 5x5 box blur smooths the per-pixel noise from the AO pass. Still not
    // depth-aware, so occlusion bleeds across silhouettes; packet 8.2 replaces
    // it with the half-resolution bilateral path.
    float ao = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ao += texture(ssaoTexture, uv + vec2(float(x), float(y)) * texel).r;
        }
    }
    ao /= 25.0;

    // The scene target and the AO buffer are the same extent, so this is a
    // 1:1 fetch.
    vec4 scene = texelFetch(sceneColor, ivec2(gl_FragCoord.xy), 0);

    if (pc.params.w > 1.5) {
        // The ambient mask, which is otherwise invisible: it lives in the
        // scene target's alpha and only ever shows up as a difference in how
        // hard this pass darkens a pixel.
        outColor = vec4(scene.a, scene.a, scene.a, 1.0);
        return;
    }
    if (pc.params.w > 0.5) {
        outColor = vec4(ao, ao, ao, 1.0);
        return;
    }

    float factor = mix(1.0, ao, clamp(pc.params.x, 0.0, 1.0) * scene.a);
    // Alpha passes through: the mask has to survive for anything downstream
    // that still cares, and overwriting it would be silently wrong.
    outColor = vec4(scene.rgb * factor, scene.a);
}
