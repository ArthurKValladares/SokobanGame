#version 460

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstants
{
    vec2 vertices[6];
    vec4 color;
} pc;

void main()
{
    gl_Position = vec4(pc.vertices[gl_VertexIndex], 0.0, 1.0);
    outColor = pc.color.rgb;
}
