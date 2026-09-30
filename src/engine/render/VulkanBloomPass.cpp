#include "engine/render/VulkanBloomPass.hpp"

#include "engine/render/BloomConfig.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"

#include <algorithm>

namespace sokoban {

VulkanBloomPass::~VulkanBloomPass()
{
    destroy();
}

void VulkanBloomPass::create(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkExtent2D renderExtent,
    VkFormat format)
{
    destroy();
    device_ = device;
    allocator_ = &allocator;
    format_ = format;
    constexpr uint32_t divisor = config::bloomResolutionDivisor;
    static_assert(divisor > 0);
    extent_ = {
        .width = std::max(1U, (renderExtent.width + divisor - 1U) / divisor),
        .height = std::max(1U, (renderExtent.height + divisor - 1U) / divisor),
    };

    try {
        const VkImageCreateInfo imageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format_,
            .extent = {
                .width = extent_.width,
                .height = extent_.height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        extractImage_ = vulkanResources::createImage(
            allocator,
            device_,
            imageInfo,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "Bloom extract and horizontal blur");
        bloomImage_ = vulkanResources::createImage(
            allocator,
            device_,
            imageInfo,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "Bloom vertical blur");

        const VkSamplerCreateInfo samplerInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .anisotropyEnable = VK_FALSE,
            .compareEnable = VK_FALSE,
            .minLod = 0.0f,
            .maxLod = 0.0f,
        };
        vkCheck(
            vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_),
            "vkCreateSampler bloom failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_SAMPLER, sampler_, "Bloom sampler");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanBloomPass::destroy()
{
    if (device_) {
        vulkanResources::destroyImage(*allocator_, device_, bloomImage_);
        vulkanResources::destroyImage(*allocator_, device_, extractImage_);
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
    }
    extractImage_ = {};
    bloomImage_ = {};
    sampler_ = VK_NULL_HANDLE;
    extent_ = {};
    format_ = VK_FORMAT_UNDEFINED;
    extractReadable_ = false;
    bloomReadable_ = false;
    allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
}

void VulkanBloomPass::prepareTarget(
    VkCommandBuffer commandBuffer,
    vulkanResources::OwnedImage& image,
    bool readable,
    RenderStats& stats)
{
    vulkanResources::transitionImage(
        commandBuffer,
        image.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        readable
            ? vulkanResources::ImageState {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }
            : vulkanResources::ImageState {},
        {
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        });
    ++stats.imageBarriers;
}

void VulkanBloomPass::publishTarget(
    VkCommandBuffer commandBuffer,
    vulkanResources::OwnedImage& image,
    RenderStats& stats)
{
    vulkanResources::transitionImage(
        commandBuffer,
        image.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        {
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
    ++stats.imageBarriers;
}

void VulkanBloomPass::prepareExtractTarget(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    prepareTarget(commandBuffer, extractImage_, extractReadable_, stats);
}

void VulkanBloomPass::publishExtractTarget(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    publishTarget(commandBuffer, extractImage_, stats);
    extractReadable_ = true;
}

void VulkanBloomPass::prepareBlurTarget(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    prepareTarget(commandBuffer, bloomImage_, bloomReadable_, stats);
}

void VulkanBloomPass::publishBlurTarget(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    publishTarget(commandBuffer, bloomImage_, stats);
    bloomReadable_ = true;
}

bool VulkanBloomPass::valid() const
{
    return extractImage_.image && extractImage_.view && bloomImage_.image &&
        bloomImage_.view && sampler_ && extent_.width > 0 && extent_.height > 0;
}

} // namespace sokoban
