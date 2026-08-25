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

    // Whether these settings make the occlusion pass sample scene depth.
    //
    // The recorder copies resolved depth into the sampled depth image purely
    // so this pass can read it, and that copy is a full render-extent
    // vkCmdCopyImage plus four barriers. Both decisions must key off the same
    // predicate, or a frame either pays for a copy nothing reads or samples a
    // stale one.
    [[nodiscard]] static constexpr bool samplesSceneDepth(
        const RenderFrameData::Lighting::AmbientOcclusion& settings)
    {
        return settings.enabled &&
            (settings.strength > 0.0f || settings.visualize);
    }

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
