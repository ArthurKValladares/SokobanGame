#version 460

layout(push_constant) uniform PushConstants
{
    // Clip space already. A shadow pass has one camera per sun and six per
    // point light, so there is no single transform a uniform could hold and
    // these stay projected on the CPU.
    vec4 shadowVertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    gl_Position = pc.shadowVertices[indices[gl_VertexIndex]];
}
