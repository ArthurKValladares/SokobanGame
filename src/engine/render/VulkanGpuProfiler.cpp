#include "engine/render/VulkanGpuProfiler.hpp"

#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sokoban {

double vulkanTimestampDeltaMilliseconds(
    uint64_t begin,
    uint64_t end,
    float timestampPeriodNanoseconds,
    uint32_t validBits) noexcept
{
    if (timestampPeriodNanoseconds <= 0.0f || validBits == 0) {
        return 0.0;
    }
    const uint64_t ticks = end >= begin
        ? end - begin
        : validBits >= 64
            ? (std::numeric_limits<uint64_t>::max() - begin) + end + 1U
            : ((uint64_t { 1 } << validBits) - begin) + end;
    return static_cast<double>(ticks) * timestampPeriodNanoseconds / 1'000'000.0;
}

VulkanGpuProfiler::~VulkanGpuProfiler()
{
    destroy();
}

void VulkanGpuProfiler::create(
    VkDevice device,
    float timestampPeriodNanoseconds,
    uint32_t timestampValidBits,
    uint32_t frameCount)
{
    destroy();
    if (!device || timestampPeriodNanoseconds <= 0.0f ||
        timestampValidBits == 0 || frameCount == 0) {
        return;
    }
    const VkQueryPoolCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = frameCount * 2,
    };
    VkQueryPool queryPool = VK_NULL_HANDLE;
    const VkResult result = vkCreateQueryPool(device, &createInfo, nullptr, &queryPool);
    if (result == VK_ERROR_FEATURE_NOT_PRESENT) {
        return;
    }
    vkCheck(result, "vkCreateQueryPool timestamp profiler failed");
    device_ = device;
    queryPool_ = queryPool;
    timestampPeriodNanoseconds_ = timestampPeriodNanoseconds;
    timestampValidBits_ = timestampValidBits;
    frameCount_ = frameCount;
    submitted_.assign(frameCount, false);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_QUERY_POOL, queryPool_, "GPU frame timestamps");
}

void VulkanGpuProfiler::destroy() noexcept
{
    if (device_ && queryPool_) {
        vkDestroyQueryPool(device_, queryPool_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    queryPool_ = VK_NULL_HANDLE;
    timestampPeriodNanoseconds_ = 0.0f;
    timestampValidBits_ = 0;
    frameCount_ = 0;
    frameTimeTelemetry_.reset();
    submitted_.clear();
}

void VulkanGpuProfiler::collectCompletedFrame(uint32_t frameIndex)
{
    if (!supported() || frameIndex >= frameCount_ || !submitted_[frameIndex]) {
        return;
    }
    std::array<uint64_t, 2> timestamps {};
    const VkResult result = vkGetQueryPoolResults(
        device_,
        queryPool_,
        firstQuery(frameIndex),
        static_cast<uint32_t>(timestamps.size()),
        sizeof(timestamps),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (result == VK_SUCCESS) {
        frameTimeTelemetry_.record(vulkanTimestampDeltaMilliseconds(
            timestamps[0],
            timestamps[1],
            timestampPeriodNanoseconds_,
            timestampValidBits_));
        submitted_[frameIndex] = false;
    } else if (result != VK_NOT_READY) {
        vkCheck(result, "vkGetQueryPoolResults timestamp profiler failed");
    }
}

void VulkanGpuProfiler::beginFrame(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex) const
{
    if (!supported() || frameIndex >= frameCount_) {
        return;
    }
    vkCmdResetQueryPool(commandBuffer, queryPool_, firstQuery(frameIndex), 2);
    vkCmdWriteTimestamp2(
        commandBuffer,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        queryPool_,
        firstQuery(frameIndex));
}

void VulkanGpuProfiler::endFrame(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex) const
{
    if (!supported() || frameIndex >= frameCount_) {
        return;
    }
    vkCmdWriteTimestamp2(
        commandBuffer,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        queryPool_,
        firstQuery(frameIndex) + 1);
}

void VulkanGpuProfiler::markSubmitted(uint32_t frameIndex)
{
    if (supported() && frameIndex < frameCount_) {
        submitted_[frameIndex] = true;
    }
}

std::optional<double> VulkanGpuProfiler::latestFrameMilliseconds() const
{
    const FrameTimeSummary summary = frameTimeTelemetry_.summary();
    return summary.available()
        ? std::optional<double> { summary.latestMilliseconds }
        : std::nullopt;
}

FrameTimeSummary VulkanGpuProfiler::frameTimeSummary() const
{
    return frameTimeTelemetry_.summary();
}

uint32_t VulkanGpuProfiler::firstQuery(uint32_t frameIndex) const
{
    return frameIndex * 2;
}

} // namespace sokoban
