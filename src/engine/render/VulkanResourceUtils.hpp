#pragma once

#include <vulkan/vulkan.h>

namespace sokoban::vulkanResources {

struct OwnedImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

[[nodiscard]] uint32_t findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties);
[[nodiscard]] VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask);
[[nodiscard]] OwnedImage createImage(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    const VkImageCreateInfo& imageInfo,
    VkImageAspectFlags aspectMask);
void destroyImage(VkDevice device, OwnedImage& image);

} // namespace sokoban::vulkanResources
