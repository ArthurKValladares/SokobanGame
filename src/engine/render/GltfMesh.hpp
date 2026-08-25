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
    Vec2 uv {};
    // One-based global descriptor index; zero means untextured.
    uint32_t textureIndex = 0;
    // Bitwise PrimitiveMaterialFlag values resolved from manifest metadata.
    uint32_t materialFlags = 0;
};

enum PrimitiveMaterialFlag : uint32_t {
    PrimitiveMaterialNone = 0,
    PrimitiveMaterialScrollV = 1U << 0U,
};

struct PrimitiveMaterialBinding {
    uint32_t textureIndex = 0;
    uint32_t flags = PrimitiveMaterialNone;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

struct Mat4 {
    std::array<float, 16> values {};
};

struct SkinnedVertex {
    Vec3 position {};
    Vec3 normal {};
    Vec2 uv {};
    std::array<uint16_t, 4> joints {};
    std::array<float, 4> weights {};
};

struct SkeletonNode {
    std::string name;
    int parent = -1;
    Vec3 translation {};
    Vec4 rotation { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

struct SkinnedAttachment {
    MeshData mesh;
    uint32_t nodeIndex = 0;
};

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
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
    Vec2 uv {};
    uint32_t textureIndex = 0;
    uint32_t materialFlags = 0;
    std::array<uint16_t, 4> joints {};
    std::array<float, 4> weights {};
    uint32_t attachmentNodeIndex = UINT32_MAX;
};

struct SkinnedPoseMatrices {
    std::vector<Mat4> jointMatrices;
    std::vector<Mat4> nodeMatrices;
};

struct AnimationKeyframes {
    std::vector<float> times;
    std::vector<Vec4> values;
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
