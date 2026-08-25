#pragma once

#include "engine/render/FrameTimeTelemetry.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace sokoban {

[[nodiscard]] double vulkanTimestampDeltaMilliseconds(
    uint64_t begin,
    uint64_t end,
    float timestampPeriodNanoseconds,
    uint32_t validBits) noexcept;

// Owns a two-timestamp query pair per in-flight frame. Results are read only
// after the renderer's existing frame fence has completed, so profiling does
// not add a CPU/GPU synchronization point.
class VulkanGpuProfiler {
public:
    void create(
        VkDevice device,
        float timestampPeriodNanoseconds,
        uint32_t timestampValidBits,
        uint32_t frameCount);
    void destroy() noexcept;

    VulkanGpuProfiler() = default;
    ~VulkanGpuProfiler();
    VulkanGpuProfiler(const VulkanGpuProfiler&) = delete;
    VulkanGpuProfiler& operator=(const VulkanGpuProfiler&) = delete;

    [[nodiscard]] bool supported() const { return queryPool_ != VK_NULL_HANDLE; }
    void collectCompletedFrame(uint32_t frameIndex);
    void beginFrame(VkCommandBuffer commandBuffer, uint32_t frameIndex) const;
    void endFrame(VkCommandBuffer commandBuffer, uint32_t frameIndex) const;
    void markSubmitted(uint32_t frameIndex);
    [[nodiscard]] std::optional<double> latestFrameMilliseconds() const;
    [[nodiscard]] FrameTimeSummary frameTimeSummary() const;

private:
    [[nodiscard]] uint32_t firstQuery(uint32_t frameIndex) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    float timestampPeriodNanoseconds_ = 0.0f;
    uint32_t timestampValidBits_ = 0;
    uint32_t frameCount_ = 0;
    FrameTimeTelemetry frameTimeTelemetry_ {};
    std::vector<bool> submitted_;
};

} // namespace sokoban
