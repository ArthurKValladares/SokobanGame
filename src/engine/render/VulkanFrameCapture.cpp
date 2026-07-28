#include "engine/render/VulkanFrameCapture.hpp"

#include "engine/render/VulkanResourceUtils.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace sokoban {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

// Swapchain colour formats are commonly BGRA; ImageData and the PNG writer are
// RGBA, so the channel order is fixed up on the CPU during the copy out.
[[nodiscard]] bool formatIsBgra(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_UNORM ||
        format == VK_FORMAT_B8G8R8A8_SRGB ||
        format == VK_FORMAT_B8G8R8A8_SNORM;
}

} // namespace

ImageData captureImageRegion(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkImage sourceImage,
    VkFormat sourceFormat,
    VkImageLayout sourceLayout,
    VkOffset2D offset,
    VkExtent2D extent)
{
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error("Cannot capture a zero-sized region");
    }

    constexpr uint32_t channels = 4;
    const VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(extent.width) * extent.height * channels;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    const auto cleanup = [&] {
        if (fence) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (commandBuffer) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
        if (staging) {
            vkDestroyBuffer(device, staging, nullptr);
        }
        if (stagingMemory) {
            vkFreeMemory(device, stagingMemory, nullptr);
        }
    };

    try {
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = byteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &staging),
            "vkCreateBuffer capture staging failed");

        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device, staging, &requirements);
        const VkMemoryAllocateInfo allocation {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = vulkanResources::findMemoryType(
                physicalDevice,
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(device, &allocation, nullptr, &stagingMemory),
            "vkAllocateMemory capture staging failed");
        vkCheck(vkBindBufferMemory(device, staging, stagingMemory, 0),
            "vkBindBufferMemory capture staging failed");

        const VkCommandBufferAllocateInfo commandBufferInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkCheck(
            vkAllocateCommandBuffers(device, &commandBufferInfo, &commandBuffer),
            "vkAllocateCommandBuffers capture failed");

        const VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer capture failed");

        // The caller has already finished rendering into this image, so the
        // only synchronisation needed is the layout move to transfer-source
        // and back.
        const VkImageMemoryBarrier2 toTransfer {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = sourceLayout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sourceImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        const VkDependencyInfo toTransferDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &toTransfer,
        };
        vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);

        const VkBufferImageCopy region {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { offset.x, offset.y, 0 },
            .imageExtent = { extent.width, extent.height, 1 },
        };
        vkCmdCopyImageToBuffer(
            commandBuffer,
            sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging,
            1,
            &region);

        // Restore the layout the caller left it in, so the next frame's
        // rendering is unaffected by having captured.
        const VkImageMemoryBarrier2 restore {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT |
                VK_ACCESS_2_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = sourceLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sourceImage,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        const VkDependencyInfo restoreDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &restore,
        };
        vkCmdPipelineBarrier2(commandBuffer, &restoreDependency);
        vkCheck(vkEndCommandBuffer(commandBuffer),
            "vkEndCommandBuffer capture failed");

        const VkFenceCreateInfo fenceInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        };
        vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &fence),
            "vkCreateFence capture failed");

        const VkCommandBufferSubmitInfo commandBufferSubmit {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer,
        };
        const VkSubmitInfo2 submit {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandBufferSubmit,
        };
        vkCheck(vkQueueSubmit2(graphicsQueue, 1, &submit, fence),
            "vkQueueSubmit2 capture failed");
        vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences capture failed");

        void* mapped = nullptr;
        vkCheck(vkMapMemory(device, stagingMemory, 0, byteCount, 0, &mapped),
            "vkMapMemory capture failed");

        ImageData image;
        image.width = extent.width;
        image.height = extent.height;
        image.rgba.resize(static_cast<std::size_t>(byteCount));
        const auto* source = static_cast<const uint8_t*>(mapped);
        const bool swizzle = formatIsBgra(sourceFormat);
        for (std::size_t i = 0; i < image.rgba.size(); i += channels) {
            const uint8_t r = swizzle ? source[i + 2] : source[i + 0];
            const uint8_t g = source[i + 1];
            const uint8_t b = swizzle ? source[i + 0] : source[i + 2];
            image.rgba[i + 0] = static_cast<std::byte>(r);
            image.rgba[i + 1] = static_cast<std::byte>(g);
            image.rgba[i + 2] = static_cast<std::byte>(b);
            // The scene renders opaque; a capture is a screenshot, so force
            // full alpha rather than trusting whatever the attachment held.
            image.rgba[i + 3] = static_cast<std::byte>(255);
        }
        vkUnmapMemory(device, stagingMemory);

        cleanup();
        return image;
    } catch (...) {
        cleanup();
        throw;
    }
}

} // namespace sokoban
