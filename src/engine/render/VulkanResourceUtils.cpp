#include "engine/render/VulkanResourceUtils.hpp"

#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace sokoban {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw VulkanError(
            result,
            std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

void vkCheck(VkResult result, const char* call, std::string_view label)
{
    if (result != VK_SUCCESS) {
        throw VulkanError(
            result,
            std::string(call) + ' ' + std::string(label)
                + " failed (VkResult " + std::to_string(result) + ")");
    }
}

namespace vulkanResources {

VkCommandBuffer beginOneShotCommands(
    VkDevice device,
    VkCommandPool pool,
    std::string_view label,
    const char* debugName)
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo commandBufferInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkCheck(
        vkAllocateCommandBuffers(device, &commandBufferInfo, &commandBuffer),
        "vkAllocateCommandBuffers",
        label);
    if (debugName != nullptr) {
        vulkanDebug::setObjectName(
            device, VK_OBJECT_TYPE_COMMAND_BUFFER, commandBuffer, debugName);
    }
    const VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    const VkResult begun = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (begun != VK_SUCCESS) {
        vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
        vkCheck(begun, "vkBeginCommandBuffer", label);
    }
    return commandBuffer;
}

VkFence submitOneShotCommands(
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer commandBuffer,
    std::string_view label,
    const char* debugFenceName)
{
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer", label);

    VkFence fence = VK_NULL_HANDLE;
    const VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    vkCheck(
        vkCreateFence(device, &fenceInfo, nullptr, &fence),
        "vkCreateFence",
        label);
    if (debugFenceName != nullptr) {
        vulkanDebug::setObjectName(
            device, VK_OBJECT_TYPE_FENCE, fence, debugFenceName);
    }

    const VkCommandBufferSubmitInfo commandBufferSubmit {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commandBuffer,
    };
    const VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmit,
    };
    const VkResult submitted = vkQueueSubmit2(queue, 1, &submit, fence);
    if (submitted != VK_SUCCESS) {
        vkDestroyFence(device, fence, nullptr);
        vkCheck(submitted, "vkQueueSubmit2", label);
    }
    return fence;
}

VkImageMemoryBarrier2 imageBarrier(
    VkImage image,
    VkImageSubresourceRange range,
    ImageState from,
    ImageState to)
{
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = from.stageMask,
        .srcAccessMask = from.accessMask,
        .dstStageMask = to.stageMask,
        .dstAccessMask = to.accessMask,
        .oldLayout = from.layout,
        .newLayout = to.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
}

void transitionImages(
    VkCommandBuffer commandBuffer,
    std::span<const VkImageMemoryBarrier2> barriers,
    VkDependencyFlags dependencyFlags)
{
    const VkDependencyInfo dependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = dependencyFlags,
        .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void transitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageSubresourceRange range,
    ImageState from,
    ImageState to,
    VkDependencyFlags dependencyFlags)
{
    const VkImageMemoryBarrier2 barrier =
        imageBarrier(image, range, from, to);
    transitionImages(
        commandBuffer,
        std::span { &barrier, 1 },
        dependencyFlags);
}

VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask,
    std::string_view debugName)
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
    vulkanDebug::setObjectName(
        device, VK_OBJECT_TYPE_IMAGE_VIEW, view, debugName);
    return view;
}

OwnedImage createImage(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    const VkImageCreateInfo& imageInfo,
    VkImageAspectFlags aspectMask,
    std::string_view debugName)
{
    OwnedImage result;
    allocator.createDeviceImage(
        imageInfo, result.image, result.allocation, debugName);
    vulkanDebug::setObjectName(
        device, VK_OBJECT_TYPE_IMAGE, result.image, debugName);
    try {
        const std::string viewName = debugName.empty()
            ? std::string {}
            : std::string(debugName) + " view";
        result.view = createImageView(
            device,
            result.image,
            imageInfo.format,
            aspectMask,
            viewName);
    } catch (...) {
        destroyImage(allocator, device, result);
        throw;
    }
    return result;
}

void destroyImage(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    OwnedImage& image)
{
    if (image.view) {
        vkDestroyImageView(device, image.view, nullptr);
    }
    allocator.destroyImage(image.image, image.allocation);
    image = {};
}

} // namespace vulkanResources
} // namespace sokoban
