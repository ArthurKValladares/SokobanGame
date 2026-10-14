#version 460
#extension GL_GOOGLE_include_directive : require

#include "SceneFrame.glsl"

layout(location = 0) out vec4 outColor;

// The same push range as GpuDrawInstance; the sky needs no texture, mesh or
// persistent target. The viewport includes the origin of a screen-preview inset.
layout(push_constant) uniform PushConstants
{
    mat4 worldFromClip;
    layout(offset = 64) vec4 viewport;
    layout(offset = 80) vec4 animation;
} pc;

float hashCloud(ivec2 cell)
{
    // Adjacent cells must agree exactly on their shared corners. Floating
    // hashes can round (cell + 1) * constant differently after GPU multiply-add
    // contraction, amplifying the difference into visible lattice seams.
    uvec2 p = uvec2(cell);
    uint h = (p.x * 0x8da6b343u) ^ (p.y * 0xd8163841u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    // Keep 24 bits so conversion to float is exact, including negative cells.
    return float(h >> 8) * (1.0 / 16777216.0);
}

// Very broad value noise for the smooth atmosphere below the cloud banks.
float atmosphereNoise(vec2 p)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hashCloud(cell);
    float b = hashCloud(cell + ivec2(1, 0));
    float c = hashCloud(cell + ivec2(0, 1));
    float d = hashCloud(cell + ivec2(1));
    float k = a - b - c + d;
    return a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y;
}

vec3 cloudSimplexCorner(ivec2 cell, vec2 offset)
{
    const float diagonal = 0.7071067812;
    const vec2 gradients[8] = vec2[](
        vec2(1.0, 0.0), vec2(-1.0, 0.0),
        vec2(0.0, 1.0), vec2(0.0, -1.0),
        vec2(diagonal, diagonal), vec2(-diagonal, diagonal),
        vec2(diagonal, -diagonal), vec2(-diagonal, -diagonal));
    vec2 gradient = gradients[int(hashCloud(cell) * 8.0)];
    float radius = max(0.5 - dot(offset, offset), 0.0);
    float radius2 = radius * radius;
    float radius3 = radius2 * radius;
    float radius4 = radius2 * radius2;
    float projection = dot(gradient, offset);
    return vec3(radius4 * projection,
        radius4 * gradient - 8.0 * radius3 * projection * offset);
}

vec3 cloudBillowNoise(vec2 p)
{
    // Radial contributions on a triangular lattice avoid the square plateaus
    // of interpolated value noise. Shared integer corner hashes and analytic
    // gradients keep the contours and their lighting continuous while drifting.
    const float skew = 0.3660254038;
    const float unskew = 0.2113248654;
    ivec2 cell = ivec2(floor(p + (p.x + p.y) * skew));
    vec2 offset = p - (vec2(cell) - float(cell.x + cell.y) * unskew);
    ivec2 step = offset.x > offset.y ? ivec2(1, 0) : ivec2(0, 1);
    vec3 field = cloudSimplexCorner(cell, offset)
        + cloudSimplexCorner(cell + step, offset - vec2(step) + unskew)
        + cloudSimplexCorner(cell + ivec2(1), offset - 1.0 + 2.0 * unskew);
    // Match the former value-noise contrast so coverage remains equally open.
    return vec3(0.5, 0.0, 0.0) + field * 41.664;
}

vec3 cloudField(vec2 p, float time)
{
    // Independent octave motion gently changes the contours as wind advects
    // the entire bank. The broad octave dominates for soft, readable masses.
    vec3 field = vec3(0.0);
    float frequency = 0.42;
    float amplitude = 0.57;
    const mat2 octaveRotation = mat2(0.8, 0.6, -0.6, 0.8);
    mat2 basis = octaveRotation;
    for (int octave = 0; octave < 4; ++octave) {
        vec3 n = cloudBillowNoise((basis * p) * frequency +
            vec2(13.7, 9.2) * float(octave) +
            time * vec2(0.008, -0.005) * float(octave));
        field += vec3(n.x, transpose(basis) * n.yz * frequency) * amplitude;
        frequency *= 2.03;
        amplitude *= 0.48;
        basis *= octaveRotation;
    }
    return field / 1.039;
}

