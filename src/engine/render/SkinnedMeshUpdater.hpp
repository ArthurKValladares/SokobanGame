#pragma once

#include "engine/render/AnimationController.hpp"
#include "engine/render/GltfMesh.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace sokoban {

// Owns one skinned source mesh and its dynamic Vulkan vertex/index buffers.
// AnimationController decides what to sample; this class performs the CPU
// skinning and uploads the resulting vertices.
class SkinnedMeshUpdater {
public:
    struct MeshView {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    SkinnedMeshUpdater() = default;
    ~SkinnedMeshUpdater();

    SkinnedMeshUpdater(const SkinnedMeshUpdater&) = delete;
    SkinnedMeshUpdater& operator=(const SkinnedMeshUpdater&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        SkinnedMeshData sourceMesh,
        const GltfAnimationClip& initialClip);
    void destroy();
    void update(const AnimationController::SkinningRequest& request);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] MeshView mesh() const;

private:
    struct OwnedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    [[nodiscard]] uint32_t findMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties) const;
    [[nodiscard]] OwnedBuffer createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties) const;
    void uploadVertices(const std::vector<MeshVertex>& vertices) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    SkinnedMeshData sourceMesh_ {};
    OwnedBuffer vertexBuffer_ {};
    OwnedBuffer indexBuffer_ {};
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
};

} // namespace sokoban
