#pragma once

#include "engine/render/GeometrySuballocator.hpp"
#include "engine/render/VulkanUploadRing.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sokoban {

struct MeshData;

// Owns lazily-created device-local geometry blocks. Static meshes receive
// suballocated slices and arrive there through a shared persistently mapped
// upload ring. Dynamic skinning intentionally remains separate until its
// GPU implementation replaces the CPU updater.
class VulkanGeometryArena {
public:
    struct Allocation {
        uint32_t vertexBlock = UINT32_MAX;
        GeometrySuballocator::Allocation vertex {};
        uint32_t indexBlock = UINT32_MAX;
        GeometrySuballocator::Allocation index {};

        [[nodiscard]] bool valid() const
        {
            return vertex.valid() && index.valid();
        }
    };

    struct Upload {
        VulkanUploadRing::Reservation staging {};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool submitted = false;
    };

    VulkanGeometryArena() = default;
    ~VulkanGeometryArena();

    VulkanGeometryArena(const VulkanGeometryArena&) = delete;
    VulkanGeometryArena& operator=(const VulkanGeometryArena&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VulkanUploadRing& uploadRing);
    void destroy();

    [[nodiscard]] Allocation allocate(
        VkDeviceSize vertexBytes,
        VkDeviceSize indexBytes);
    void release(Allocation& allocation);
    [[nodiscard]] Upload beginUpload(
        const Allocation& allocation,
        const MeshData& mesh) const;
    [[nodiscard]] Upload beginUpload(
        const Allocation& allocation,
        const void* vertexData,
        VkDeviceSize vertexBytes,
        const void* indexData,
        VkDeviceSize indexBytes) const;
    [[nodiscard]] bool uploadComplete(const Upload& upload) const;
    void destroyUpload(Upload& upload) const;

    [[nodiscard]] VkBuffer vertexBuffer(const Allocation& allocation) const;
    [[nodiscard]] VkBuffer indexBuffer(const Allocation& allocation) const;
    [[nodiscard]] VkDeviceSize vertexOffset(const Allocation& allocation) const;
    [[nodiscard]] VkDeviceSize indexOffset(const Allocation& allocation) const;

private:
    struct Block {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        GeometrySuballocator allocator;
    };

    [[nodiscard]] uint32_t findMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties) const;
    [[nodiscard]] Block createBlock(
        VkDeviceSize capacity,
        VkBufferUsageFlags usage,
        const char* name) const;
    [[nodiscard]] GeometrySuballocator::Allocation allocateSlice(
        std::vector<Block>& blocks,
        uint32_t& blockIndex,
        VkDeviceSize bytes,
        VkBufferUsageFlags usage,
        VkDeviceSize preferredBlockBytes,
        const char* name);
    void destroyBlock(Block& block) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VulkanUploadRing* uploadRing_ = nullptr;
    std::vector<Block> vertexBlocks_;
    std::vector<Block> indexBlocks_;
};

} // namespace sokoban
