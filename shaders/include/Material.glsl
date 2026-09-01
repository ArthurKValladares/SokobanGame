#ifndef SOKOBAN_MATERIAL_GLSL
#define SOKOBAN_MATERIAL_GLSL

// One glTF material. Mirrors GpuMaterial in VulkanRenderConstants.hpp; the
// static_assert there is what keeps the two from drifting in size, and the
// field comments there are the authority on what each lane means.
struct Material
{
    vec4 baseColorFactor;
    vec4 emissiveAndMetallic;
    vec4 materialScalars;
    uvec4 primaryTextureHandles;
    uvec4 occlusionTextureAndPadding;
    uvec4 textureUvSets;
    uvec4 materialState;
};
layout(std430, set = 0, binding = 12) readonly buffer Materials
{
    Material entries[];
} materials;

#endif
