#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;

#include "DrawInstance.glsl"

layout(push_constant) uniform PushConstants
{
    DrawInstance instance;
} pc;

void main()
{
    // Chunk vertices already include cap deformation and world placement.
    // The sun transform shares the model-shadow push layout, but no body rim
    // deformation is applied to this finished geometry.
    mat4 shadowFromWorld = mat4(pc.instance.vertices[0], pc.instance.vertices[1],
        pc.instance.vertices[2], pc.instance.vertices[3]);
    gl_Position = shadowFromWorld * vec4(inPosition, 1.0);
    gl_Position.z = clamp(gl_Position.z, 0.0, 1.0);
}
