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

uint hashCellBits(ivec2 cell)
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
    return h;
}

float hashCell(ivec2 cell)
{
    // Keep 24 bits so conversion to float is exact, including negative cells.
    return float(hashCellBits(cell) >> 8) * (1.0 / 16777216.0);
}

vec3 nightStars(vec3 ray)
{
    // Project world directions so stars stay fixed as the camera translates
    // and clouds drift. The projection has no seams across the horizon.
    vec2 p = ray.xy * (148.0 / max(1.0 - ray.z, 0.001));
    float pixelWidth = max(length(dFdx(p)), length(dFdy(p)));
    ivec2 cell = ivec2(floor(p));
    float chance = hashCell(cell + ivec2(341, -173));
    const float density = 0.20;
    if (chance >= density || pixelWidth >= 0.20) {
        return vec3(0.0);
    }

    vec2 offset = vec2(hashCell(cell + ivec2(-207, 127)),
        hashCell(cell + ivec2(113, 281)));
    vec2 center = mix(vec2(0.18), vec2(0.82), offset);
    float distanceToStar = length(fract(p) - center);
    float brilliance = pow(chance / density, 3.0);
    float radius = mix(0.027, 0.053, brilliance);
    float edge = clamp(pixelWidth * 0.55, 0.001, 0.07);
    float core = 1.0 - smoothstep(max(radius - edge, 0.0),
        radius + edge, distanceToStar);
    float halo = 1.0 - smoothstep(radius, 0.16, distanceToStar);
    float visibility = 1.0 - smoothstep(0.08, 0.20, pixelWidth);
    return vec3(0.66, 0.64, 0.95) * visibility *
        (mix(0.075, 0.49, brilliance) * core + 0.065 * brilliance * halo);
}

// Very broad value noise for the smooth atmosphere below the cloud banks.
float atmosphereNoise(vec2 p)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hashCell(cell);
    float b = hashCell(cell + ivec2(1, 0));
    float c = hashCell(cell + ivec2(0, 1));
    float d = hashCell(cell + ivec2(1));
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
    vec2 gradient = gradients[int(hashCell(cell) * 8.0)];
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

vec3 cloudPuffs(vec2 p, float presence, float minRadius, float maxRadius,
    float smallBias)
{
    // A smooth union of round domes, at most one per lattice cell, returned as
    // (height, gradient). Each dome is t^2 with t = 1 - distance^2 / radius^2.
    // The union is the 3-norm of the domes: overlapping domes keep their own
    // convex arcs and meet in a tight valley, as cumulus lobes do, where a
    // plain sum would fillet them into one blob. It stays C1, so neither the
    // outline nor the lighting pops while the field drifts.
    // One hash gives a cell its jitter, size and presence. A radius of at most
    // one cell keeps every dome inside the 3x3 neighborhood searched here.
    ivec2 cell = ivec2(floor(p));
    vec2 local = p - vec2(cell);
    float sum = 0.0;
    vec2 slope = vec2(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 neighbor = ivec2(x, y);
            uint bits = hashCellBits(cell + neighbor);
            if (float(bits >> 24) >= presence * 256.0) {
                continue;
            }
            vec2 jitter = vec2(float(bits & 255u), float((bits >> 8) & 255u))
                * (1.0 / 256.0);
            // smallBias moves the size distribution toward minRadius.
            float size = float((bits >> 16) & 255u) * (1.0 / 255.0);
            size *= mix(1.0, size, smallBias);
            float radius = mix(minRadius, maxRadius, size);
            float inverseRadius2 = 1.0 / (radius * radius);
            vec2 toCenter = vec2(neighbor) + jitter - local;
            float t = max(1.0 - dot(toCenter, toCenter) * inverseRadius2, 0.0);
            float t2 = t * t;
            float t5 = t2 * t2 * t;
            sum += t5 * t;
            slope += toCenter * (4.0 * t5 * inverseRadius2);
        }
    }
    float height = pow(sum, 1.0 / 3.0);
    return vec3(height, slope * (height / max(sum, 1.0e-9)));
}

