#version 460
#extension GL_GOOGLE_include_directive : require
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 7) flat out uint outDrawInstance;
#include "DrawInstance.glsl"
#define draw drawInstances.instances[gl_InstanceIndex]
const int indices[6] = int[6](0, 1, 2, 0, 2, 3);
const vec2 uv[4] = vec2[4](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1));
void main()
{
    int corner = indices[gl_VertexIndex];
    gl_Position = vec4(draw.vertices[corner].xyz, 1.0);
    vec2 pixels = uv[corner] * draw.materialOptions.yz;
    outFaceCoordU = pixels.x + draw.materialOptions.x;
    outFaceCoordV = pixels.y;
    outDrawInstance = uint(gl_InstanceIndex);
}
