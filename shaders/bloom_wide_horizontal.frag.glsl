#version 460

// First half of the bloom's skirt: reads the core from the bloom target and
// spreads it across, into the extract target, which nothing needs any more.
layout(set = 0, binding = 15) uniform sampler2D bloomColor;

layout(location = 0) out vec4 outColor;

#include "BloomWide.glsl"

void main()
{
    outColor = vec4(bloomWide(bloomColor, vec2(1.0, 0.0)), 1.0);
}
