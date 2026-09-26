#include "engine/render/VulkanAtmospherePass.hpp"

#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"

namespace sokoban {

VulkanAtmospherePass::~VulkanAtmospherePass()
{
    destroy();
}

void VulkanAtmospherePass::create(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkExtent2D renderExtent,
    VkFormat format)
{
    destroy();
    device_ = device;
    allocator_ = &allocator;
    format_ = format;
    extent_ = {
        .width = (renderExtent.width + 1U) / 2U,
        .height = (renderExtent.height + 1U) / 2U,
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
        image_ = vulkanResources::createImage(
            allocator,
            device_,
            imageInfo,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "Half-resolution atmosphere integration");

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
            "vkCreateSampler atmosphere failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_SAMPLER, sampler_,
            "Atmosphere integration sampler");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanAtmospherePass::destroy()
{
    if (device_) {
        vulkanResources::destroyImage(*allocator_, device_, image_);
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
    }
    image_ = {};
    sampler_ = VK_NULL_HANDLE;
    extent_ = {};
    format_ = VK_FORMAT_UNDEFINED;
    readable_ = false;
    allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
}

void VulkanAtmospherePass::prepareTarget(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    vulkanResources::transitionImage(
        commandBuffer,
        image_.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        readable_
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

void VulkanAtmospherePass::publishTarget(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    vulkanResources::transitionImage(
        commandBuffer,
        image_.image,
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
    readable_ = true;
    ++stats.imageBarriers;
}

bool VulkanAtmospherePass::valid() const
{
    return image_.image && image_.view && sampler_ &&
        extent_.width > 0 && extent_.height > 0;
}

} // namespace sokoban
