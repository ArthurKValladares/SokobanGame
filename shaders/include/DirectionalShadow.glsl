#ifndef SOKOBAN_DIRECTIONAL_SHADOW_GLSL
#define SOKOBAN_DIRECTIONAL_SHADOW_GLSL

// Directional-shadow filtering shared by the ground and model shaders.
//
// The sampler is nearest-filtered, so textureGather returns the four actual
// depth texels surrounding the projected coordinate. Pairing that footprint
// with the center sample keeps five equal PCF comparisons while issuing two
// texture operations. The former 3x3 loop issued nine independent samples on
// every sun-lit fragment; at evidence resolution the ground alone covers more
// than half the frame.
float shadowFactor(vec4 shadowPosition, float diffuse)
{
    if (draw.shadowOptions.x <= 0.5 || diffuse <= 0.0 ||
        abs(shadowPosition.w) <= 0.0001) {
        return 1.0;
    }

    vec3 shadowCoord = shadowPosition.xyz / shadowPosition.w;
    vec2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (any(lessThan(shadowUv, vec2(0.0))) ||
        any(greaterThan(shadowUv, vec2(1.0))) ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }

    float biasedDepth = shadowCoord.z - draw.shadowOptions.z;
    float centerDepth = texture(shadowMap, shadowUv).r;
    vec4 gatheredDepth = textureGather(shadowMap, shadowUv);
    float shadowedSamples = biasedDepth > centerDepth ? 1.0 : 0.0;
    shadowedSamples += dot(
        mix(
            vec4(0.0),
            vec4(1.0),
            greaterThan(vec4(biasedDepth), gatheredDepth)),
        vec4(1.0));
    return 1.0 - shadowedSamples * 0.2 * draw.shadowOptions.y;
}

#endif
