#ifndef SOKOBAN_SCENE_FRAME_GLSL
#define SOKOBAN_SCENE_FRAME_GLSL

// Camera and lighting for the whole frame. Mirrors SceneFrameUniform and
// PointLightUniform in VulkanRenderConstants.hpp, whose static_asserts pin the
// 10432-byte, 48-byte, and 32-byte sizes.
struct PointLightData
{
    vec4 positionAndRange;
    // rgb is linear radiance with intensity already applied. w is 1/range^2;
    // positionAndRange.w retains the range needed by cube-shadow depth.
    vec4 radianceAndInverseRangeSquared;
    vec4 shadowOptions;
};
struct RockScratchData
{
    vec4 startAndAge;
    vec4 endPatternAndDuration;
};
struct WaterContactData
{
    vec4 centerAndElevation;
    vec4 halfExtentAndRadius;
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
    RockScratchData rockScratches[256];
    // x=count, y=lifetime, z=hold before fading, w=material opacity per pass.
    vec4 rockScratchMeta;
    vec4 waterRippleStyles[16];
    WaterContactData waterContacts[32];
    vec4 waterContactMeta;
    // The cloud light; see CloudLight.glsl.
    // xy slides a position along its sun ray to height zero, per unit of
    // height. z = pattern frequency, w = detail frequency, per world unit.
    vec4 cloudLightProjection;
    // xy = how far the clouds have drifted, z = coverage, w = edge softness.
    vec4 cloudLightShape;
    // x = dimming on surfaces, y = strength of the detail the air sees. A
    // medium's own dimming arrives with that medium's draw.
    vec4 cloudLightStrength;
    // xy = unit direction the detail is drawn out along, z = how much lower
    // its frequency is that way.
    vec4 cloudLightRays;
    // The wind that sways grass and foliage (WindSway.glsl).
    // xy = unit direction the wind blows toward, z = the view's effect clock.
    vec4 windDirectionAndTime;
    // x = how far a tip moves in a full gust, in world units; zero is no
    // wind. y = radians of gust per world unit downwind, z = radians of
    // gust per second, w = the quick flutter's share of the sway.
    vec4 windSway;
} frame;

#endif
