#version 460
#extension GL_GOOGLE_include_directive : require

// Resolved scene depth drives the reduced-resolution volumetric integration.
// The result is scattering.rgb + transmittance.a; a separate depth-aware
// fullscreen pass composites it over the unblurred HDR scene.
layout(set = 0, binding = 0) uniform sampler2D shadowMap;
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
    layout(offset = 64) vec4 volumeMinimumAndMode;
    layout(offset = 80) vec4 volumeMaximum;
    layout(offset = 96) vec4 revealOriginRadiusAndFeather;
    layout(offset = 112) vec4 volumeAnimation;
    layout(offset = 128) vec4 mediumColorAndDensity;
    layout(offset = 144) vec4 sunRadianceAndStrength;
    layout(offset = 160) vec4 sunDirectionAndAnisotropy;
    layout(offset = 176) vec4 heightAndDistance;
    layout(offset = 192) vec4 shadowOptions;
    layout(offset = 208) vec4 ambientRadiance;
    layout(offset = 224) vec4 targetExtent;
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

float fogNoiseHash(vec3 position)
{
    // Hash lattice coordinates without trigonometry. Keeping this cheap is
    // important because it is evaluated at every volumetric ray-march step.
    position = fract(position * 0.1031);
    position += dot(position, position.yzx + 33.33);
    return fract((position.x + position.y) * position.z);
}

float fogValueNoise(vec3 position)
{
    vec3 cell = floor(position);
    vec3 local = fract(position);
    vec3 blend = local * local * (3.0 - 2.0 * local);

    float lower00 = mix(
        fogNoiseHash(cell + vec3(0.0, 0.0, 0.0)),
        fogNoiseHash(cell + vec3(1.0, 0.0, 0.0)), blend.x);
    float lower10 = mix(
        fogNoiseHash(cell + vec3(0.0, 1.0, 0.0)),
        fogNoiseHash(cell + vec3(1.0, 1.0, 0.0)), blend.x);
    float upper00 = mix(
        fogNoiseHash(cell + vec3(0.0, 0.0, 1.0)),
        fogNoiseHash(cell + vec3(1.0, 0.0, 1.0)), blend.x);
    float upper10 = mix(
        fogNoiseHash(cell + vec3(0.0, 1.0, 1.0)),
        fogNoiseHash(cell + vec3(1.0, 1.0, 1.0)), blend.x);
    float lower = mix(lower00, lower10, blend.y);
    float upper = mix(upper00, upper10, blend.y);
    return mix(lower, upper, blend.z);
}

