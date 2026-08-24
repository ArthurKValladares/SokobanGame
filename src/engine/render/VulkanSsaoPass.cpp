#include "engine/render/VulkanSsaoPass.hpp"

#include "engine/render/LightingConfig.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <array>

namespace sokoban {
VulkanSsaoPass::~VulkanSsaoPass()
{
    destroy();
}

void VulkanSsaoPass::create(VkPhysicalDevice physicalDevice, VkDevice device, VkExtent2D extent)
{
    destroy();
    physicalDevice_ = physicalDevice;
    device_ = device;
    extent_ = extent;

    try {
        createImage();
        VkSamplerCreateInfo samplerInfo {
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
        vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_),
            "vkCreateSampler ssao failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_SAMPLER, sampler_, "SSAO sampler");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanSsaoPass::recreate(VkExtent2D extent)
{
    extent_ = extent;
    destroyImage();
    createImage();
}

void VulkanSsaoPass::destroy()
{
    if (device_) {
        destroyImage();
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
    }
    sampler_ = VK_NULL_HANDLE;
    extent_ = {};
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
}

void VulkanSsaoPass::record(
    VkCommandBuffer commandBuffer,
    VkImageView targetView,
    const RenderFrameData::Lighting::AmbientOcclusion& settings,
    VkDescriptorSet descriptorSet,
    VkPipelineLayout pipelineLayout,
    Pipelines pipelines,
    RenderStats& stats) const
{
    if (!settings.enabled ||
        (settings.strength <= 0.0f && !settings.visualize) ||
        !valid() ||
        !targetView ||
        !descriptorSet ||
        !pipelineLayout ||
        !pipelines.occlusion ||
        !pipelines.composite ||
        !pipelines.visualize) {
        return;
    }

    const std::array<VkImageMemoryBarrier2, 1> beforeBarriers {
        vulkanResources::imageBarrier(
            image_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_NONE,
                VK_IMAGE_LAYOUT_UNDEFINED,
            },
            {
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            }),
    };
    vulkanResources::transitionImages(commandBuffer, beforeBarriers);
    ++stats.imageBarriers;

    VkViewport viewport {
        .x = 0.0f,
        .y = static_cast<float>(extent_.height),
        .width = static_cast<float>(extent_.width),
        .height = -static_cast<float>(extent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor { .offset = { 0, 0 }, .extent = extent_ };
    TilePushConstants pushConstants {};
    pushConstants.color = {
        settings.strength,
        config::ssaoRadiusPixels,
        config::ssaoDepthRange,
        settings.visualize ? 1.0f : 0.0f,
    };

    VkRenderingAttachmentInfo ssaoAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = image_.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { { 1.0f, 1.0f, 1.0f, 1.0f } } },
    };
    VkRenderingInfo ssaoRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = extent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &ssaoAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &ssaoRenderingInfo);
    ++stats.renderPasses;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.occlusion);
    ++stats.pipelineBinds;
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    ++stats.drawCalls;
    vkCmdEndRendering(commandBuffer);

    const std::array<VkImageMemoryBarrier2, 1> afterBarriers {
        vulkanResources::imageBarrier(
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
            }),
    };
    vulkanResources::transitionImages(commandBuffer, afterBarriers);
    ++stats.imageBarriers;

    VkRenderingAttachmentInfo compositeAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = targetView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    VkRenderingInfo compositeRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = extent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &compositeAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &compositeRenderingInfo);
    ++stats.renderPasses;
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        settings.visualize ? pipelines.visualize : pipelines.composite);
    ++stats.pipelineBinds;
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    ++stats.drawCalls;
    vkCmdEndRendering(commandBuffer);
}

bool VulkanSsaoPass::valid() const
{
    return image_.image && image_.view && sampler_ && extent_.width > 0 && extent_.height > 0;
}

void VulkanSsaoPass::createImage()
{
    if (!device_ || extent_.width == 0 || extent_.height == 0) {
        return;
    }
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { .width = extent_.width, .height = extent_.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    image_ = vulkanResources::createImage(
        physicalDevice_,
        device_,
        imageInfo,
        VK_IMAGE_ASPECT_COLOR_BIT,
        "SSAO buffer");
}

void VulkanSsaoPass::destroyImage()
{
    if (device_) {
        vulkanResources::destroyImage(device_, image_);
    }
}

} // namespace sokoban
