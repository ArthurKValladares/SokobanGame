#ifndef SOKOBAN_DRAW_MODE_GLSL
#define SOKOBAN_DRAW_MODE_GLSL

// Which shading path a draw takes, carried in GpuDrawInstance::textureOptions.x
// and read by the scene, mirror and UI fragment shaders.
//
// Mirrors DrawMaterialMode in VulkanRenderConstants.hpp. The two are pinned
// against each other by DrawModeTests, which parses the values out of this file
// and compares them to the enum - so the numbering cannot drift the way it did
// while these were bare literals on both sides.
//
// Values 0 to 2 are also ModelMaterialMode in AssetManifest.hpp, because a
// model draw passes its authored material mode straight through. Static
// assertions in VulkanSceneRecorder.cpp hold those three together.
//
// The mode is transported as a float and recovered with int(x + 0.5): the lane
// is part of a vec4 that carries floats for every other draw kind.
const int DRAW_MODE_UNTEXTURED = 0;
const int DRAW_MODE_MANIFEST_TEXTURE = 1;
const int DRAW_MODE_GLTF_MATERIAL = 2;
const int DRAW_MODE_FONT_GLYPH = 3;
const int DRAW_MODE_TITLE_BACKGROUND = 4;
const int DRAW_MODE_PROCEDURAL_TEXTURE = 5;
const int DRAW_MODE_SCENE_IMAGE = 6;
const int DRAW_MODE_TEXTURE_IMAGE = 7;

// Only the pipelines that branch on a mode read textureOptions.x this way.
// ground_splat reads it as a base texture handle and water reads it as a ripple
// frequency, because a pipeline can only ever see its own draws.
#endif
