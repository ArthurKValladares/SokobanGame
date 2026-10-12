#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 1) in float inFaceCoordU;
layout(location = 2) in float inFaceCoordV;
layout(location = 7) flat in uint inDrawInstance;
layout(location = 0) out vec4 outColor;

#include "DrawInstance.glsl"

#define draw drawInstances.instances[inDrawInstance]

float lightStroke(float distanceToCurve, float width, float pixelWidth)
{
    return 1.0 - smoothstep(
        max(width - pixelWidth, 0.0), width + pixelWidth,
        abs(distanceToCurve));
}

float randomCell(vec2 cell)
{
    return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

// Tiny, detached points, with no elongated streaks. The broad density mask
// places most of the flickering light near the middle of the aperture/plume.
float energyMotes(vec2 uv, float time, float phase, vec2 grid, float density)
{
    vec2 movingUv = uv + vec2(sin(time * 0.19 + phase) * 0.006, -time * 0.018);
    vec2 cell = floor(movingUv * grid);
    float seed = randomCell(cell + vec2(phase));
    vec2 center = vec2(0.20, 0.20) + vec2(
        randomCell(cell + vec2(2.17, 4.13)),
        randomCell(cell + vec2(5.31, 1.73))) * 0.60;
    vec2 delta = fract(movingUv * grid) - center;
    float radius = length(delta);
    float pixelWidth = max(fwidth(radius), 0.014);
    float point = lightStroke(radius, 0.024, pixelWidth * 0.65);
    float glow = exp(-radius * 19.0) * 0.22;
    float twinkle = 0.25 + 0.75 * pow(
        0.5 + 0.5 * sin(time * (1.9 + seed * 2.7) + seed * 31.0), 2.0);
    return (point + glow) * twinkle * step(seed, density);
}

vec4 portalRings(vec2 uv, vec3 tint, float time, float phase)
{
    vec2 p = uv * 2.0 - 1.0;
    float radius = length(p);
    float angle = atan(p.y, p.x);
    // Distort whole closed curves rather than scrolling a texture across the
    // disk. Different angular speeds make the fine loops orbit one another.
    float firstRadius = 0.825 +
        0.012 * sin(angle * 3.0 - time * 1.15 + phase) +
        0.008 * sin(angle * 5.0 + time * 0.73 + phase * 1.7);
    vec2 orbit = vec2(cos(time * 0.63 + phase),
                      sin(time * 0.63 + phase)) * 0.012;
    float secondAngle = atan(p.y - orbit.y, p.x - orbit.x);
    float secondRadius = 0.852 +
        0.013 * sin(secondAngle * 2.0 + time * 0.92 + phase * 0.8);
    float thirdRadius = 0.880 +
        0.010 * sin(angle * 4.0 - time * 0.77 + phase * 1.3);
    float fourthRadius = 0.905 +
        0.012 * sin(angle * 3.0 + time * 0.58 + phase * 1.9);
    float d0 = radius - firstRadius;
    float d1 = length(p - orbit) - secondRadius;
    float d2 = radius - thirdRadius;
    float d3 = radius - fourthRadius;
    float pixelWidth = max(fwidth(radius) * 0.70, 0.001);
    float core0 = lightStroke(d0, 0.006, pixelWidth);
    float core1 = lightStroke(d1, 0.005, pixelWidth);
    float core2 = lightStroke(d2, 0.005, pixelWidth);
    float core3 = lightStroke(d3, 0.004, pixelWidth);
    float core = max(max(core0, core1 * 0.88), max(core2 * 0.92, core3 * 0.76));
    float nearestRing = min(min(abs(d0), abs(d1)), min(abs(d2), abs(d3)));
    float halo = exp(-nearestRing * 65.0);
    float outerHalo = exp(-nearestRing * 27.0);
    float travellingLight = pow(
        0.5 + 0.5 * cos(angle - time * 1.8 + phase), 12.0);
    float pulse = 0.92 + 0.08 * sin(time * 2.1 + phase);
    float interior = 1.0 - smoothstep(0.79, 0.85, radius);
    float centerDensity = pow(max(1.0 - radius * radius, 0.0), 1.7);
    float motes = energyMotes(uv, time, phase, vec2(13.0, 19.0),
        centerDensity * 0.45) * interior;
    // A cool, almost-black opening establishes a doorway even against a
    // bright board. The illuminated rim supplies the bloom, not the center.
    float lightCoverage = clamp(
        core * 0.91 + halo * 0.12 + outerHalo * 0.025 + motes * 0.3,
        0.0, 0.96);
    float alpha = clamp(draw.color.a *
        max(interior, lightCoverage), 0.0, 1.0);
    vec3 hotTint = mix(tint, vec3(1.0), 0.78);
    vec3 lightColor = tint * (2.7 + pulse * 0.6) +
        hotTint * core * (6.5 + travellingLight * 3.0);
    float rimMix = clamp(halo * 0.9 + core + motes * 0.1, 0.0, 1.0);
    vec3 darkCenter = vec3(0.0035, 0.0020, 0.0085) + tint * 0.0015;
    vec3 color = mix(darkCenter, lightColor, rimMix) +
        mix(tint, vec3(0.78, 0.82, 1.0), 0.45) * motes * 2.0;
    return vec4(color, alpha);
}

vec4 portalBeam(vec2 uv, vec3 tint, float time, float phase)
{
    float x = uv.x * 2.0 - 1.0;
    float distanceAlongBeam = clamp(uv.y, 0.0, 1.0);
    float softEdge = pow(max(1.0 - x * x, 0.0), 2.0);
    float farFade = 1.0 - smoothstep(0.20, 1.0, distanceAlongBeam);
    // Every axial sheet terminates on a diameter of the doorway. Fade that
    // entire entrance away: nonzero coverage on the cut edge makes a hard
    // pinwheel across the dark opening when the sheets overlap in projection.
    float entranceFade = smoothstep(0.0, 0.30, distanceAlongBeam);
    float envelope = softEdge * entranceFade * farFade *
        exp(-distanceAlongBeam * 1.6);
    // Broad, slowly moving interference adds life without discrete shafts
    // or trails. The beam is a softly diffused glow leaving the doorway.
    float haze = 0.84 +
        0.10 * sin(x * 4.0 + distanceAlongBeam * 3.0 - time * 0.65 + phase) +
        0.06 * sin(x * 7.0 - distanceAlongBeam * 5.0 + time * 0.41 + phase);
    float motes = energyMotes(uv, time * 1.3, phase + 3.7, vec2(9.0, 17.0),
        softEdge * farFade * 0.22) * envelope;
    float alpha = clamp(draw.color.a *
        (envelope * haze * 0.20 + motes * 0.12), 0.0, 0.20);
    vec3 color = tint * (3.6 + haze * 0.5) +
        mix(tint, vec3(0.82, 0.86, 1.0), 0.4) * motes * 1.2;
    return vec4(color, alpha);
}

void main()
{
    vec2 uv = vec2(inFaceCoordU, inFaceCoordV);
    // Use the same scene clock as the other energy materials. The face's
    // origin supplies a stable phase for neighbouring rings and portals.
    float time = draw.materialOptions.w;
    float phase = dot(draw.vertices[0].xyz, vec3(1.71, 2.37, 17.3));
    vec3 tint = max(draw.color.rgb, vec3(0.0));
    tint /= max(max(tint.r, tint.g), max(tint.b, 0.001));
    outColor = draw.passData[0].x > 0.5
        ? portalBeam(uv, tint, time, phase)
        : portalRings(uv, tint, time, phase);
    if (outColor.a < 0.0015) {
        discard;
    }
}
