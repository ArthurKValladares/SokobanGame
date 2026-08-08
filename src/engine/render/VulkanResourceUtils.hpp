#pragma once

#include <vulkan/vulkan.h>

#include <span>

namespace sokoban {

void vkCheck(VkResult result, const char* message);

namespace vulkanResources {

struct ImageState {
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

[[nodiscard]] constexpr VkImageSubresourceRange subresourceRange(
    VkImageAspectFlags aspectMask)
{
    return {
        .aspectMask = aspectMask,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] VkImageMemoryBarrier2 imageBarrier(
    VkImage image,
    VkImageSubresourceRange range,
    ImageState from,
    ImageState to);
void transitionImages(
    VkCommandBuffer commandBuffer,
    std::span<const VkImageMemoryBarrier2> barriers,
    VkDependencyFlags dependencyFlags = 0);
void transitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageSubresourceRange range,
    ImageState from,
    ImageState to,
    VkDependencyFlags dependencyFlags = 0);

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

} // namespace vulkanResources
} // namespace sokoban
