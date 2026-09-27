#version 460
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 5) uniform sampler2D depthTexture;
layout(set = 0, binding = 13) uniform sampler2D atmosphereTexture;

layout(location = 0) out vec4 outColor;

#include "SceneFrame.glsl"

layout(push_constant) uniform PushConstants
{
    mat4 worldFromClip;
    layout(offset = 224) vec4 targetExtent;
} pc;

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec4 world = pc.worldFromClip * vec4(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0,
        depth >= 0.9999 ? 0.999 : depth,
        1.0);
    return world.xyz / max(abs(world.w), 0.000001) * sign(world.w);
}

float endpointDistance(vec2 uv, float depth)
{
    return length(reconstructWorldPosition(uv, depth) -
        frame.cameraPositionAndNearPlane.xyz);
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 fullExtent = textureSize(depthTexture, 0);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(fullExtent);
    float centerDepth = texelFetch(depthTexture, pixel, 0).r;
    float centerDistance = endpointDistance(uv, centerDepth);
    bool centerSky = centerDepth >= 0.9999;

    ivec2 halfExtent = textureSize(atmosphereTexture, 0);
    vec2 halfPosition = uv * vec2(halfExtent) - 0.5;
    ivec2 first = ivec2(floor(halfPosition));
    vec2 fraction = fract(halfPosition);
    vec4 medium = vec4(0.0);
    float totalWeight = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            ivec2 coordinate = clamp(
                first + ivec2(x, y),
                ivec2(0),
                halfExtent - ivec2(1));
            vec2 candidateUv =
                (vec2(coordinate) + 0.5) / vec2(halfExtent);
            ivec2 candidatePixel = clamp(
                ivec2(candidateUv * vec2(fullExtent)),
                ivec2(0),
                fullExtent - ivec2(1));
            float candidateDepth =
                texelFetch(depthTexture, candidatePixel, 0).r;
            bool candidateSky = candidateDepth >= 0.9999;
            float depthWeight = centerSky == candidateSky
                ? exp(-abs(endpointDistance(candidateUv, candidateDepth) -
                    centerDistance) * 2.0)
                : 0.0;
            vec2 axisWeight = mix(
                vec2(1.0) - fraction,
                fraction,
                vec2(x, y));
            float weight = axisWeight.x * axisWeight.y * depthWeight;
            medium += texelFetch(
                atmosphereTexture, coordinate, 0) * weight;
            totalWeight += weight;
        }
    }
    if (totalWeight > 0.0001) {
        medium /= totalWeight;
    } else {
        medium = texture(atmosphereTexture, uv);
    }

    float transmittance = clamp(medium.a, 0.0, 1.0);
    if (pc.targetExtent.z > 0.5) {
        // The mirror-over-fog path composites into the preserved multisample
        // scene and resolves it again. Its source is the already-resolved scene
        // (including SSAO and earlier media), so it still replaces the target.
        vec4 scene = texelFetch(sceneColor, pixel, 0);
        outColor = vec4(
            scene.rgb * transmittance + medium.rgb,
            scene.a);
    } else {
        // The ordinary single-sample pipeline uses fixed-function blending:
        // source.rgb + destination.rgb * source.a, preserving destination
        // alpha. It avoids a scene snapshot and texture read for every medium.
        outColor = vec4(medium.rgb, transmittance);
    }
}
