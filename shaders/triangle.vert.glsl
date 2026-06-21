#version 460

layout(push_constant) uniform PushConstants
{
    vec4 vertices[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    gl_Position = pc.vertices[indices[gl_VertexIndex]];
}
