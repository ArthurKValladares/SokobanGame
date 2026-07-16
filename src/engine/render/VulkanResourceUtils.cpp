#include "engine/render/VulkanResourceUtils.hpp"

#include <stdexcept>
#include <string>

namespace sokoban::vulkanResources {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

} // namespace

uint32_t findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable Vulkan image memory type found");
}

VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask)
{
    VkImageViewCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkImageView view = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(device, &createInfo, nullptr, &view), "vkCreateImageView failed");
    return view;
}

OwnedImage createImage(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    const VkImageCreateInfo& imageInfo,
    VkImageAspectFlags aspectMask)
{
    OwnedImage result;
    vkCheck(vkCreateImage(device, &imageInfo, nullptr, &result.image), "vkCreateImage failed");
    try {
        VkMemoryRequirements requirements {};
        vkGetImageMemoryRequirements(device, result.image, &requirements);
        VkMemoryAllocateInfo allocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(
                physicalDevice,
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &result.memory),
            "vkAllocateMemory image failed");
        vkCheck(vkBindImageMemory(device, result.image, result.memory, 0),
            "vkBindImageMemory failed");
        result.view = createImageView(device, result.image, imageInfo.format, aspectMask);
    } catch (...) {
        destroyImage(device, result);
        throw;
    }
    return result;
}

void destroyImage(VkDevice device, OwnedImage& image)
{
    if (image.view) {
        vkDestroyImageView(device, image.view, nullptr);
    }
    if (image.image) {
        vkDestroyImage(device, image.image, nullptr);
    }
    if (image.memory) {
        vkFreeMemory(device, image.memory, nullptr);
    }
    image = {};
}

} // namespace sokoban::vulkanResources
