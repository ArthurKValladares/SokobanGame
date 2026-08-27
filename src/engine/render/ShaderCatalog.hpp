#pragma once

#include <array>
#include <string>
#include <string_view>

namespace sokoban::shaderCatalog {

// Every shader in the project, named exactly once.
//
// Four things need this list, and each used to keep its own copy: CMake
// compiles these sources to SPIR-V, ContentPipeline stages the compiled
// modules into the runtime asset tree, VulkanPipelineFactory loads them, and
// ContentPipelineTests fakes them. Adding a shader to some of those and not
// the others builds clean and validates clean, then fails at launch on the
// first frame that wants the missing module. That is not hypothetical: F2's
// tonemap shader was added to CMake alone and the game died on startup with
// "Failed to open file: ...assets/shaders/tonemap.frag.glsl.spv".
//
// CMakeLists.txt parses the quoted names below at configure time, and
// CMAKE_CONFIGURE_DEPENDS re-runs configure when this file changes. Two rules
// follow from that, and neither is optional:
//
//  * Every name is a plain string literal. A name assembled from pieces, or
//    reached through another constant, is invisible to the parser.
//  * No other quoted shader filename appears in this file, comments
//    included, or CMake will try to compile it. Name them without quotes
//    when you have to mention one.
//
// Adding a shader is: one constant, one entry in `sources`, one bump of its
// declared size. CMake fails the configure if the count and the array
// disagree, so a half-finished addition cannot reach a build.

inline constexpr std::string_view triangleVert = "triangle.vert.glsl";
inline constexpr std::string_view triangleFrag = "triangle.frag.glsl";
inline constexpr std::string_view waterFrag = "water.frag.glsl";
inline constexpr std::string_view mirrorEnergyFrag = "mirror_energy.frag.glsl";
inline constexpr std::string_view groundSplatFrag = "ground_splat.frag.glsl";
inline constexpr std::string_view shadowVert = "shadow.vert.glsl";
inline constexpr std::string_view modelVert = "model.vert.glsl";
inline constexpr std::string_view modelShadowVert = "model_shadow.vert.glsl";
inline constexpr std::string_view skinnedModelVert = "skinned_model.vert.glsl";
inline constexpr std::string_view skinnedModelShadowVert =
    "skinned_model_shadow.vert.glsl";
inline constexpr std::string_view fullscreenVert = "fullscreen.vert.glsl";
inline constexpr std::string_view ssaoFrag = "ssao.frag.glsl";
inline constexpr std::string_view ssaoCompositeFrag = "ssao_composite.frag.glsl";
inline constexpr std::string_view tonemapFrag = "tonemap.frag.glsl";
inline constexpr std::string_view worldTransitionFrag =
    "world_transition.frag.glsl";

inline constexpr std::array<std::string_view, 15> sources {
    triangleVert,
    triangleFrag,
    waterFrag,
    mirrorEnergyFrag,
    groundSplatFrag,
    shadowVert,
    modelVert,
    modelShadowVert,
    skinnedModelVert,
    skinnedModelShadowVert,
    fullscreenVert,
    ssaoFrag,
    ssaoCompositeFrag,
    tonemapFrag,
    worldTransitionFrag,
};

// The compiled module's filename. The build writes one SPIR-V blob per source
// under this name, and the runtime only ever reads the staged tree, so this
// is the name that has to match on disk.
[[nodiscard]] inline std::string compiledName(std::string_view source)
{
    return std::string(source) + ".spv";
}

} // namespace sokoban::shaderCatalog
