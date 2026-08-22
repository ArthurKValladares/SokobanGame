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

    // Collapse the view to roughly six square cells across its short edge.
    // The sub-linear curve makes the blocks readable early instead of saving
    // all of the pixel growth for the last few frames.
    float maximumBlock = max(16.0, min(resolution.x, resolution.y) * 0.16);
    float growth = pow(amount, 0.68);
    float blockSize = floor(mix(1.0, maximumBlock, growth) + 0.5);
    vec2 snappedPixel =
        (floor(gl_FragCoord.xy / blockSize) + vec2(0.5)) * blockSize;
    vec2 uv = snappedPixel / resolution;

    // Blur around each snapped source point, then repeat that result across
    // the entire output cell. The source blur therefore cannot soften the
    // hard cell boundaries.
    float radiusPixels = blockSize * growth * 1.15;
    vec2 radius = vec2(radiusPixels) / resolution;
    vec3 color = sceneSample(uv) * 0.36;
    color += sceneSample(uv + vec2( radius.x, 0.0)) * 0.10;
    color += sceneSample(uv + vec2(-radius.x, 0.0)) * 0.10;
    color += sceneSample(uv + vec2(0.0,  radius.y)) * 0.10;
    color += sceneSample(uv + vec2(0.0, -radius.y)) * 0.10;
    color += sceneSample(uv + vec2( radius.x,  radius.y)) * 0.06;
    color += sceneSample(uv + vec2(-radius.x,  radius.y)) * 0.06;
    color += sceneSample(uv + vec2( radius.x, -radius.y)) * 0.06;
    color += sceneSample(uv + vec2(-radius.x, -radius.y)) * 0.06;

    // A narrow one-to-two-pixel seam preserves the square silhouette when
    // neighbouring cells happen to contain similar colours.
    vec2 withinCell = fract(gl_FragCoord.xy / blockSize);
    float edgeDistance = min(
        min(withinCell.x, 1.0 - withinCell.x),
        min(withinCell.y, 1.0 - withinCell.y));
    float seamWidth = min(0.09, 1.5 / blockSize);
    float cellInterior = smoothstep(0.0, seamWidth, edgeDistance);
    float seamVisibility = smoothstep(0.12, 0.42, amount);
    color *= mix(1.0, mix(0.82, 1.0, cellInterior), seamVisibility);

    const vec3 worldClearColor = vec3(0.03, 0.04, 0.06);
    // Keep the large mosaic visible almost to the midpoint, then close fast.
    float cover = smoothstep(0.86, 1.0, amount);
    outColor = vec4(mix(color, worldClearColor, cover), 1.0);
}
