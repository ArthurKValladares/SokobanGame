#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sokoban {

// Scene descriptor set bindings that hold a single image each: the shadow map,
// the sampled scene color copy, the post-processing inputs, and the scene
// target itself, which the tonemap pass reads. The model texture array is the
// remaining binding. Kept here so the descriptor pool sizing and the device
// sampled-image limit check agree on the count.
inline constexpr uint32_t sceneSingleImageBindings = 8;

struct PointLightUniform {
    Vec4 positionAndRange {};
    Vec4 colorAndIntensity {};
    Vec4 shadowOptions {};
};

// Everything the GPU needs to know about the frame that is not per draw: the
// camera it renders through, the sun's shadow transform, and the point lights.
// One uniform buffer, written once per frame per descriptor set.
//
// The camera lives here because before C1 there was no camera on the GPU at
// all. The CPU projected every vertex and pushed clip-space corners, so the
// only spatial frame a shader could reach was the sun's shadow frustum, which
// it inverted to guess at a world position. Vertices now arrive in world
// space and every stage can simply ask where it is.
struct SceneFrameUniform {
    Mat4 clipFromWorld {};
    // Orthographic, and the w it produces is always 1. The z must still be
    // clamped to [0, 1] by whoever uses it: projectShadowPoint clamped on the
    // CPU, and a matrix has no way to. Without the clamp, geometry outside
    // the sun's depth range samples past the edge of the shadow map instead
    // of at it.
    Mat4 shadowFromWorld {};
    // xyz is the camera in world space. Specular and any view-dependent term
    // derives its view direction from this and the fragment's world position,
    // rather than from the isometric constant that used to be compiled in.
    Vec4 cameraPositionAndNearPlane {};
    std::array<PointLightUniform, RenderFrameData::pointLightCapacity>
        pointLights {};
    Vec4 pointLightMeta {};
};

static_assert(sizeof(PointLightUniform) == 48);
static_assert(sizeof(SceneFrameUniform) == 544);

// The camera and sun transforms a frame renders through, in the form the
// uniform buffer wants them. Built by the recorder from the prepared scene's
// layouts so that VulkanSceneDescriptors never has to know what an isometric
// layout is.
struct SceneCamera {
    Mat4 clipFromWorld {};
    Mat4 shadowFromWorld {};
    Vec3 position {};
    float nearPlane = 0.0f;
};

// What the w of a corner in GpuDrawInstance::vertices means.
//
// Most quads are scene geometry in world space and the vertex shader applies
// the camera to them. The UI, the top-down 2D board and the grid overlay are
// authored directly in clip space - they have no world position and must not
// be projected - so they say so here. Getting this wrong is not subtle: a
// clip-space quad run through the camera lands somewhere off screen, and the
// first symptom is a window with nothing in it.
inline constexpr float worldSpaceQuad = 1.0f;
inline constexpr float clipSpaceQuad = 0.0f;

// Everything one draw needs that is not shared by the frame.
//
// T1 moved this out of push constants and into a storage buffer: a scene draw
// writes one entry and reads it back by instance index, which is what lets
// consecutive draws sharing a pipeline collapse into a single instanced draw.
// It was one vkCmdPushConstants(256) + vkCmdDraw(6) per quad before, and a
// board of any size is on the order of a thousand quads.
//
// The shadow pipelines still receive this same block as *push constants*.
// They are not instanced, they have one camera per pass, and nothing in a
// shadow pass reads material state - so the transport differs while the
// layout stays identical, and there is only one struct to keep in step.
struct GpuDrawInstance {
    // Four **world-space** corners for a quad, or the four columns of
    // worldFromModel for a mesh. Before C1 these were clip-space corners and
    // a baked clipFromModel, which is why the vertex shaders had nothing to
    // transform and nothing to report a world position from.
    //
    // The shadow pass is the exception: it has one camera per sun and six
    // more per point light, so its pipelines keep receiving clip-space
    // corners here. Nothing samples a world position in a shadow pass.
    std::array<Vec4, 4> vertices;
    // Sixty-four bytes of per-draw space, claimed by one pass at a time.
    //
    // Water uses all four for its border and ripple parameters. Model draws
    // use passData[0].x for the base index of their material range and y for
    // mixed-material back-face rejection. The full-screen SSAO pass uses this
    // block for clipFromView while `vertices` carries its inverse; `color`
    // carries its physical sampling controls and `normalAndAmbientRed` carries
    // the half-resolution extent plus bilateral thresholds.
    //
    // These used to be a shadow-space copy of the corners above, pushed on
    // every scene draw - the frame's sun transform, restated once per face.
    // SceneFrameUniform::shadowFromWorld covers all of them at once now, so
    // the block is no longer full. Water is the one pass that claims the
    // slot, for its border and ripple parameters; it had already been
    // squatting here, which is what the handoff meant about passes reusing
    // slots another path "leaves free".
    std::array<Vec4, 4> passData;
    Vec4 color;
    Vec4 normalAndAmbientRed;
    Vec4 sunDirectionAndAmbientGreen;
    Vec4 sunRadianceAndAmbientBlue;
    Vec4 shadowOptions;
    Vec4 materialOptions;
    Vec4 gridColor;
    Vec4 textureOptions;
};

