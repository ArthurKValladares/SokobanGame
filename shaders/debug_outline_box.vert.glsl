#version 460
#extension GL_GOOGLE_include_directive : require
layout(location = 1) out float outFaceCoordU;
layout(location = 2) out float outFaceCoordV;
layout(location = 7) flat out uint outDrawInstance;
#include "SceneFrame.glsl"
#include "DrawInstance.glsl"
#define draw drawInstances.instances[gl_InstanceIndex]
const int indices[6] = int[6](0, 1, 2, 0, 2, 3);
const vec2 uv[4] = vec2[4](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1));
// Outward CCW winding, matching glTF before the negative-height viewport.
const vec3 corners[24] = vec3[24](
    vec3(0,0,1), vec3(1,0,1), vec3(1,1,1), vec3(0,1,1),
    vec3(0,1,0), vec3(1,1,0), vec3(1,0,0), vec3(0,0,0),
    vec3(0,0,0), vec3(1,0,0), vec3(1,0,1), vec3(0,0,1),
    vec3(1,1,0), vec3(0,1,0), vec3(0,1,1), vec3(1,1,1),
    vec3(0,1,0), vec3(0,0,0), vec3(0,0,1), vec3(0,1,1),
    vec3(1,0,0), vec3(1,1,0), vec3(1,1,1), vec3(1,0,1));
void main()
{
    int corner = indices[gl_VertexIndex % 6];
    vec3 position = corners[(gl_VertexIndex / 6) * 4 + corner];
    mat4 world = mat4(draw.vertices[0], draw.vertices[1],
                      draw.vertices[2], draw.vertices[3]);
    gl_Position = frame.clipFromWorld * world * vec4(position, 1.0);
    gl_Position.z -= 0.00001 * gl_Position.w;
    outFaceCoordU = uv[corner].x;
    outFaceCoordV = uv[corner].y;
    outDrawInstance = uint(gl_InstanceIndex);
}
