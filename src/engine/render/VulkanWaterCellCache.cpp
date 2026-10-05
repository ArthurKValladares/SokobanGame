#include "engine/render/VulkanWaterCellCache.hpp"

#include <stdexcept>

namespace sokoban {

VulkanWaterCellCache::~VulkanWaterCellCache()
{
    for (const Frame& frame : frames_) {
        allocator_->destroyBuffer(frame.buffer, frame.allocation);
    }
}

void VulkanWaterCellCache::create(
    VulkanMemoryAllocator& allocator,
    uint32_t frameCount,
    bool enabled)
{
    if (allocator_ || frameCount == 0) {
        throw std::runtime_error("Invalid water cell cache creation");
    }
    allocator_ = &allocator;
    enabled_ = enabled;
    // Two 128x128 domains, two vec4s per feature, plus the std430 header.
    bytesPerFrame_ = sizeof(WaterCellCachePlan) +
        (enabled ? 2 * WaterCellCachePlan::extent * WaterCellCachePlan::extent *
                 WaterCellCachePlan::featureBytes
                 : 0);
    frames_.resize(frameCount);
    buffers_.reserve(frameCount);
    for (Frame& frame : frames_) {
        const VkBufferCreateInfo info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bytesPerFrame_,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        allocator.createBuffer(
            info,
            VulkanMemoryUsage::DeviceLocal,
            frame.buffer,
            frame.allocation,
            nullptr,
            "Water cell features");
        buffers_.push_back({ frame.buffer, 0, bytesPerFrame_ });
    }
}

void VulkanWaterCellCache::invalidate()
{
    for (Frame& frame : frames_) {
        frame.dirty = true;
    }
}

void VulkanWaterCellCache::record(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    VkPipeline pipeline,
    VkPipelineLayout layout,
    VkDescriptorSet descriptors,
    const RenderFrameData& frame)
{
    Frame& cache = frames_.at(frameIndex);
    const WaterCellCachePlan plan = waterCellCachePlan(frame, enabled_);
    if (cache.initialized && !cache.dirty && cache.plan == plan) {
        return;
    }
    const bool dispatch = plan.dimensions[3] != 0;
    const VkPipelineStageFlags writeStage = dispatch
        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        : VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkAccessFlags writeAccess =
        dispatch ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    VkBufferMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = cache.initialized ? VK_ACCESS_SHADER_READ_BIT : 0U,
        .dstAccessMask = writeAccess,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = cache.buffer,
        .offset = 0,
        .size = bytesPerFrame_,
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        cache.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                          : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        writeStage,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr);
    if (dispatch) {
        vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            layout,
            0,
            1,
            &descriptors,
            0,
            nullptr);
        vkCmdPushConstants(
            commandBuffer,
            layout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(plan),
            &plan);
        vkCmdDispatch(
            commandBuffer, plan.dimensions[0] / 8, plan.dimensions[1] / 8, 2);
        ++rebuilds_;
    } else {
        vkCmdUpdateBuffer(commandBuffer, cache.buffer, 0, sizeof(plan), &plan);
    }
    barrier.srcAccessMask = writeAccess;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        writeStage,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr);
    cache.plan = plan;
    cache.initialized = true;
    cache.dirty = false;
}

} // namespace sokoban