static_assert(sizeof(GpuDrawInstance) == 256);

// Which entry of the draw-instance buffer a draw should read. Only the
// skinned-model pipeline needs it: gl_InstanceIndex is already spoken for
// there, indexing the skinning palette. Everything else gets its entry from
// gl_InstanceIndex via firstInstance, which is also what makes batching work.
// How many draws one frame may record. Quads dominate: a tile emits up to
// five faces, and particles, the grid overlay and the UI all take an entry
// too. Four per tile of capacity is roughly thirty times what a real board
// produces, and the buffer is host-visible, so this is the knob to turn if
// "Draw instance buffer is exhausted" ever appears.
inline constexpr uint32_t maxDrawInstancesPerFrame =
    RenderFrameData::tileCapacity * 4;

// Four explicit 32-bit integer lanes with the size and alignment of a GLSL
// uvec4 under std430. Keeping this separate from Math.hpp avoids making an
// integer GPU transport type look like general-purpose engine math.
struct alignas(16) GpuMaterialUint4 {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    uint32_t w = 0;
};

static_assert(sizeof(GpuMaterialUint4) == 16);
static_assert(alignof(GpuMaterialUint4) == 16);

// One glTF material, in the exact std430 form the shaders read. Every member
// is one aligned 16-byte lane; the offset assertions below are the CPU side of
// the ABI contract mirrored by Material in triangle.frag.glsl and
// mirror_energy.frag.glsl.
struct alignas(16) GpuMaterial {
    // rgb is the base colour factor, a multiplied over whatever the base
    // colour texture supplies; a is the material's opacity.
    Vec4 baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
    // rgb emissive factor, w metallic.
    Vec4 emissiveAndMetallic {};
    // x roughness, y normal scale, z occlusion strength, w alpha cutoff.
    Vec4 materialScalars { 1.0f, 1.0f, 1.0f, 0.5f };
    // One-based handles: x base colour, y normal, z metallic-roughness,
    // w emissive. Zero means that map is absent.
    GpuMaterialUint4 primaryTextureHandles {};
    // x is the one-based occlusion handle; yzw are reserved and remain zero.
    GpuMaterialUint4 occlusionTextureAndPadding {};
    // UV selections: x base colour, y normal, z metallic-roughness, w emissive.
    GpuMaterialUint4 textureUvSets {};
    // x occlusion UV, y alpha mode (0 opaque, 1 mask, 2 blend),
    // z PrimitiveMaterialFlag bits, w double-sided (0 false, 1 true).
    GpuMaterialUint4 materialState {};
};

static_assert(std::is_standard_layout_v<GpuMaterial>);
static_assert(alignof(GpuMaterial) == 16);
static_assert(offsetof(GpuMaterial, baseColorFactor) == 0);
static_assert(offsetof(GpuMaterial, emissiveAndMetallic) == 16);
static_assert(offsetof(GpuMaterial, materialScalars) == 32);
static_assert(offsetof(GpuMaterial, primaryTextureHandles) == 48);
static_assert(offsetof(GpuMaterial, occlusionTextureAndPadding) == 64);
static_assert(offsetof(GpuMaterial, textureUvSets) == 80);
static_assert(offsetof(GpuMaterial, materialState) == 96);
static_assert(sizeof(GpuMaterial) == 112);

struct MeshMaterial;

// Pure CPU-to-GPU conversion, kept public so the material ABI can be tested
// without constructing Vulkan resources.
[[nodiscard]] GpuMaterial gpuMaterialFrom(const MeshMaterial& material);

// How many materials may be resident at once. A model claims a contiguous
// range when it publishes and gives it back when residency evicts it, which
// is why the ranges are repacked rather than merely marked free: a base index
// is read off the model slot at record time and never baked into anything
// that outlives a frame, so moving one costs nothing. Four per model of the
// manifest cap is far more than any real model uses - the whole current
// manifest needs forty-one - and if "Material buffer is exhausted" ever
// appears, this is the knob.
inline constexpr uint32_t maxModelMaterials = 1024;

struct DrawInstanceIndexPushConstants {
    uint32_t drawInstance = 0;
    uint32_t padding[3] {};
};

} // namespace sokoban
