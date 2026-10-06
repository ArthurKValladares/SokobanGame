#include "engine/render/VulkanUiResources.hpp"

#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include "engine/ui/FontAtlas.hpp"

#include <cstring>
#include <algorithm>
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
    font_ = &font;
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
        std::vector<std::byte> emptyCurves;
        if (font.curvePixels().empty()) {
            emptyCurves.resize(std::size_t(FontAtlas::curveAtlasSize) * FontAtlas::curveAtlasSize * 8);
        }
        curveImage_ = uploadImage(allocator, device_, commandPool, graphicsQueue,
            FontAtlas::curveAtlasSize, FontAtlas::curveAtlasSize, VK_FORMAT_R16G16B16A16_SINT,
            emptyCurves.empty() ? std::span<const std::byte>(font.curvePixels()) : std::span<const std::byte>(emptyCurves));
        fontRevision_ = font.updatesSince(0).revision;
        curveRevision_ = font.updatesSince(0, true).revision;
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
        VkSamplerCreateInfo curveSamplerInfo = samplerInfo;
        curveSamplerInfo.magFilter = VK_FILTER_NEAREST;
        curveSamplerInfo.minFilter = VK_FILTER_NEAREST;
        vkCheck(vkCreateSampler(device_, &curveSamplerInfo, nullptr, &curveSampler_),
            "vkCreateSampler UI curves failed");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanUiResources::recordFontUpdates(VkCommandBuffer commandBuffer, uint32_t frameIndex)
{
    const FontAtlasUpdate coverage = font_->updatesSince(fontRevision_);
    const FontAtlasUpdate curves = font_->updatesSince(curveRevision_, true);
    const VkDeviceSize coverageBytes = VkDeviceSize(coverage.width) * coverage.height;
    const VkDeviceSize curveStart = (coverageBytes + 7) & ~VkDeviceSize(7);
    const VkDeviceSize totalBytes = curveStart + VkDeviceSize(curves.width) * curves.height * 8;
    if (!totalBytes) return;
    Upload& upload = uploads_.at(frameIndex);
    if (upload.capacity < totalBytes) {
        allocator_->destroyBuffer(upload.buffer, upload.allocation);
        upload = {};
        const VkBufferCreateInfo info { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = totalBytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        allocator_->createBuffer(info, VulkanMemoryUsage::HostSequentialWrite, upload.buffer,
            upload.allocation, &upload.mapped, "UI glyph updates");
        upload.capacity = totalBytes;
    }
    const auto copy = [&](const FontAtlasUpdate& rect, const std::vector<std::byte>& pixels,
                          uint32_t side, uint32_t stride, VkDeviceSize offset, VkImage image) {
        if (!rect.width || !rect.height) return;
        auto* destination = static_cast<std::byte*>(upload.mapped) + offset;
        for (uint32_t row = 0; row < rect.height; ++row) {
            std::memcpy(destination + std::size_t(row) * rect.width * stride,
                pixels.data() + (std::size_t(rect.y + row) * side + rect.x) * stride,
                std::size_t(rect.width) * stride);
        }
        const auto range = vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
        const vulkanResources::ImageState sampled { VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        const vulkanResources::ImageState transferred { VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
        vulkanResources::transitionImage(commandBuffer, image, range, sampled, transferred);
        const VkBufferImageCopy region { .bufferOffset = offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { static_cast<int32_t>(rect.x), static_cast<int32_t>(rect.y), 0 },
            .imageExtent = { rect.width, rect.height, 1 } };
        vkCmdCopyBufferToImage(commandBuffer, upload.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        vulkanResources::transitionImage(commandBuffer, image, range, transferred, sampled);
    };
    copy(coverage, font_->pixels(), font_->width(), 1, 0, fontImage_.image);
    copy(curves, font_->curvePixels(), FontAtlas::curveAtlasSize, 8, curveStart, curveImage_.image);
    // HostSequentialWrite allocations require HOST_COHERENT memory.
    fontRevision_ = coverage.revision;
    curveRevision_ = curves.revision;
}

void VulkanUiResources::destroy()
{
    if (device_) {
        for (Upload& upload : uploads_) {
            allocator_->destroyBuffer(upload.buffer, upload.allocation);
            upload = {};
        }
        if (curveSampler_) vkDestroySampler(device_, curveSampler_, nullptr);
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
        vulkanResources::destroyImage(
            *allocator_, device_, titleBackgroundImage_);
        vulkanResources::destroyImage(*allocator_, device_, fontImage_);
        vulkanResources::destroyImage(*allocator_, device_, curveImage_);
    }
    sampler_ = VK_NULL_HANDLE;
    curveSampler_ = VK_NULL_HANDLE;
    curveImage_ = {};
    font_ = nullptr;
    fontRevision_ = 0; curveRevision_ = 0;
    titleBackgroundImage_ = {};
    fontImage_ = {};
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

} // namespace sokoban
