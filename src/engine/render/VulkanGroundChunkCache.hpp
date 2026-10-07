#pragma once

#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/GpuMappedBuffer.hpp"
#include <memory>
#include <vector>

namespace sokoban {

// Render-thread publication. Immutable geometry stays device-local; replaced
// buffers retire only after their upload and every referencing frame finishes.
class VulkanGroundChunkCache {
public:
    struct MeshView {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize vertexOffset = 0;
        VkDeviceSize indexOffset = 0;
        uint32_t indexCount = 0;
    };
    void create(VulkanMemoryAllocator& allocator, VkDevice device,
        VkCommandPool pool, VkQueue queue);
    void destroy() noexcept;
    bool update(std::shared_ptr<const GroundChunkGeometry> geometry, uint32_t pendingFrameMask);
    void completeFrame(uint32_t frameIndex);
    [[nodiscard]] bool readyFor(const GroundChunkGeometry* geometry) const noexcept;
    [[nodiscard]] MeshView mesh(const GroundChunkGeometry* geometry, std::size_t chunk) const;
    [[nodiscard]] uint64_t residentBytes() const noexcept;
    [[nodiscard]] uint64_t stagingBytes() const noexcept;
    [[nodiscard]] uint64_t uploadCount() const noexcept { return uploadCount_; }
private:
    struct Entry {
        std::shared_ptr<const GroundChunkGeometry> geometry;
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        GpuMappedBuffer staging;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::vector<MeshView> meshes;
        VkDeviceSize bytes = 0;
        uint32_t pendingFrameMask = 0;
        bool ready = false;
    };
    bool finishUpload(Entry& entry);
    void release(Entry& entry) noexcept;
    void collect();
    VulkanMemoryAllocator* allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::unique_ptr<Entry> current_;
    std::vector<std::unique_ptr<Entry>> retired_;
    uint64_t uploadCount_ = 0;
};
} // namespace sokoban
