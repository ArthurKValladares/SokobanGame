#pragma once

#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/WaterCellCachePlan.hpp"

#include <vector>

namespace sokoban {

// One cache per frame in flight: record() runs only after that frame's fence.
// Buffers retire with the descriptor sets and pipelines that reference them.
class VulkanWaterCellCache {
public:
    ~VulkanWaterCellCache();
    VulkanWaterCellCache() = default;
    VulkanWaterCellCache(const VulkanWaterCellCache&) = delete;
    VulkanWaterCellCache& operator=(const VulkanWaterCellCache&) = delete;

    void create(
        VulkanMemoryAllocator& allocator,
        uint32_t frameCount,
        bool enabled);
    void invalidate();
    void record(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        VkPipeline pipeline,
        VkPipelineLayout layout,
        VkDescriptorSet descriptors,
        const RenderFrameData& frame);
    [[nodiscard]] const std::vector<VkDescriptorBufferInfo>& buffers() const
    {
        return buffers_;
    }
    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] uint64_t rebuilds() const { return rebuilds_; }
    [[nodiscard]] VkDeviceSize bytes() const
    {
        return bytesPerFrame_ * frames_.size();
    }

private:
    struct Frame {
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        WaterCellCachePlan plan;
        bool initialized = false;
        bool dirty = true;
    };
    VulkanMemoryAllocator* allocator_ = nullptr;
    std::vector<Frame> frames_;
    std::vector<VkDescriptorBufferInfo> buffers_;
    VkDeviceSize bytesPerFrame_ = 0;
    uint64_t rebuilds_ = 0;
    bool enabled_ = false;
};

} // namespace sokoban
