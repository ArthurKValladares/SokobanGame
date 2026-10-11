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

float hashCloud(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// Value and analytic XY gradient. Lighting uses the slope of the billows,
// avoiding extra noise evaluations or screen derivatives at cloud edges.
vec3 cloudNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float a = hashCloud(cell);
    float b = hashCloud(cell + vec2(1.0, 0.0));
    float c = hashCloud(cell + vec2(0.0, 1.0));
    float d = hashCloud(cell + vec2(1.0));
    float k = a - b - c + d;
    return vec3(a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y,
        du * (vec2(b - a, c - a) + k * u.yx));
}

vec3 cloudField(vec2 p, float time)
{
    // Independent octave motion gently changes the contours as wind advects
    // the entire bank. The broad octave dominates for soft, readable masses.
    vec3 field = vec3(0.0);
    float frequency = 1.0;
    float amplitude = 0.57;
    for (int octave = 0; octave < 4; ++octave) {
        vec3 n = cloudNoise(p * frequency +
            vec2(13.7, 9.2) * float(octave) +
            time * vec2(0.008, -0.005) * float(octave));
        field += vec3(n.x, n.yz * frequency) * amplitude;
        frequency *= 2.03;
        amplitude *= 0.48;
    }
    return field / 1.039;
}

vec3 compositeCloudLayer(vec3 below, vec3 camera, vec3 ray,
    float height, float scale, vec2 wind, vec2 seed,
    float coverage, float opacity, float haze)
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
    // Low-frequency warping breaks up the square value-noise lattice.
    vec2 warp = vec2(cloudNoise(p * 0.43 + 17.2).x,
        cloudNoise(p * 0.43 - 8.4).x) - 0.5;
    vec3 field = cloudField(p + warp * 0.65, time);
    float body = smoothstep(coverage, coverage + 0.25, field.x);
    float alpha = (1.0 - exp(-body * opacity)) * visibility;

    vec3 normal = normalize(vec3(-field.yz * 1.35, 1.0));
    vec3 sun = normalize(vec3(-0.55, -0.45, 0.70));
    float light = clamp(dot(normal, sun), 0.0, 1.0);
    vec3 shade = vec3(0.38, 0.48, 0.62);
    vec3 lit = vec3(0.86, 0.89, 0.91);
    vec3 color = mix(shade, lit, 0.32 + light * 0.68);
    // Thin wisps blend into blue air; the distant sea loses contrast.
    color = mix(color, vec3(0.57, 0.66, 0.77), haze);
    return mix(below, color, alpha);
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - pc.viewport.xy) / max(pc.viewport.zw, vec2(1.0));
    vec4 farPoint = pc.worldFromClip * vec4(uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0, 0.999, 1.0);
    vec3 camera = frame.cameraPositionAndNearPlane.xyz;
    vec3 ray = normalize(farPoint.xyz / farPoint.w - camera);
    float horizon = pow(1.0 - abs(ray.z), 3.0);
    vec3 sky = mix(vec3(0.27, 0.42, 0.61), vec3(0.63, 0.72, 0.82), horizon);
    // A hazy, almost continuous distant sea, a broken middle bank, and sparse
    // nearer wisps. Fixed world heights give genuine parallax during orbit,
    // top-down view and overworld zoom, without attaching clouds to the camera.
    sky = compositeCloudLayer(sky, camera, ray, -90.0, 0.040,
        vec2(0.004, -0.002), vec2(38.1, 7.4), 0.20, 3.8, 0.68);
    sky = compositeCloudLayer(sky, camera, ray, -52.0, 0.065,
        vec2(0.007, 0.003), vec2(-19.3, 31.8), 0.36, 3.0, 0.30);
    sky = compositeCloudLayer(sky, camera, ray, -26.0, 0.090,
        vec2(0.011, -0.004), vec2(4.7, -23.6), 0.46, 2.6, 0.08);
    // Scene alpha is the ambient-occlusion mask, never background opacity.
    outColor = vec4(sky, 0.0);
}
