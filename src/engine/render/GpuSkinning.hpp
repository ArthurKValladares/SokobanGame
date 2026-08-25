#pragma once

#include "engine/render/GltfMesh.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sokoban {

inline constexpr uint32_t maxSkinJoints = 128;
inline constexpr uint32_t maxSkeletonNodes = 128;
inline constexpr uint32_t maxSkinPaletteMatrices =
    maxSkinJoints + maxSkeletonNodes;
inline constexpr uint32_t maxSkinnedInstancesPerFrame = 256;
inline constexpr uint32_t gpuSkinningFrameCount = 2;

[[nodiscard]] constexpr bool modelInstanceReadyForDraw(
    bool meshReady,
    bool requiresPublishedPose,
    bool posePublished)
{
    return meshReady && (!requiresPublishedPose || posePublished);
}

struct alignas(16) GpuSkinningInstance {
    std::array<Mat4, maxSkinPaletteMatrices> palette {};
    Mat4 modelFromSource {};
    Mat4 normalFromSource {};
};

static_assert(sizeof(Mat4) == 64);
static_assert(sizeof(GpuSkinningInstance) ==
    (maxSkinPaletteMatrices + 2) * sizeof(Mat4));

[[nodiscard]] std::vector<GpuSkinnedVertex> makeGpuSkinnedVertices(
    const SkinnedMeshData& mesh);
[[nodiscard]] std::vector<uint32_t> makeGpuSkinnedIndices(
    const SkinnedMeshData& mesh);
[[nodiscard]] GpuSkinningInstance makeGpuSkinningInstance(
    const SkinnedMeshData& mesh,
    const SkinnedPoseMatrices& pose);

} // namespace sokoban
