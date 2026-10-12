#ifndef SOKOBAN_SCENE_FRAME_GLSL
#define SOKOBAN_SCENE_FRAME_GLSL

// Camera and lighting for the whole frame. Mirrors SceneFrameUniform and
// PointLightUniform in VulkanRenderConstants.hpp, whose static_asserts pin the
// 832-byte and 48-byte sizes.
struct PointLightData
{
    vec4 positionAndRange;
    // rgb is linear radiance with intensity already applied. w is 1/range^2;
    // positionAndRange.w retains the range needed by cube-shadow depth.
    vec4 radianceAndInverseRangeSquared;
    vec4 shadowOptions;
};
layout(std140, set = 0, binding = 7) uniform SceneFrame
{
    mat4 clipFromWorld;
    mat4 shadowFromWorld;
    vec4 cameraPositionAndNearPlane;
    PointLightData pointLights[8];
    vec4 pointLightMeta;
    vec4 waterRipples[16];
    vec4 waterRippleMeta;
    // x = diffuse softness, y = normal detail, z = roughness floor.
    // (0, 1, 0) preserves the authored PBR response for studio frames.
    vec4 stylizedSurface;
} frame;

#endif
