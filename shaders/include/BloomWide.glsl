// The skirt of the bloom. bloom_blur.frag.glsl leaves the core in the bloom
// target; this spreads it with a 21-tap Gaussian, once across and once down,
// and the second of those lays the result over the core it started from.
//
// The taps stand params.x texels apart. That is sound only because the core
// is already a Gaussian 1.7 texels wide: up to about twice that spacing no
// texel between two taps goes unseen. bloomMix in SceneDrawLanes.hpp chooses
// the two strengths.
layout(push_constant) uniform PushConstants
{
    // x = spacing of the taps in texels. For the pass that lays the skirt
    // over the core: y = strength the skirt is written with, z = share of
    // the core that is kept, which the blend takes as the source alpha.
    layout(offset = 128) vec4 params;
} pc;

vec3 bloomWide(sampler2D source, vec2 direction)
{
    // exp(-i * i / (2 * 3.6 * 3.6)) for i = 0..10, scaled to sum to one over
    // all 21 taps.
    const float weights[11] = float[11](
        0.111199, 0.106991, 0.095297, 0.078579, 0.059982, 0.042386,
        0.027728, 0.016792, 0.009414, 0.004886, 0.002347);
    vec2 extent = vec2(textureSize(source, 0));
    vec2 uv = gl_FragCoord.xy / extent;
    vec2 stepUv = direction * max(pc.params.x, 1.0) / extent;

    vec3 result = textureLod(source, uv, 0.0).rgb * weights[0];
    for (int tap = 1; tap < 11; ++tap) {
        vec2 offset = stepUv * float(tap);
        result += (textureLod(source, uv + offset, 0.0).rgb +
            textureLod(source, uv - offset, 0.0).rgb) * weights[tap];
    }
    return result;
}
