#ifndef SOKOBAN_SCENE_FRAME_GLSL
#define SOKOBAN_SCENE_FRAME_GLSL

// Camera and lighting for the whole frame. Mirrors SceneFrameUniform and
// PointLightUniform in VulkanRenderConstants.hpp, whose static_asserts pin the
// 544-byte and 48-byte sizes.
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
} frame;

#endif
