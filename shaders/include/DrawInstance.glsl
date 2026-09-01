#ifndef SOKOBAN_DRAW_INSTANCE_GLSL
#define SOKOBAN_DRAW_INSTANCE_GLSL

// One draw's parameters, read back by instance index. T1 moved these out of
// push constants so that consecutive draws sharing a pipeline can collapse
// into a single instanced draw.
//
// Mirrors GpuDrawInstance in VulkanRenderConstants.hpp, whose static_assert
// pins the 256-byte size and whose field comments are the authority on what
// each lane means. Several lanes are a union claimed by one pass at a time;
// read that header before assuming a lane means the same thing in two passes.
//
// How a shader reaches its own entry differs by pipeline, so the `draw` macro
// stays with each shader: fragment stages index by inDrawInstance, most vertex
// stages by gl_InstanceIndex, and the skinned vertex stage by a push constant
// because gl_InstanceIndex is already spoken for by the skinning palette.
struct DrawInstance
{
    vec4 vertices[4];
    vec4 passData[4];
    vec4 color;
    vec4 normalAndAmbientRed;
    vec4 sunDirectionAndAmbientGreen;
    vec4 sunRadianceAndAmbientBlue;
    vec4 shadowOptions;
    vec4 materialOptions;
    vec4 gridColor;
    vec4 textureOptions;
};
layout(std430, set = 0, binding = 10) readonly buffer DrawInstances
{
    DrawInstance instances[];
} drawInstances;

#endif
