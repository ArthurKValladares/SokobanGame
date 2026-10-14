#pragma once

#include "engine/render/GeometrySuballocator.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanUploadRing.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sokoban {

struct MeshData;

// Owns lazily-created device-local vertex/index blocks for static and packed
// GPU-skinned meshes. Suballocated slices arrive through a shared persistently
// mapped upload ring; the caller owns upload and frame completion tracking.
class VulkanGeometryArena {
public:
    struct BufferStatistics {
        VkDeviceSize usedBytes = 0;
        VkDeviceSize reservedBytes = 0;
        uint32_t blockCount = 0;
    };

    struct Statistics {
        BufferStatistics vertex {};
        BufferStatistics index {};

        [[nodiscard]] VkDeviceSize usedBytes() const noexcept
        {
            return vertex.usedBytes + index.usedBytes;
        }
        [[nodiscard]] VkDeviceSize reservedBytes() const noexcept
        {
            return vertex.reservedBytes + index.reservedBytes;
        }
        [[nodiscard]] uint32_t blockCount() const noexcept
        {
            return vertex.blockCount + index.blockCount;
        }
    };

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
        VulkanMemoryAllocator& memoryAllocator,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VulkanUploadRing& uploadRing);
    // The owner must first complete every upload and frame draw referencing
    // this arena. The arena does not wait for GPU work itself.
    void destroy();

    [[nodiscard]] Allocation allocate(
        VkDeviceSize vertexBytes,
        VkDeviceSize indexBytes);
    // Only release after every GPU reference to these slices has completed.
    // Empty backing buffers are destroyed immediately; other slice indices
    // remain stable while vacant block slots are reused by later allocations.
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

    // Used bytes are exact requested slice payload, including uploads and
    // delayed retirement until release(). Reserved bytes are VkBuffer capacity,
    // including free space and alignment gaps. VMA allocation padding and heap
    // backing are reported separately by VulkanMemoryAllocator.
    [[nodiscard]] Statistics statistics() const noexcept;

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    // Rejects one index block creation after the vertex slice is allocated,
    // exercising rollback without requesting an unsafe oversized VkBuffer.
    static void failNextIndexBlockCreationForTesting();
#endif

private:
    struct Block {
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        GeometrySuballocator allocator;
    };
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
    void destroyBlock(Block& block) const noexcept;

    VulkanMemoryAllocator* memoryAllocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VulkanUploadRing* uploadRing_ = nullptr;
    std::vector<Block> vertexBlocks_;
    std::vector<Block> indexBlocks_;
};

} // namespace sokoban
