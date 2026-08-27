#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct MeshVertex {
    Vec3 position {};
    Vec3 normal {};
    // xyz is the tangent; w is the bitangent's handedness, +1 or -1. That is
    // glTF's own layout, and it is what a normal map needs to be read in the
    // space it was baked in. The loader reads TANGENT when a file supplies
    // one and derives it from the UVs otherwise, so this is never zero on a
    // mesh with usable texture coordinates.
    Vec4 tangent {};
    Vec2 uv {};
    // The second UV set, for a material that lights and textures a surface
    // from different unwraps. Falls back to `uv` when a file has only one,
    // so a material asking for set 1 on a single-set mesh reads something
    // sensible rather than nothing.
    Vec2 uv1 {};
    // Which entry of the owning mesh's `materials` this vertex belongs to.
    // Per vertex rather than per draw because one draw covers a whole model
    // and a model's primitives can carry different materials. It replaced a
    // per-vertex texture index and flag word in F3b: those were the same
    // value for every vertex of a primitive, and the material entry they were
    // copied from now travels to the shader instead.
    uint32_t materialIndex = 0;
};

enum PrimitiveMaterialFlag : uint32_t {
    PrimitiveMaterialNone = 0,
    PrimitiveMaterialScrollV = 1U << 0U,
};

struct PrimitiveMaterialBinding {
    uint32_t textureIndex = 0;
    uint32_t flags = PrimitiveMaterialNone;
};

// glTF's alpha handling, which decides whether a primitive belongs in the
// opaque pass, is cut out against a threshold, or blends.
enum class MaterialAlphaMode : uint32_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

// One glTF material, as authored.
//
// Everything here except the last three fields comes straight out of the
// file. The texture does not: this engine's textures are declared by
// assets/manifest.json and resolved to descriptor indices there, so a glTF
// material's own image references are ignored and the manifest's slot for
// that material index supplies `baseColorTexture` instead. Whether the other
// map slots - normal, metallic-roughness, emissive, occlusion - should follow
// the same rule or be read from the glTF is an open question; see the F3b
// note in HANDOFF.md. They are deliberately absent rather than present and
// unfillable.
struct MeshMaterial {
    Vec4 baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
    Vec3 emissiveFactor {};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float alphaCutoff = 0.5f;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    bool doubleSided = false;
    // One-based descriptor index; zero means untextured.
    uint32_t baseColorTexture = 0;
    // Which UV set the base colour texture reads.
    uint32_t baseColorUvSet = 0;
    uint32_t flags = PrimitiveMaterialNone;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    // Indexed by MeshVertex::materialIndex. Never empty: a file with no
    // materials at all still gets one default entry, so nothing downstream
    // has to special-case the absence.
    std::vector<MeshMaterial> materials;
};

struct SkinnedVertex {
    Vec3 position {};
    Vec3 normal {};
    // Bind-pose tangent, skinned by the same palette as the normal.
    Vec4 tangent {};
    Vec2 uv {};
    Vec2 uv1 {};
    uint32_t materialIndex = 0;
    std::array<uint16_t, 4> joints {};
    std::array<float, 4> weights {};
};

struct SkeletonNode {
    std::string name;
    int parent = -1;
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

struct SkinnedAttachment {
    MeshData mesh;
    uint32_t nodeIndex = 0;
};

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshMaterial> materials;
    std::vector<SkeletonNode> nodes;
    std::vector<uint32_t> jointNodeIndices;
    std::vector<Mat4> inverseBindMatrices;
    std::vector<SkinnedAttachment> attachments;
    Vec3 sourceMinimum {};
    Vec3 sourceMaximum {};
    bool preserveAspectRatio = false;
    bool preserveSourceScale = false;
    bool rotateHalfTurn = false;
};

// The source vertex layout consumed by the GPU skinning pipelines. Attachment
// vertices use attachmentNodeIndex; regular skin vertices leave it at UINT32_MAX.
struct GpuSkinnedVertex {
    Vec3 position {};
    Vec3 normal {};
    Vec4 tangent {};
    Vec2 uv {};
    Vec2 uv1 {};
    uint32_t materialIndex = 0;
    std::array<uint16_t, 4> joints {};
    std::array<float, 4> weights {};
    uint32_t attachmentNodeIndex = UINT32_MAX;
};

struct SkinnedPoseMatrices {
    std::vector<Mat4> jointMatrices;
    std::vector<Mat4> nodeMatrices;
};

// How a channel reads between its keyframes. The loader ignored glTF's
// `interpolation` field entirely until A1 step two, so a STEP curve played as
// though it were LERP and nothing said so, and a CUBICSPLINE clip failed to
// load at all.
enum class AnimationInterpolation {
    Linear,
    Step,
    CubicSpline,
};

struct AnimationKeyframes {
    std::vector<float> times;
    // Linear and Step store one value per time. CubicSpline stores three, in
    // glTF's own order - in-tangent, value, out-tangent - so `values` is
    // three times as long as `times` and must not be indexed by key directly.
    std::vector<Vec4> values;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
};

enum class AnimationChannelPath {
    Translation,
    Rotation,
    Scale,
};

struct AnimationChannel {
    std::string targetNodeName;
    AnimationChannelPath path = AnimationChannelPath::Translation;
    AnimationKeyframes keyframes;
};

struct GltfAnimationClip {
    std::string name;
    float durationSeconds = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct GltfMeshLoadOptions {
    bool preserveAspectRatio = false;
    bool preserveSourceScale = false;
    bool rotateHalfTurn = false;
    // Entry N is the resolved render behavior for glTF material N.
    std::vector<PrimitiveMaterialBinding> primitiveMaterials;
};

[[nodiscard]] MeshData loadGltfMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

[[nodiscard]] SkinnedMeshData loadGltfSkinnedMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

// Binds source-scale static geometry to a named skeleton node. The attachment
// is transformed by the sampled node pose and merged into every skinned frame.
void addSkinnedAttachment(
    SkinnedMeshData& mesh,
    MeshData attachment,
    std::string_view nodeName);

[[nodiscard]] GltfAnimationClip loadGltfAnimationClip(
    const std::filesystem::path& path,
    uint32_t animationIndex);

// Names of all animations in a glTF/GLB file, in index order (unnamed clips
// get "animation N"). Returns an empty list for files without animations or
// files this loader cannot parse; never throws.
[[nodiscard]] std::vector<std::string> listGltfAnimationNames(
    const std::filesystem::path& path);

[[nodiscard]] MeshData skinGltfMesh(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& animation,
    float timeSeconds);

// Skins with a pose blended between two clips (0 = from, 1 = to); used for
// short crossfades when the active animation changes.
[[nodiscard]] MeshData skinGltfMeshBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend);

// Samples the skeleton without transforming vertices. GPU skinning uploads
// these palettes instead of rebuilding every vertex on the CPU.
[[nodiscard]] SkinnedPoseMatrices sampleGltfSkinPose(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& animation,
    float timeSeconds);
[[nodiscard]] SkinnedPoseMatrices sampleGltfSkinPoseBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend);

} // namespace sokoban
