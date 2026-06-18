#version 460

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    vec2 origin;
    vec2 size;
    vec4 color;
} pc;

void main()
{
    outColor = vec4(inColor, pc.color.a);
}
