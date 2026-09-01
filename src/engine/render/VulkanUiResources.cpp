#include "engine/render/VulkanUiResources.hpp"

#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include "engine/ui/FontAtlas.hpp"

#include <cstring>
#include <span>
#include <stdexcept>

namespace sokoban {
namespace {

struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocation allocation = nullptr;
    void* mapped = nullptr;
};

void destroyStaging(
    VulkanMemoryAllocator& allocator,
    StagingBuffer& staging)
{
    allocator.destroyBuffer(staging.buffer, staging.allocation);
    staging = {};
}

StagingBuffer createStaging(
    VulkanMemoryAllocator& allocator,
    std::span<const std::byte> pixels)
{
    StagingBuffer staging;
    const VkDeviceSize size = static_cast<VkDeviceSize>(pixels.size());
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    try {
        allocator.createBuffer(
            bufferInfo,
            VulkanMemoryUsage::HostSequentialWrite,
            staging.buffer,
            staging.allocation,
            &staging.mapped,
            "UI image staging");
        std::memcpy(staging.mapped, pixels.data(), pixels.size());
    } catch (...) {
        destroyStaging(allocator, staging);
        throw;
    }
    return staging;
}

vulkanResources::OwnedImage uploadImage(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    std::span<const std::byte> pixels)
{
    StagingBuffer staging;
    vulkanResources::OwnedImage image;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    try {
        staging = createStaging(allocator, pixels);
        const VkImageCreateInfo imageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { width, height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image = vulkanResources::createImage(
            allocator,
            device,
            imageInfo,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "UI texture");

        commandBuffer = vulkanResources::beginOneShotCommands(
            device, commandPool, "UI image upload");

        vulkanResources::transitionImage(
            commandBuffer,
            image.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {},
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            });

        const VkBufferImageCopy copy {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { width, height, 1 },
        };
        vkCmdCopyBufferToImage(
            commandBuffer,
            staging.buffer,
            image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copy);

        vulkanResources::transitionImage(
            commandBuffer,
            image.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            });
        // The only one of the six one-shot submits that does not close with
        // submitOneShotCommands. It submits against no fence and then waits on
        // the whole queue, which is a wider wait than this upload needs;
        // switching it to a fence is a behavioural change and wants a run to
        // confirm, not a cleanup.
        vkCheck(vkEndCommandBuffer(commandBuffer),
            "vkEndCommandBuffer UI image upload failed");

        const VkCommandBufferSubmitInfo commandInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commandBuffer,
        };
        const VkSubmitInfo2 submit {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &commandInfo,
        };
        vkCheck(vkQueueSubmit2(graphicsQueue, 1, &submit, VK_NULL_HANDLE),
            "vkQueueSubmit2 UI image upload failed");
        vkCheck(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle UI image upload failed");

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        commandBuffer = VK_NULL_HANDLE;
        destroyStaging(allocator, staging);
        return image;
    } catch (...) {
        if (commandBuffer) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
        destroyStaging(allocator, staging);
        vulkanResources::destroyImage(allocator, device, image);
        throw;
    }
}

} // namespace

VulkanUiResources::~VulkanUiResources()
{
    destroy();
}

void VulkanUiResources::create(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const FontAtlas& font,
    const ImageData& titleBackground)
{
    destroy();
    if (font.width() == 0 || font.height() == 0 || font.pixels().empty()) {
        throw std::runtime_error("UI font atlas contains no pixels");
    }
    if (titleBackground.width == 0 ||
        titleBackground.height == 0 ||
        titleBackground.rgba.empty()) {
        throw std::runtime_error("Title background contains no pixels");
    }
    device_ = device;
    allocator_ = &allocator;
    try {
        fontImage_ = uploadImage(
            allocator,
            device_,
            commandPool,
            graphicsQueue,
            font.width(),
            font.height(),
            VK_FORMAT_R8_UNORM,
            font.pixels());
        titleBackgroundImage_ = uploadImage(
            allocator,
            device_,
            commandPool,
            graphicsQueue,
            titleBackground.width,
            titleBackground.height,
            VK_FORMAT_R8G8B8A8_SRGB,
            titleBackground.rgba);

        const VkSamplerCreateInfo samplerInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 0.0f,
        };
        vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_),
            "vkCreateSampler UI images failed");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanUiResources::destroy()
{
    if (device_) {
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
        vulkanResources::destroyImage(
            *allocator_, device_, titleBackgroundImage_);
        vulkanResources::destroyImage(*allocator_, device_, fontImage_);
    }
    sampler_ = VK_NULL_HANDLE;
    titleBackgroundImage_ = {};
    fontImage_ = {};
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

} // namespace sokoban
