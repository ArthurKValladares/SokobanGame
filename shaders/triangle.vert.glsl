#version 460

layout(location = 0) out vec4 outShadowPosition;

layout(push_constant) uniform PushConstants
{
    vec4 vertices[4];
    vec4 shadowVertices[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    const int index = indices[gl_VertexIndex];
    gl_Position = pc.vertices[index];
    outShadowPosition = pc.shadowVertices[index];
}
