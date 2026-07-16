#pragma once

#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

class VulkanShadowPass {
public:
    VulkanShadowPass() = default;
    ~VulkanShadowPass();

    VulkanShadowPass(const VulkanShadowPass&) = delete;
    VulkanShadowPass& operator=(const VulkanShadowPass&) = delete;

    void create(VkPhysicalDevice physicalDevice, VkDevice device, VkFormat format);
    void destroy();

    void begin(VkCommandBuffer commandBuffer, VkPipeline tilePipeline, RenderStats& stats);
    void bindModelPipeline(
        VkCommandBuffer commandBuffer,
        VkPipeline modelPipeline,
        RenderStats& stats) const;
    void end(VkCommandBuffer commandBuffer, RenderStats& stats);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] VkFormat format() const { return format_; }
    [[nodiscard]] VkImageView imageView() const { return image_.view; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    vulkanResources::OwnedImage image_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImageLayout imageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace sokoban
