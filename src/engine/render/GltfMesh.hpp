#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sokoban {

struct MeshVertex {
    Vec3 position {};
    Vec3 normal {};
    Vec2 uv {};
    float textureIndex = 0.0f;
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

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SkeletonNode> nodes;
    std::vector<uint32_t> jointNodeIndices;
    std::vector<Mat4> inverseBindMatrices;
    Vec3 sourceMinimum {};
    Vec3 sourceMaximum {};
    bool preserveAspectRatio = false;
    bool rotateHalfTurn = false;
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
    bool rotateHalfTurn = false;
    bool usePrimitiveMaterialTextures = false;
};

[[nodiscard]] MeshData loadGltfMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

[[nodiscard]] SkinnedMeshData loadGltfSkinnedMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

[[nodiscard]] GltfAnimationClip loadGltfAnimationClip(
    const std::filesystem::path& path,
    uint32_t animationIndex);

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

} // namespace sokoban