vec3 compositeCloudLayer(vec3 below, vec3 camera, vec3 ray,
    float height, float scale, vec2 wind, vec2 seed,
    float coverage, float opacity, float haze, vec3 shade, vec3 lit)
{
    // Horizon/upward rays see the gradient sky. Fade out before an almost
    // horizontal intersection can magnify the noise into subpixel flicker.
    float visibility = smoothstep(0.015, 0.16, -ray.z);
    if (visibility <= 0.0 || camera.z <= height) {
        return below;
    }
    float distanceToLayer = (height - camera.z) / min(ray.z, -0.001);
    vec2 world = camera.xy + ray.xy * distanceToLayer;
    float time = pc.animation.x;
    vec2 p = world * scale + wind * time + seed;
    // Broad, smooth warping keeps the billows irregular across all octaves.
    const float warpFrequency = 0.18;
    const float warpStrength = 1.2;
    vec3 warpX = cloudBillowNoise(p * warpFrequency + 17.2);
    vec3 warpY = cloudBillowNoise(p * warpFrequency - 8.4);
    vec2 warp = vec2(warpX.x, warpY.x) - 0.5;
    vec3 field = cloudField(p + warp * warpStrength, time);
    // Apply the warp's Jacobian so shading follows the curved contours.
    vec2 slope = field.yz;
    field.yz += warpStrength * warpFrequency *
        (slope.x * warpX.yz + slope.y * warpY.yz);
    float body = smoothstep(coverage, coverage + 0.25, field.x);
    float alpha = (1.0 - exp(-body * opacity)) * visibility;

    vec3 normal = normalize(vec3(-field.yz * 1.35, 1.0));
    vec3 sun = normalize(vec3(-0.55, -0.45, 0.70));
    float light = clamp(dot(normal, sun), 0.0, 1.0);
    vec3 color = mix(shade, lit, 0.24 + light * 0.76);
    // Thin wisps blend into lavender air; the distant sea loses contrast.
    color = mix(color, vec3(0.52, 0.48, 0.68), haze);
    // Slightly weathered gray separates clouds from the richer island colors
    // while preserving a trace of peach light and blue-violet shadow.
    float gray = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(color, vec3(gray), 0.22);
    return mix(below, color, alpha);
}

vec3 lowerAtmosphere(vec3 sky, vec3 camera, vec3 ray)
{
    // Broad strata below the clouds give the air its own world-space parallax.
    // Shallow views catch the upper peach haze; looking farther down reveals
    // deeper, darker blue-violet air. Depth follows world down, not screen Y.
    const float heights[3] = float[](-115.0, -190.0, -300.0);
    float visibility = smoothstep(0.015, 0.16, -ray.z);
    if (visibility <= 0.0 || camera.z <= heights[0]) {
        return sky;
    }
    float depth = smoothstep(0.10, 0.95, -ray.z);
    // Overlapping weights avoid visible bands between the atmosphere strata.
    vec3 weights = vec3((1.0 - depth) * (1.0 - depth),
        2.0 * depth * (1.0 - depth), depth * depth);
    vec2 sunDirection = normalize(vec2(-0.55, -0.45));
    vec3 air = vec3(0.0);
    for (int layer = 0; layer < 3; ++layer) {
        float layerDepth = float(layer) * 0.5;
        float distanceToAir = (heights[layer] - camera.z) / min(ray.z, -0.001);
        vec2 world = camera.xy + ray.xy * distanceToAir;
        float broadHaze = atmosphereNoise(world * 0.003
            + vec2(12.8, -6.3) + vec2(7.1, 13.4) * layerDepth);
        float warmth = smoothstep(-1.2, 1.2,
            dot(ray.xy, sunDirection) * 0.65 + (broadHaze - 0.5) * 1.1);
        vec3 shadow = mix(vec3(0.18, 0.22, 0.40),
            vec3(0.09, 0.13, 0.28), layerDepth);
        vec3 sunlit = mix(vec3(0.55, 0.36, 0.34),
            vec3(0.30, 0.22, 0.27), layerDepth);
        // Peach light falls away with depth, leaving the cool palette below.
        air += mix(shadow, sunlit, warmth * (1.0 - layerDepth * 0.25))
            * weights[layer];
    }
    return mix(sky, air, visibility * 0.94);
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - pc.viewport.xy) / max(pc.viewport.zw, vec2(1.0));
    vec4 farPoint = pc.worldFromClip * vec4(uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0, 0.999, 1.0);
    vec3 camera = frame.cameraPositionAndNearPlane.xyz;
    vec3 ray = normalize(farPoint.xyz / farPoint.w - camera);
    float horizon = pow(1.0 - abs(ray.z), 3.0);
    vec3 sky = mix(vec3(0.25, 0.29, 0.53), vec3(0.68, 0.52, 0.61), horizon);
    sky = lowerAtmosphere(sky, camera, ray);
    // A muted distant bank, darker blue-violet middle clouds, and peach-lit
    // nearer wisps. Broken coverage leaves the deep atmosphere visible.
    // Fixed world heights give genuine parallax during orbit,
    // top-down view and overworld zoom, without attaching clouds to the camera.
    sky = compositeCloudLayer(sky, camera, ray, -90.0, 0.040,
        vec2(0.004, -0.002), vec2(38.1, 7.4), 0.35, 1.8, 0.55,
        vec3(0.27, 0.28, 0.49), vec3(0.66, 0.56, 0.61));
    sky = compositeCloudLayer(sky, camera, ray, -52.0, 0.065,
        vec2(0.007, 0.003), vec2(-19.3, 31.8), 0.42, 2.2, 0.18,
        vec3(0.18, 0.20, 0.42), vec3(0.54, 0.44, 0.53));
    sky = compositeCloudLayer(sky, camera, ray, -26.0, 0.090,
        vec2(0.011, -0.004), vec2(4.7, -23.6), 0.50, 2.0, 0.05,
        vec3(0.34, 0.32, 0.55), vec3(0.95, 0.71, 0.58));
    // Scene alpha is the ambient-occlusion mask, never background opacity.
    outColor = vec4(sky, 0.0);
}
