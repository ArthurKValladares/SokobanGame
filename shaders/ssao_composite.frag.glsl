#version 460

// Applies the occlusion buffer to the scene.
//
// This used to be a multiply blend over the finished image, which darkened
// direct sunlight along with everything else. Screen-space occlusion is an
// estimate of *ambient* visibility, so it scales the ambient share of each
// pixel and nothing else. That share arrives in the scene target's alpha,
// written by the opaque pipelines - see writeAmbientMask in triangle.frag.
//
// A shader cannot read and write the same attachment. The normal opaque-only
// path therefore resolves the scene into sceneColor and composites into the
// HDR output target. Translucent content retains the copy fallback because it
// already samples sceneColor while resolving into the HDR target.
//
// It runs before the preview inset and the level transition, as the blended
// version did, because the occlusion buffer was built from the main view's
// depth and means nothing anywhere else.

layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 5) uniform sampler2D depthTexture;
layout(set = 0, binding = 6) uniform sampler2D ssaoTexture;
layout(location = 0) out vec4 outColor;

// params: x = strength, w = debug view (1 draws the filtered occlusion buffer,
// 2 draws the ambient mask this pass scales itself by). filterParams.z is the
// view-space plane-distance sigma.
layout(push_constant) uniform PushConstants
{
    mat4 viewFromClip;
    layout(offset = 128) vec4 params;
    layout(offset = 144) vec4 filterParams;
} pc;

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 view = pc.viewFromClip * vec4(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0,
        depth,
        1.0);
    return view.xyz / max(abs(view.w), 0.000001) * sign(view.w);
}

ivec2 depthCoordinate(vec2 requestedUv)
{
    ivec2 extent = textureSize(depthTexture, 0);
    return clamp(ivec2(requestedUv * vec2(extent)), ivec2(0), extent - 1);
}

vec2 depthTexelUv(ivec2 coordinate)
{
    return (vec2(coordinate) + 0.5) /
        vec2(textureSize(depthTexture, 0));
}

vec3 viewNormal(vec3 centerPosition)
{
    // Neighboring lanes already reconstructed their positions. Derivatives
    // reuse those values instead of fetching and unprojecting another two to
    // four depth texels per full-resolution pixel.
    vec3 normal = cross(dFdx(centerPosition), dFdy(centerPosition));
    float magnitudeSquared = dot(normal, normal);
    // The cross product is a screen-footprint area, so a fixed visual-scale
    // epsilon would reject valid surfaces as their pixel footprint shrinks.
    if (magnitudeSquared <= 1e-20) {
        return vec3(0.0, 0.0, -1.0);
    }
    normal *= inversesqrt(magnitudeSquared);
    return dot(normal, -centerPosition) < 0.0 ? -normal : normal;
}

float bilateralWeight(
    vec3 centerPosition,
    vec3 centerNormal,
    vec3 samplePosition,
    float spatialWeight)
{
    float depthSigma = max(pc.filterParams.z, 0.0001);
    float planeDistance = abs(dot(
        samplePosition - centerPosition, centerNormal));
    float normalizedDistance = planeDistance / depthSigma;
    float depthWeight = exp(-0.5 * normalizedDistance * normalizedDistance);
    return spatialWeight * depthWeight;
}

float bilateralAo(vec2 uv, vec3 centerPosition, vec3 centerNormal)
{
    ivec2 aoSize = textureSize(ssaoTexture, 0);
    vec2 aoPosition = uv * vec2(aoSize) - 0.5;
    ivec2 base = ivec2(floor(aoPosition));
    vec2 fraction = fract(aoPosition);
    float weightedAo = 0.0;
    float weightSum = 0.0;

    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            ivec2 coordinate = clamp(
                base + ivec2(x, y), ivec2(0), aoSize - ivec2(1));
            vec2 sampleUv = (vec2(coordinate) + 0.5) / vec2(aoSize);
            ivec2 sampleDepthCoordinate = depthCoordinate(sampleUv);
            float sampleDepth = texelFetch(
                depthTexture, sampleDepthCoordinate, 0).r;
            if (sampleDepth >= 0.9999) {
                continue;
            }

            vec3 samplePosition = reconstructViewPosition(
                depthTexelUv(sampleDepthCoordinate), sampleDepth);
            float spatialWeight =
                (x == 0 ? 1.0 - fraction.x : fraction.x) *
                (y == 0 ? 1.0 - fraction.y : fraction.y);
            float weight = bilateralWeight(
                centerPosition,
                centerNormal,
                samplePosition,
                spatialWeight);
            weightedAo += texelFetch(ssaoTexture, coordinate, 0).r * weight;
            weightSum += weight;
        }
    }

    ivec2 fallback = clamp(
        ivec2(uv * vec2(aoSize)), ivec2(0), aoSize - ivec2(1));
    return weightSum > 0.0001
        ? weightedAo / weightSum
        : texelFetch(ssaoTexture, fallback, 0).r;
}

void main()
{
    vec2 fullExtent = vec2(textureSize(depthTexture, 0));
    vec2 uv = gl_FragCoord.xy / fullExtent;
    ivec2 centerDepthCoordinate = depthCoordinate(uv);
    float centerDepth = texelFetch(
        depthTexture, centerDepthCoordinate, 0).r;
    vec3 centerPosition = reconstructViewPosition(
        depthTexelUv(centerDepthCoordinate), centerDepth);
    vec3 centerNormal = viewNormal(centerPosition);

    // Four half-resolution candidates form the native bilinear footprint.
    // Distance from the center surface's view-space plane rejects samples
    // across depth discontinuities before normalization. Reconstructing a
    // second normal for every candidate gave no visible edge improvement and
    // dominated this full-resolution pass.
    float ao = centerDepth < 0.9999
        ? bilateralAo(uv, centerPosition, centerNormal)
        : 1.0;

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
