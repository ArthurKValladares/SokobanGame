#version 460
#extension GL_GOOGLE_include_directive : require

// HDR scene snapshot and resolved scene depth. This pass writes the live HDR
// target, so sampling the snapshot avoids a read/write attachment feedback
// loop.
layout(set = 0, binding = 0) uniform sampler2D shadowMap;
layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 5) uniform sampler2D depthTexture;
layout(set = 0, binding = 8) uniform samplerCubeArray pointShadowMaps;

layout(location = 0) out vec4 outColor;

#include "SceneFrame.glsl"

#define POINT_SHADOW_TAPS 1
#include "PointShadow.glsl"

// Reuses the fixed 256-byte GpuDrawInstance push range. Only the lanes named
// here belong to this pass.
layout(push_constant) uniform PushConstants
{
    mat4 worldFromClip;
    layout(offset = 128) vec4 mediumColorAndDensity;
    layout(offset = 144) vec4 sunRadianceAndStrength;
    layout(offset = 160) vec4 sunDirectionAndAnisotropy;
    layout(offset = 176) vec4 heightAndDistance;
    layout(offset = 192) vec4 shadowOptions;
    layout(offset = 208) vec4 ambientRadiance;
} pc;

const int maximumSampleCount = 32;

vec3 reconstructWorldPosition(vec2 uv, float depth)
{
    vec4 world = pc.worldFromClip * vec4(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0,
        depth,
        1.0);
    return world.xyz / max(abs(world.w), 0.000001) * sign(world.w);
}

float bayer4x4(ivec2 pixel)
{
    const float values[16] = float[16](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 wrapped = pixel & ivec2(3);
    return (values[wrapped.y * 4 + wrapped.x] + 0.5) / 16.0;
}

float phaseFunction(float cosine)
{
    float g = clamp(pc.sunDirectionAndAnisotropy.w, -0.85, 0.85);
    float denominator = max(
        1.0 + g * g - 2.0 * g * clamp(cosine, -1.0, 1.0),
        0.0001);
    // Four-pi-scaled Henyey-Greenstein: g=0 is exactly one.
    return (1.0 - g * g) /
        (denominator * sqrt(denominator));
}

float sunVisibility(vec3 worldPosition)
{
    if (pc.shadowOptions.x <= 0.5) {
        return 1.0;
    }
    vec4 projected = frame.shadowFromWorld * vec4(worldPosition, 1.0);
    if (abs(projected.w) <= 0.0001) {
        return 1.0;
    }
    vec3 shadowCoord = projected.xyz / projected.w;
    vec2 uv = shadowCoord.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) ||
        any(greaterThan(uv, vec2(1.0))) ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }
    float closestDepth = texture(shadowMap, uv).r;
    float shadowed = shadowCoord.z - max(pc.shadowOptions.z, 0.0) >
        closestDepth ? 1.0 : 0.0;
    return 1.0 - shadowed * clamp(pc.shadowOptions.y, 0.0, 1.0);
}

vec3 pointLightRadiance(vec3 worldPosition, vec3 rayDirection)
{
    vec3 radiance = vec3(0.0);
    int count = clamp(int(frame.pointLightMeta.x + 0.5), 0, 8);
    for (int lightIndex = 0; lightIndex < count; ++lightIndex) {
        PointLightData light = frame.pointLights[lightIndex];
        vec3 toLight = light.positionAndRange.xyz - worldPosition;
        float distanceToLight = length(toLight);
        float range = max(light.positionAndRange.w, 0.001);
        if (distanceToLight <= 0.0001 || distanceToLight >= range) {
            continue;
        }
        vec3 lightDirection = toLight / distanceToLight;
        float normalizedDistance = distanceToLight / range;
        float rangeWindow = max(
            1.0 - normalizedDistance * normalizedDistance *
                normalizedDistance * normalizedDistance,
            0.0);
        float attenuation = rangeWindow * rangeWindow /
            max(distanceToLight * distanceToLight, 0.16);
        float visibility = pointShadowFactor(
            lightIndex,
            -toLight,
            lightDirection);
        radiance += light.colorAndIntensity.rgb *
            light.colorAndIntensity.w * attenuation * visibility *
            phaseFunction(dot(rayDirection, lightDirection));
    }
    // Keep a ray passing almost exactly through an emitter finite. Surface
    // shading can carry a sharp highlight; a participating medium cannot.
    return min(radiance, vec3(12.0));
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 extent = textureSize(depthTexture, 0);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(extent);
    float depth = texelFetch(depthTexture, pixel, 0).r;
    vec4 scene = texelFetch(sceneColor, pixel, 0);

    vec3 camera = frame.cameraPositionAndNearPlane.xyz;
    // Pull a background sample slightly inside the far plane so an infinite
    // perspective far plane cannot produce a zero homogeneous divisor.
    float reconstructionDepth = depth >= 0.9999 ? 0.999 : depth;
    vec3 endpoint = reconstructWorldPosition(uv, reconstructionDepth);
    vec3 cameraToEndpoint = endpoint - camera;
    float endpointDistance = length(cameraToEndpoint);
    if (endpointDistance <= 0.0001) {
        outColor = scene;
        return;
    }

    vec3 rayDirection = cameraToEndpoint / endpointDistance;
    float rayLength = min(endpointDistance, max(pc.heightAndDistance.z, 0.0));
    if (rayLength <= 0.0001 || pc.mediumColorAndDensity.w <= 0.0 ||
        pc.sunRadianceAndStrength.w <= 0.0) {
        outColor = scene;
        return;
    }

    int activeSampleCount = clamp(
        int(pc.heightAndDistance.w + 0.5), 1, maximumSampleCount);
    float stepLength = rayLength / float(activeSampleCount);
    float jitter = bayer4x4(pixel);
    float transmittance = 1.0;
    vec3 inScattering = vec3(0.0);
    vec3 sunDirection = length(pc.sunDirectionAndAnisotropy.xyz) > 0.0001
        ? normalize(pc.sunDirectionAndAnisotropy.xyz)
        : vec3(0.0, 0.0, 1.0);
    float sunPhase = phaseFunction(dot(rayDirection, sunDirection));

    for (int sampleIndex = 0;
         sampleIndex < maximumSampleCount;
         ++sampleIndex) {
        if (sampleIndex >= activeSampleCount) {
            break;
        }
        float distanceAlongRay =
            (float(sampleIndex) + jitter) * stepLength;
        vec3 samplePosition = camera + rayDirection * distanceAlongRay;
        float altitude = max(
            samplePosition.z - pc.heightAndDistance.y,
            0.0);
        float density = max(pc.mediumColorAndDensity.w, 0.0) * exp(
            -max(pc.heightAndDistance.x, 0.0) * altitude);
        float stepTransmittance = exp(-density * stepLength);
        float scatteredFraction = 1.0 - stepTransmittance;

        vec3 directRadiance = pc.sunRadianceAndStrength.rgb *
            sunPhase * sunVisibility(samplePosition);
        directRadiance += pointLightRadiance(samplePosition, rayDirection);
        vec3 illumination = pc.ambientRadiance.rgb + directRadiance;
        vec3 scatteredRadiance = pc.mediumColorAndDensity.rgb *
            illumination * pc.sunRadianceAndStrength.w;
        inScattering += transmittance * scatteredFraction *
            scatteredRadiance;
        transmittance *= stepTransmittance;
    }

    outColor = vec4(scene.rgb * transmittance + inScattering, scene.a);
}
