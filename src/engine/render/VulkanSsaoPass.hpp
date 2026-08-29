#pragma once

#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

class VulkanSsaoPass {
public:
    struct Pipelines {
        VkPipeline occlusion = VK_NULL_HANDLE;
        // Also draws the debug visualization. Same shader, same state since
        // the composite stopped blending; params.w picks which.
        VkPipeline composite = VK_NULL_HANDLE;
    };

    // Whether these settings make this pass run at all.
    //
    // The recorder copies resolved depth into the sampled depth image purely
    // so the occlusion pass can read it, and copies resolved colour so the
    // composite has something to read that is not the attachment it writes.
    // Each is a full render-extent vkCmdCopyImage plus barriers. Every one of
    // those decisions must key off this same predicate, or a frame either
    // pays for a copy nothing reads or samples a stale one.
    [[nodiscard]] static constexpr bool samplesSceneDepth(
        const RenderFrameData::Lighting::AmbientOcclusion& settings)
    {
        return settings.enabled &&
            (settings.strength > 0.0f ||
                settings.debug !=
                    RenderFrameData::Lighting::AmbientOcclusion::Debug::Off);
    }

    VulkanSsaoPass() = default;
    ~VulkanSsaoPass();

    VulkanSsaoPass(const VulkanSsaoPass&) = delete;
    VulkanSsaoPass& operator=(const VulkanSsaoPass&) = delete;

    void create(
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkExtent2D extent);
    void recreate(VkExtent2D extent);
    void destroy();

    void record(
        VkCommandBuffer commandBuffer,
        VkImageView targetView,
        const RenderFrameData::Lighting::AmbientOcclusion& settings,
        const Mat4& clipFromView,
        VkDescriptorSet descriptorSet,
        VkPipelineLayout pipelineLayout,
        Pipelines pipelines,
        RenderStats& stats) const;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] VkImageView imageView() const { return image_.view; }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] VkExtent2D aoExtent() const { return aoExtent_; }

private:
    void createImage();
    void destroyImage();

    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    VkExtent2D renderExtent_ {};
    VkExtent2D aoExtent_ {};
    vulkanResources::OwnedImage image_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace sokoban
