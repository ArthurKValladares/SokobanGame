#version 460

// Second half of the bloom's skirt: spreads the extract target down and lays
// the result over the core in the bloom target. The pipeline blends it as
// source + target * source alpha, so the colour carries the skirt at its
// strength and the alpha says how much of the core stays.
layout(set = 0, binding = 14) uniform sampler2D bloomExtract;

layout(location = 0) out vec4 outColor;

#include "BloomWide.glsl"

void main()
{
    outColor = vec4(
        bloomWide(bloomExtract, vec2(0.0, 1.0)) * max(pc.params.y, 0.0),
        clamp(pc.params.z, 0.0, 1.0));
}
