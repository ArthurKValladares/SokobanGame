#version 460

// The resolved scene snapshot is sampled rather than the live scene target,
// keeping this pass free of attachment/read feedback. It is linearly sampled
// into a half-resolution target while a nine-tap horizontal Gaussian spreads
// only HDR highlights.
layout(set = 0, binding = 1) uniform sampler2D sceneColor;

layout(location = 0) out vec4 outColor;

// params: x = threshold, y = soft-knee width.
// targetExtent: xy = half-resolution bloom target extent.
layout(push_constant) uniform PushConstants
{
    layout(offset = 128) vec4 params;
    layout(offset = 144) vec4 targetExtent;
} pc;

vec3 extractHighlight(vec3 color)
{
    color = max(color, vec3(0.0));
    float brightness = max(color.r, max(color.g, color.b));
    float knee = max(pc.params.y, 0.0001);
    float soft = clamp(brightness - pc.params.x + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float contribution = max(brightness - pc.params.x, soft) /
        max(brightness, 0.0001);
    return color * contribution;
}

void main()
{
    ivec2 sourceExtent = textureSize(sceneColor, 0);
    vec2 outputExtent = max(pc.targetExtent.xy, vec2(1.0));
    vec2 uv = gl_FragCoord.xy / outputExtent;
    vec2 stepUv = vec2(1.0 / float(sourceExtent.x), 0.0);

    vec3 result = extractHighlight(texture(sceneColor, uv).rgb) * 0.227027;
    result += extractHighlight(texture(sceneColor, uv + stepUv * 1.384615).rgb) * 0.316216;
    result += extractHighlight(texture(sceneColor, uv - stepUv * 1.384615).rgb) * 0.316216;
    result += extractHighlight(texture(sceneColor, uv + stepUv * 3.230769).rgb) * 0.070270;
    result += extractHighlight(texture(sceneColor, uv - stepUv * 3.230769).rgb) * 0.070270;
    outColor = vec4(result, 1.0);
}
