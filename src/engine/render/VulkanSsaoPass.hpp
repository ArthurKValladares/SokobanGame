#pragma once

#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

class VulkanSsaoPass {
public:
    struct Pipelines {
        VkPipeline occlusion = VK_NULL_HANDLE;
        VkPipeline composite = VK_NULL_HANDLE;
        VkPipeline visualize = VK_NULL_HANDLE;
    };

    VulkanSsaoPass() = default;
    ~VulkanSsaoPass();

    VulkanSsaoPass(const VulkanSsaoPass&) = delete;
    VulkanSsaoPass& operator=(const VulkanSsaoPass&) = delete;

    void create(VkPhysicalDevice physicalDevice, VkDevice device, VkExtent2D extent);
    void recreate(VkExtent2D extent);
    void destroy();

    void record(
        VkCommandBuffer commandBuffer,
        VkImageView targetView,
        VkImage depthSource,
        const RenderFrameData::Lighting::AmbientOcclusion& settings,
        VkDescriptorSet descriptorSet,
        VkPipelineLayout pipelineLayout,
        Pipelines pipelines,
        RenderStats& stats) const;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] VkImageView imageView() const { return image_.view; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

private:
    void createImage();
    void destroyImage();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkExtent2D extent_ {};
    vulkanResources::OwnedImage image_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace sokoban
