#version 460

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants
{
    vec4 vertices[6];
    vec4 color;
} pc;

void main()
{
    gl_Position = pc.vertices[gl_VertexIndex];
    outColor = pc.color.rgb;
}
