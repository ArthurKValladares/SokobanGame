#version 460

layout(set = 0, binding = 5) uniform sampler2D depthTexture;
layout(location = 0) out vec4 outAo;

// params: x = strength (applied at composite), y = radius in pixels,
// z = depth range for occlusion falloff.
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
} pc;

// Interleaved gradient noise: cheap per-pixel randomization without a noise
// texture; the composite blur averages it away.
float gradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(depthTexture, 0));
    vec2 uv = gl_FragCoord.xy * texel;
    float centerDepth = texture(depthTexture, uv).r;
    if (centerDepth >= 0.9999) {
        outAo = vec4(1.0); // background
        return;
    }

    const int pairCount = 10;
    const float goldenAngle = 2.39996323;
    const float bias = 0.0005;
    float radiusPixels = max(pc.params.y, 1.0);
    float depthRange = max(pc.params.z, 0.00001);

    // Random per-pixel rotation of the whole sample disk breaks the sampling
    // pattern into noise instead of banding.
    float rotation = gradientNoise(gl_FragCoord.xy) * 6.2831853;

    float occlusion = 0.0;
    for (int i = 0; i < pairCount; ++i) {
        float angle = rotation + float(i) * goldenAngle;
        float radius = radiusPixels * sqrt((float(i) + 0.5) / float(pairCount));
        vec2 offset = vec2(cos(angle), sin(angle)) * radius * texel;

        // Symmetric pairs cancel planar depth slope exactly: on any flat
        // (even tilted) surface the pair average equals the center depth, so
        // only genuine concavity (creases, corners, contacts) occludes.
        float depthA = texture(depthTexture, uv + offset).r;
        float depthB = texture(depthTexture, uv - offset).r;
        float concavity = centerDepth - 0.5 * (depthA + depthB);

        float contribution = smoothstep(bias, depthRange, concavity);
        // Large discontinuities are unrelated geometry, not creases; fade
        // them out to avoid halos.
        contribution *= 1.0 - smoothstep(depthRange * 2.0, depthRange * 6.0, concavity);
        occlusion += contribution;
    }

    float ao = clamp(1.0 - occlusion / float(pairCount) * 1.6, 0.0, 1.0);
    outAo = vec4(ao, ao, ao, 1.0);
}
