#pragma once

#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

// Owns the two reduced-resolution HDR targets used by the separable bloom
// filter. Scene recording renders the bright-pass/horizontal blur into the
// first image, then the vertical blur into the second. The second image stays
// shader-readable for the tonemap composite.
class VulkanBloomPass {
public:
    VulkanBloomPass() = default;
    ~VulkanBloomPass();

    VulkanBloomPass(const VulkanBloomPass&) = delete;
    VulkanBloomPass& operator=(const VulkanBloomPass&) = delete;

    void create(
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkExtent2D renderExtent,
        VkFormat format);
    void destroy();

    void prepareExtractTarget(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void publishExtractTarget(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void prepareBlurTarget(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void publishBlurTarget(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] VkImageView extractImageView() const
    {
        return extractImage_.view;
    }
    [[nodiscard]] VkImageView bloomImageView() const
    {
        return bloomImage_.view;
    }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }

private:
    void prepareTarget(
        VkCommandBuffer commandBuffer,
        vulkanResources::OwnedImage& image,
        bool readable,
        RenderStats& stats);
    void publishTarget(
        VkCommandBuffer commandBuffer,
        vulkanResources::OwnedImage& image,
        RenderStats& stats);

    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    VkExtent2D extent_ {};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    vulkanResources::OwnedImage extractImage_ {};
    vulkanResources::OwnedImage bloomImage_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool extractReadable_ = false;
    bool bloomReadable_ = false;
};

} // namespace sokoban
