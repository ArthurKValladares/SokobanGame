#ifndef SOKOBAN_SKINNING_GLSL
#define SOKOBAN_SKINNING_GLSL

// GPU skinning palette. Mirrors GpuSkinningInstance in GpuSkinning.hpp, whose
// static_assert pins the size against this layout.
//
// MAX_SKIN_JOINTS comes from the build (-DMAX_SKIN_JOINTS=128); each shader
// keeps its own #ifndef fallback so it still compiles when opened alone in an
// editor. The palette is deliberately twice that: a mesh may carry attachment
// nodes past its own joint count.
struct SkinningInstance
{
    mat4 palette[MAX_SKIN_JOINTS + 128];
    mat4 modelFromSource;
    mat4 normalFromSource;
};
layout(std430, set = 0, binding = 9) readonly buffer SkinningPalette
{
    SkinningInstance instances[];
} skinning;

#endif
