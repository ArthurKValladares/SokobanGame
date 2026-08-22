#version 460

layout(set = 0, binding = 1) uniform sampler2D sceneColorTexture;
layout(location = 0) out vec4 outColor;

// params.x is the eased transition amount: 0 = untouched, 1 = fully closed.
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
} pc;

vec3 sceneSample(vec2 uv)
{
    return texture(sceneColorTexture, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
}

void main()
{
    vec2 resolution = vec2(textureSize(sceneColorTexture, 0));
    float amount = clamp(pc.params.x, 0.0, 1.0);

    // Grow screen pixels into chunky world pixels. Snapping before the blur
    // keeps the samples visibly blocky instead of turning into a conventional
    // soft-focus filter.
    float maximumBlock = max(8.0, min(resolution.x, resolution.y) * 0.065);
    float blockSize = mix(1.0, maximumBlock, pow(amount, 1.35));
    vec2 snappedPixel =
        (floor(gl_FragCoord.xy / blockSize) + vec2(0.5)) * blockSize;
    vec2 uv = snappedPixel / resolution;

    // A compact nine-tap blur spreads recognizable shapes into broad colour
    // fields as the pixel grid grows. The final dark cover makes the midpoint
    // safe for replacing the rendered world.
    float radiusPixels = amount * min(resolution.x, resolution.y) * 0.085;
    vec2 radius = vec2(radiusPixels) / resolution;
    vec3 color = sceneSample(uv) * 0.24;
    color += sceneSample(uv + vec2( radius.x, 0.0)) * 0.11;
    color += sceneSample(uv + vec2(-radius.x, 0.0)) * 0.11;
    color += sceneSample(uv + vec2(0.0,  radius.y)) * 0.11;
    color += sceneSample(uv + vec2(0.0, -radius.y)) * 0.11;
    color += sceneSample(uv + vec2( radius.x,  radius.y)) * 0.08;
    color += sceneSample(uv + vec2(-radius.x,  radius.y)) * 0.08;
    color += sceneSample(uv + vec2( radius.x, -radius.y)) * 0.08;
    color += sceneSample(uv + vec2(-radius.x, -radius.y)) * 0.08;

    const vec3 worldClearColor = vec3(0.03, 0.04, 0.06);
    float cover = smoothstep(0.70, 1.0, amount);
    outColor = vec4(mix(color, worldClearColor, cover), 1.0);
}
