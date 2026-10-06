#include "engine/render/VulkanFrameCapture.hpp"

#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <cstring>
#include <stdexcept>

namespace sokoban {
namespace {

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
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkImage sourceImage,
    VkFormat sourceFormat,
    VkImageLayout sourceLayout,
    VkOffset2D offset,
    VkExtent2D extent,
    VkExtent2D outputExtent)
{
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error("Cannot capture a zero-sized region");
    }

    constexpr uint32_t channels = 4;
    if (!outputExtent.width || !outputExtent.height) outputExtent = extent;
    const VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(outputExtent.width) * outputExtent.height * channels;

    VkBuffer staging = VK_NULL_HANDLE;
    VulkanAllocation stagingAllocation = nullptr;
    void* mapped = nullptr;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    vulkanResources::OwnedImage resized;

    const auto cleanup = [&] {
        if (fence) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (commandBuffer) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
        allocator.destroyBuffer(staging, stagingAllocation);
        vulkanResources::destroyImage(allocator, device, resized);
    };

    try {
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = byteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        allocator.createBuffer(
            bufferInfo,
            VulkanMemoryUsage::HostReadback,
            staging,
            stagingAllocation,
            &mapped,
            "Frame capture readback");

        commandBuffer = vulkanResources::beginOneShotCommands(
            device, commandPool, "capture");

        // The caller has already finished rendering into this image, so the
        // only synchronisation needed is the layout move to transfer-source
        // and back.
        vulkanResources::transitionImage(
            commandBuffer,
            sourceImage,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_WRITE_BIT,
                sourceLayout,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            });

        VkImage copyImage = sourceImage;
        VkOffset2D copyOffset = offset;
        if (extent.width != outputExtent.width || extent.height != outputExtent.height) {
            const VkImageCreateInfo imageInfo { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D, .format = sourceFormat,
                .extent = { outputExtent.width, outputExtent.height, 1 }, .mipLevels = 1, .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
            resized = vulkanResources::createImage(allocator, device, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT, "Capture resample");
            vulkanResources::transitionImage(commandBuffer, resized.image,
                vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT), {},
                { VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL });
            const VkImageBlit2 blit { .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .srcOffsets = { VkOffset3D { offset.x, offset.y, 0 },
                    VkOffset3D { offset.x + static_cast<int32_t>(extent.width), offset.y + static_cast<int32_t>(extent.height), 1 } },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .dstOffsets = { VkOffset3D { 0, 0, 0 },
                    VkOffset3D { static_cast<int32_t>(outputExtent.width), static_cast<int32_t>(outputExtent.height), 1 } } };
            const VkBlitImageInfo2 blitInfo { .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                .srcImage = sourceImage, .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .dstImage = resized.image, .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .regionCount = 1, .pRegions = &blit, .filter = VK_FILTER_LINEAR };
            vkCmdBlitImage2(commandBuffer, &blitInfo);
            vulkanResources::transitionImage(commandBuffer, resized.image,
                vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
                { VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
                { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL });
            copyImage = resized.image;
            copyOffset = {};
        }

        const VkBufferImageCopy region {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { copyOffset.x, copyOffset.y, 0 },
            .imageExtent = { outputExtent.width, outputExtent.height, 1 },
        };
        vkCmdCopyImageToBuffer(
            commandBuffer,
            copyImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            staging,
            1,
            &region);

        // Restore the layout the caller left it in, so the next frame's
        // rendering is unaffected by having captured.
        vulkanResources::transitionImage(
            commandBuffer,
            sourceImage,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                sourceLayout,
            });
        fence = vulkanResources::submitOneShotCommands(
            device, graphicsQueue, commandBuffer, "capture");
        vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences capture failed");

        ImageData image;
        image.width = outputExtent.width;
        image.height = outputExtent.height;
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
        cleanup();
        return image;
    } catch (...) {
        cleanup();
        throw;
    }
}

} // namespace sokoban
