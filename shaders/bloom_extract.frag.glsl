#version 460

// The resolved scene snapshot is sampled rather than the live scene target,
// keeping this pass free of attachment/read feedback. It is linearly sampled
// into a half-resolution target while a nine-tap horizontal Gaussian spreads
// only HDR highlights. bloom_blur.frag.glsl then applies the same Gaussian
// vertically over that target.
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
    vec2 outputExtent = max(pc.targetExtent.xy, vec2(1.0));
    vec2 uv = gl_FragCoord.xy / outputExtent;
    // The taps sit one target texel apart, the unit the vertical pass uses, so
    // the glow is as wide as it is tall. Spacing them in source texels made
    // the horizontal blur half as wide on screen as the vertical one.
    //
    // These are the nine discrete weights that the vertical pass folds into
    // five linear taps. They stay discrete here because each one must land on
    // the centre of a target texel: there a linear fetch averages the source
    // texels under it, and neighbouring taps leave none out. Folded taps fall
    // between those centres and would skip source columns, which makes a thin
    // highlight flicker as it moves.
    const float weights[5] = float[5](
        0.227027, 0.194595, 0.121622, 0.054054, 0.016216);
    vec2 stepUv = vec2(1.0 / outputExtent.x, 0.0);

    vec3 result = extractHighlight(texture(sceneColor, uv).rgb) * weights[0];
    for (int tap = 1; tap < 5; ++tap) {
        vec2 offset = stepUv * float(tap);
        result += extractHighlight(texture(sceneColor, uv + offset).rgb) * weights[tap];
        result += extractHighlight(texture(sceneColor, uv - offset).rgb) * weights[tap];
    }
    outColor = vec4(result, 1.0);
}