vec3 cloudField(vec2 p, float time)
{
    // The surface texture of the puffs, not their outline. Independent octave
    // motion gently changes it as wind advects the entire bank.
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

    // The outline is cumulus: separate round clumps, present in under half of
    // the cells, whose edges are scalloped by two finer dome fields. Coverage
    // is the density threshold; lowering it grows and merges the clouds. The
    // detail fields drift slowly against the clumps so the outlines evolve.
    const float clumpFrequency = 0.50;
    const float lobeFrequency = 1.40;
    const float crumbFrequency = 3.40;
    const float lobeAmount = 0.42;
    const float crumbAmount = 0.11;
    const float textureAmount = 0.14;
    // A fine, shallow warp of the dome coordinates bends the arcs so the rims
    // are not perfect circles. Keep rimWarpAmount * rimWarpFrequency * 3 (the
    // noise's peak slope) below one, or the warp folds the outline into
    // spikes. The lighting ignores this warp's small effect on the relief.
    const float rimWarpFrequency = 1.8;
    const float rimWarpAmount = 0.14;
    vec2 rimWarp = vec2(
        cloudBillowNoise(p * rimWarpFrequency + vec2(5.3, -14.1)).x,
        cloudBillowNoise(p * rimWarpFrequency + vec2(-31.7, 8.9)).x) - 0.5;
    vec2 q = p + rimWarp * rimWarpAmount;
    vec3 clumps = cloudPuffs(q * clumpFrequency + vec2(3.7, -1.3),
        0.42, 0.5, 1.0, 0.5);
    // Fine noise, applied below, erodes and extends the rim. It fades in
    // with the clump height, so it adds nothing in open sky and the early
    // exits stay exact; this is the most it can add to the density here.
    const float erosionAmount = 0.32;
    float erosionReach = 0.5 * erosionAmount * smoothstep(0.0, 0.3, clumps.x);
    // Most of each layer is open sky. Each detail term below adds at most
    // half its amount, so density cannot reach coverage here and the result
    // is exactly the transparent one.
    const float detailReach = 0.5 * (lobeAmount + crumbAmount + textureAmount);
    if (clumps.x + detailReach + erosionReach <= coverage) {
        return below;
    }
    vec3 lobes = cloudPuffs(q * lobeFrequency + vec2(-11.9, 6.2)
        + time * vec2(0.006, -0.004), 1.0, 0.6, 0.95, 0.0);
    vec3 crumbs = cloudPuffs(q * crumbFrequency + vec2(21.4, 15.8)
        + time * vec2(-0.009, 0.007), 1.0, 0.6, 0.95, 0.0);
    // Clamp the heights so the bound above holds where domes pile up. The
    // unclamped gradients still feed the lighting, which keeps it continuous.
    float shape = clumps.x + lobeAmount * (min(lobes.x, 1.0) - 0.5)
        + crumbAmount * (min(crumbs.x, 1.0) - 0.5);
    if (shape + 0.5 * textureAmount + erosionReach <= coverage) {
        return below;
    }

    // The warped billow field supplies the brushed surface texture and frays
    // the rim slightly. Its strong warp must not shape the outline: it shears
    // whatever it carries into long streaks.
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
    // Two octaves of unwarped fine noise, centered on zero, break the rim
    // into ragged bites and wisps without moving the outline on average.
    // The finer octave gives way to its mean toward the horizon, where it
    // would shrink below a pixel and shimmer.
    const float erosionFrequency = 4.8;
    float coarseErosion = cloudBillowNoise(p * erosionFrequency
        + vec2(61.3, -47.9) + time * vec2(0.010, -0.006)).x;
    float fineErosion = cloudBillowNoise((mat2(0.8, 0.6, -0.6, 0.8) * p)
        * (erosionFrequency * 2.17)
        + vec2(-18.6, 73.2) + time * vec2(-0.016, 0.012)).x;
    float erosion = coarseErosion * 0.62
        + mix(0.5, fineErosion, smoothstep(0.2, 0.5, -ray.z)) * 0.38;
    float density = shape + textureAmount * (field.x - 0.5)
        + erosionReach * (1.0 - 2.0 * erosion);
    float body = smoothstep(coverage, coverage + 0.20, density);
    // Thickness varies across each cloud: broad smooth patches, partly
    // following the billow texture, thin the cloud to 0.40 of the layer's
    // opacity or thicken it to 1.15, so some parts show the sky behind them.
    float thicknessNoise = cloudBillowNoise(p * 0.8 + vec2(-27.3, 41.9)).x;
    float thickness = mix(0.40, 1.15, smoothstep(0.32, 0.68,
        mix(thicknessNoise, field.x, 0.35)));
    float alpha = (1.0 - exp(-body * opacity * thickness)) * visibility;

    // The domes round the lighting toward each rim; the finer fields count
    // for less so the billow texture stays the dominant surface detail.
    vec2 relief = clumps.yz * clumpFrequency
        + lobes.yz * (lobeFrequency * lobeAmount * 0.6)
        + crumbs.yz * (crumbFrequency * crumbAmount * 0.2);
    vec3 normal = normalize(vec3(-(relief * 0.8 + field.yz * 1.35), 1.0));
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
    sky = lowerAtmosphere(sky, camera, ray) * 0.43 + nightStars(ray);
    // A muted distant bank, darker blue-violet middle clouds, and peach-lit
    // nearer puffs. Broken coverage leaves the deep atmosphere visible.
    // Fixed world heights give genuine parallax during orbit,
    // top-down view and overworld zoom, without attaching clouds to the camera.
    sky = compositeCloudLayer(sky, camera, ray, -90.0, 0.052,
        vec2(0.004, -0.002), vec2(38.1, 7.4), 0.38, 1.0, 0.55,
        vec3(0.27, 0.28, 0.49), vec3(0.66, 0.56, 0.61));
    sky = compositeCloudLayer(sky, camera, ray, -52.0, 0.084,
        vec2(0.007, 0.003), vec2(-19.3, 31.8), 0.40, 1.55, 0.18,
        vec3(0.18, 0.20, 0.42), vec3(0.54, 0.44, 0.53));
    sky = compositeCloudLayer(sky, camera, ray, -26.0, 0.115,
        vec2(0.011, -0.004), vec2(4.7, -23.6), 0.42, 2.0, 0.05,
        vec3(0.34, 0.32, 0.55), vec3(0.95, 0.71, 0.58));
    // Scene alpha is the ambient-occlusion mask, never background opacity.
    outColor = vec4(sky, 0.0);
}
