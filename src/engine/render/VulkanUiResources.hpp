#pragma once

#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

namespace sokoban {

class FontAtlas;

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
        const FontAtlas& font);
    void destroy();

    [[nodiscard]] VkImageView fontImageView() const { return fontImage_.view; }
    [[nodiscard]] VkSampler fontSampler() const { return fontSampler_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    vulkanResources::OwnedImage fontImage_ {};
    VkSampler fontSampler_ = VK_NULL_HANDLE;
};

} // namespace sokoban
