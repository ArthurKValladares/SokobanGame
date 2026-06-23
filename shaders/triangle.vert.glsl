#version 460

layout(location = 0) out vec4 outShadowPosition;
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
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
    vec4 gridColor;
} pc;

const int indices[6] = int[6](0, 1, 2, 0, 2, 3);
const vec2 faceCoords[4] = vec2[4](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0));

void main()
{
    const int index = indices[gl_VertexIndex];
    gl_Position = pc.vertices[index];
    outShadowPosition = pc.shadowVertices[index];
    vec2 faceCoord = faceCoords[index] * pc.materialOptions.yz;
    outFaceCoordU = faceCoord.x;
    outFaceCoordV = faceCoord.y;
}