float fogFractalNoise(vec3 position)
{
    float broadBillows = fogValueNoise(position);
    float fineDetail = fogValueNoise(
        position * 2.07 + vec3(19.1, -7.3, 11.7));
    return broadBillows * 0.68 + fineDetail * 0.32;
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

float pointLightVisibility(
    PointLightData light,
    vec3 fromLight,
    vec3 directionFromLight)
{
    if (light.shadowOptions.x <= 0.5) {
        return 1.0;
    }
    const float nearPlane = POINT_SHADOW_NEAR_PLANE;
    float farPlane = max(light.positionAndRange.w, nearPlane + 0.001);
    float majorDistance = max(
        abs(fromLight.x), max(abs(fromLight.y), abs(fromLight.z)));
    if (majorDistance <= nearPlane || majorDistance >= farPlane) {
        return 1.0;
    }

    // A participating medium has no surface normal. The old call supplied
    // the direction to the light, which made the shared surface bias compute
    // a facing of exactly one after normalizing the same vector twice. Keep
    // that result directly and reuse the already-normalized light direction.
    float worldBias = max(light.shadowOptions.z, 0.0);
    float closestDepth = texture(
        pointShadowMaps,
        vec4(directionFromLight, light.shadowOptions.y)).r;
    float closestDistance = pointShadowWorldDistance(
        closestDepth, nearPlane, farPlane);
    float shadowed = majorDistance - worldBias > closestDistance
        ? 1.0
        : 0.0;
    return 1.0 - shadowed * clamp(light.shadowOptions.w, 0.0, 1.0);
}

vec3 pointLightRadiance(vec3 worldPosition, vec3 rayDirection)
{
    vec3 radiance = vec3(0.0);
    int count = clamp(int(frame.pointLightMeta.x + 0.5), 0, 8);
    for (int lightIndex = 0; lightIndex < count; ++lightIndex) {
        PointLightData light = frame.pointLights[lightIndex];
        vec3 toLight = light.positionAndRange.xyz - worldPosition;
        float distanceSquared = dot(toLight, toLight);
        float range = max(light.positionAndRange.w, 0.001);
        float rangeSquared = range * range;
        if (distanceSquared <= 0.00000001 ||
            distanceSquared >= rangeSquared) {
            continue;
        }
        vec3 lightDirection = toLight * inversesqrt(distanceSquared);
        float normalizedDistanceSquared = distanceSquared / rangeSquared;
        float rangeWindow = max(
            1.0 - normalizedDistanceSquared * normalizedDistanceSquared,
            0.0);
        float attenuation = rangeWindow * rangeWindow /
            max(distanceSquared, 0.16);
        float visibility = pointLightVisibility(
            light,
            -toLight,
            -lightDirection);
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
    ivec2 extent = max(ivec2(pc.targetExtent.xy + 0.5), ivec2(1));
    vec2 uv = (vec2(pixel) + 0.5) / vec2(extent);
    ivec2 depthExtent = textureSize(depthTexture, 0);
    ivec2 depthPixel = clamp(
        ivec2(uv * vec2(depthExtent)),
        ivec2(0),
        depthExtent - ivec2(1));
    float depth = texelFetch(depthTexture, depthPixel, 0).r;

    vec3 camera = frame.cameraPositionAndNearPlane.xyz;
    // Pull a background sample slightly inside the far plane so an infinite
    // perspective far plane cannot produce a zero homogeneous divisor.
    float reconstructionDepth = depth >= 0.9999 ? 0.999 : depth;
    vec3 endpoint = reconstructWorldPosition(uv, reconstructionDepth);
    vec3 cameraToEndpoint = endpoint - camera;
    float endpointDistance = length(cameraToEndpoint);
    if (endpointDistance <= 0.0001) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 rayDirection = cameraToEndpoint / endpointDistance;
    bool boundedVolume = pc.volumeMinimumAndMode.w > 0.5;
    float rayStart = 0.0;
    float rayEnd = min(
        endpointDistance,
        max(pc.heightAndDistance.z, 0.0));
    if (boundedVolume) {
        // Slab intersection against the screen's world-space fog column.
        // Preserve a sign for axis-aligned rays so division stays finite.
        const float directionEpsilon = 0.000001;
        vec3 fallbackDirection = mix(
            vec3(-directionEpsilon),
            vec3(directionEpsilon),
            greaterThanEqual(rayDirection, vec3(0.0)));
        vec3 safeDirection = mix(
            fallbackDirection,
            rayDirection,
            greaterThan(abs(rayDirection), vec3(directionEpsilon)));
        vec3 first =
            (pc.volumeMinimumAndMode.xyz - camera) / safeDirection;
        vec3 second =
            (pc.volumeMaximum.xyz - camera) / safeDirection;
        vec3 nearPlanes = min(first, second);
        vec3 farPlanes = max(first, second);
        rayStart = max(max(nearPlanes.x, nearPlanes.y), nearPlanes.z);
        rayEnd = min(min(farPlanes.x, farPlanes.y), farPlanes.z);
        rayStart = max(rayStart, 0.0);
        rayEnd = min(rayEnd, endpointDistance);
    }
    float rayLength = rayEnd - rayStart;
    if (rayLength <= 0.0001 || pc.mediumColorAndDensity.w <= 0.0 ||
        pc.sunRadianceAndStrength.w <= 0.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
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
            rayStart + (float(sampleIndex) + jitter) * stepLength;
        vec3 samplePosition = camera + rayDirection * distanceAlongRay;
        float altitude = max(
            samplePosition.z - pc.heightAndDistance.y,
            0.0);
        float density = max(pc.mediumColorAndDensity.w, 0.0) * exp(
            -max(pc.heightAndDistance.x, 0.0) * altitude);
        float colorValueVariation = 1.0;
        if (boundedVolume) {
            // Sample slowly advected 3D noise in world space. The camera can
            // move independently without the pattern swimming across the
            // screen, while the time offset gives the fog a gentle drift.
            float noiseScale = max(pc.volumeAnimation.z, 0.0001);
            vec3 noisePosition = samplePosition * noiseScale +
                pc.volumeAnimation.x * vec3(0.37, -0.23, 0.17);
            float fogNoise = smoothstep(
                0.32, 0.68, fogFractalNoise(noisePosition));
            float signedNoise = fogNoise * 2.0 - 1.0;
            float noiseStrength = clamp(pc.volumeAnimation.w, 0.0, 0.8);
            density *= max(1.0 + signedNoise * noiseStrength, 0.2);

            // An independent field changes value only: every RGB component
            // receives the same multiplier, preserving the fog hue while
            // producing a wider range of light and dark greys. Its distinct
            // scale, offset, and motion keep it from tracing density billows.
            float colorNoiseScale = max(pc.volumeMaximum.w, 0.0001);
            vec3 colorNoisePosition = samplePosition * colorNoiseScale +
                pc.ambientRadiance.w * vec3(-0.21, 0.29, -0.13) +
                vec3(37.1, -19.7, 8.3);
            float colorNoise = smoothstep(
                0.3, 0.7, fogFractalNoise(colorNoisePosition));
            float colorNoiseStrength = clamp(pc.shadowOptions.w, 0.0, 0.75);
            colorValueVariation = mix(
                1.0 - colorNoiseStrength,
                1.0 + colorNoiseStrength,
                colorNoise);

            // volumeMinimum/Maximum include a horizontal feather beyond the
            // authored screen. Distance is zero throughout the screen itself,
            // so the fade never exposes any of its tiles prematurely.
            float edgeFade = max(pc.volumeAnimation.y, 0.0001);
            vec2 coveredMinimum =
                pc.volumeMinimumAndMode.xy + vec2(edgeFade);
            vec2 coveredMaximum = pc.volumeMaximum.xy - vec2(edgeFade);
            vec2 outsideCoveredArea = max(
                max(coveredMinimum - samplePosition.xy,
                    samplePosition.xy - coveredMaximum),
                vec2(0.0));
            density *= 1.0 - smoothstep(
                0.0, edgeFade, length(outsideCoveredArea));
        }
        if (boundedVolume && pc.revealOriginRadiusAndFeather.z >= 0.0) {
            float revealDistance = distance(
                samplePosition.xy,
                pc.revealOriginRadiusAndFeather.xy);
            density *= smoothstep(
                pc.revealOriginRadiusAndFeather.z,
                pc.revealOriginRadiusAndFeather.z +
                    max(pc.revealOriginRadiusAndFeather.w, 0.0001),
                revealDistance);
        }
        float stepTransmittance = exp(-density * stepLength);
        float scatteredFraction = 1.0 - stepTransmittance;

        vec3 directRadiance = pc.sunRadianceAndStrength.rgb *
            sunPhase * sunVisibility(samplePosition);
        directRadiance += pointLightRadiance(samplePosition, rayDirection);
        vec3 illumination = pc.ambientRadiance.rgb + directRadiance;
        vec3 scatteredRadiance = pc.mediumColorAndDensity.rgb *
            illumination * pc.sunRadianceAndStrength.w *
            colorValueVariation;
        inScattering += transmittance * scatteredFraction *
            scatteredRadiance;
        transmittance *= stepTransmittance;
    }

    outColor = vec4(inScattering, transmittance);
}
