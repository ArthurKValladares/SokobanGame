#pragma once

#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

class FontAtlas;
struct ImageData;

class VulkanUiResources {
public:
    VulkanUiResources() = default;
    ~VulkanUiResources();

    VulkanUiResources(const VulkanUiResources&) = delete;
    VulkanUiResources& operator=(const VulkanUiResources&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        const FontAtlas& font,
        const ImageData& titleBackground);
    void destroy();

    [[nodiscard]] VkImageView fontImageView() const { return fontImage_.view; }
    [[nodiscard]] VkImageView titleBackgroundImageView() const
    {
        return titleBackgroundImage_.view;
    }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    vulkanResources::OwnedImage fontImage_ {};
    vulkanResources::OwnedImage titleBackgroundImage_ {};
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace sokoban
