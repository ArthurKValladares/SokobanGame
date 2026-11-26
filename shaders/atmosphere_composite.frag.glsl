#version 460
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 5) uniform sampler2D depthTexture;
layout(set = 0, binding = 13) uniform sampler2D atmosphereTexture;

layout(location = 0) out vec4 outColor;

#include "SceneFrame.glsl"

// The same block the integration pass was given; only these lanes are read.
layout(push_constant) uniform PushConstants
{
    mat4 worldFromClip;
    layout(offset = 64) vec4 volumeMinimumAndMode;
    layout(offset = 80) vec4 volumeMaximum;
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

// Whether the camera ray through `worldPosition` reaches a bounded medium's
// box before it reaches that position. The slab test is the integration
// pass's own, so the two agree about where a volume begins.
bool rayEntersVolumeBefore(vec3 worldPosition)
{
    vec3 camera = frame.cameraPositionAndNearPlane.xyz;
    vec3 toPosition = worldPosition - camera;
    float distanceToPosition = length(toPosition);
    if (distanceToPosition <= 0.0001) {
        return false;
    }
    vec3 rayDirection = toPosition / distanceToPosition;
    const float directionEpsilon = 0.000001;
    vec3 fallbackDirection = mix(
        vec3(-directionEpsilon),
        vec3(directionEpsilon),
        greaterThanEqual(rayDirection, vec3(0.0)));
    vec3 safeDirection = mix(
        fallbackDirection,
        rayDirection,
        greaterThan(abs(rayDirection), vec3(directionEpsilon)));
    vec3 first = (pc.volumeMinimumAndMode.xyz - camera) / safeDirection;
    vec3 second = (pc.volumeMaximum.xyz - camera) / safeDirection;
    vec3 nearPlanes = min(first, second);
    vec3 farPlanes = max(first, second);
    float rayStart = max(
        max(max(nearPlanes.x, nearPlanes.y), nearPlanes.z), 0.0);
    float rayEnd = min(min(farPlanes.x, farPlanes.y), farPlanes.z);
    return rayEnd > rayStart && rayStart < distanceToPosition;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 fullExtent = textureSize(depthTexture, 0);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(fullExtent);
    float centerDepth = texelFetch(depthTexture, pixel, 0).r;
    float centerDistance = endpointDistance(uv, centerDepth);
    bool centerSky = centerDepth >= 0.9999;

    // A surface in front of a bounded medium has none of it over it, and
    // that is decided here rather than left to the filter below. The filter
    // only knows what the neighbouring rays gathered: where a thin object
    // stands before a volume and every neighbour ran on into it, it would
    // have nothing matching to weigh and would fog the object by default.
    // This is what keeps the island mist off everything above its top.
    if (pc.volumeMinimumAndMode.w > 0.5 && !centerSky &&
        !rayEntersVolumeBefore(reconstructWorldPosition(uv, centerDepth))) {
        if (pc.targetExtent.z > 0.5) {
            outColor = texelFetch(sceneColor, pixel, 0);
        } else {
            // Blended as source + destination * alpha: the scene, unchanged.
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // Reconstruct the reduced-resolution medium at this pixel from a 3x3
    // texel neighbourhood centred on it.
    //
    // Per axis the three weights are (1 - f) / 2, 1 / 2 and f / 2, where f
    // is how far the pixel sits past the middle texel's leading edge. That
    // is bilinear interpolation of a two-texel box: it moves smoothly with
    // the pixel, and it always gives the even and the odd texels half the
    // weight each.
    //
    // The integration pass offsets each pixel's samples along the ray by a
    // 4x4 ordered dither. In that matrix the four parities of a texel (even
    // or odd column, even or odd row) each hold offsets from a different
    // quarter of one step. Equal weight on every parity therefore turns the
    // dither into four evenly spread extra samples per step. The plain
    // bilinear weights used before took a single texel at each texel centre,
    // which left the pattern visible as a weave in any medium dense enough
    // to see.
    //
    // atmosphereReconstructionWeights in AtmosphereMath.cpp is the CPU
    // reference, and atmosphereIntegrationRect keeps the integration scissor
    // wide enough for this reach.
    ivec2 mediumExtent = textureSize(atmosphereTexture, 0);
    vec2 mediumPosition = uv * vec2(mediumExtent) - 1.0;
    ivec2 first = ivec2(floor(mediumPosition));
    vec2 fraction = fract(mediumPosition);
    vec2 axisWeights[3] = vec2[3](
        0.5 * (vec2(1.0) - fraction),
        vec2(0.5),
        0.5 * fraction);
    vec4 medium = vec4(0.0);
    float totalWeight = 0.0;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            ivec2 coordinate = clamp(
                first + ivec2(x, y),
                ivec2(0),
                mediumExtent - ivec2(1));
            vec2 candidateUv =
                (vec2(coordinate) + 0.5) / vec2(mediumExtent);
            ivec2 candidatePixel = clamp(
                ivec2(candidateUv * vec2(fullExtent)),
                ivec2(0),
                fullExtent - ivec2(1));
            float candidateDepth =
                texelFetch(depthTexture, candidatePixel, 0).r;
            bool candidateSky = candidateDepth >= 0.9999;
            // Texels marched to a different surface belong to a different
            // ray length. Leaving them out keeps the medium attached to
            // geometry edges, at the cost of less smoothing right there.
            float depthWeight = centerSky == candidateSky
                ? exp(-abs(endpointDistance(candidateUv, candidateDepth) -
                    centerDistance) * 2.0)
                : 0.0;
            float weight =
                axisWeights[x].x * axisWeights[y].y * depthWeight;
            medium += texelFetch(
                atmosphereTexture, coordinate, 0) * weight;
            totalWeight += weight;
        }
    }
    if (totalWeight > 0.0001) {
        medium /= totalWeight;
    } else {
        // Level zero by name: this branch differs between neighbouring
        // fragments, so an implicit level of detail is undefined here.
        medium = textureLod(atmosphereTexture, uv, 0.0);
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
