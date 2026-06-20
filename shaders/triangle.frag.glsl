#version 460

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    vec4 vertices[6];
    vec4 color;
} pc;

void main()
{
    outColor = vec4(inColor, pc.color.a);
}
