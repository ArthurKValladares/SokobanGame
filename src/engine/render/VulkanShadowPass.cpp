#include "engine/render/VulkanShadowPass.hpp"

#include "engine/render/LightingConfig.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <stdexcept>

namespace sokoban {
VulkanShadowPass::~VulkanShadowPass()
{
    destroy();
}

void VulkanShadowPass::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkFormat format)
{
    destroy();
    device_ = device;
    format_ = format;

    try {
        VkImageCreateInfo imageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format_,
            .extent = {
                .width = config::shadowMapSize,
                .height = config::shadowMapSize,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image_ = vulkanResources::createImage(
            physicalDevice, device_, imageInfo, VK_IMAGE_ASPECT_DEPTH_BIT);

        VkSamplerCreateInfo samplerInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .compareEnable = VK_FALSE,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        };
        vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_),
            "vkCreateSampler shadow map failed");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanShadowPass::destroy()
{
    if (device_) {
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
        vulkanResources::destroyImage(device_, image_);
    }
    sampler_ = VK_NULL_HANDLE;
    imageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    format_ = VK_FORMAT_UNDEFINED;
    device_ = VK_NULL_HANDLE;
}

void VulkanShadowPass::begin(
    VkCommandBuffer commandBuffer,
    VkPipeline tilePipeline,
    RenderStats& stats)
{
    if (!valid() || !tilePipeline) {
        throw std::runtime_error("Shadow pass resources and pipelines must exist before recording");
    }

    vulkanResources::transitionImage(
        commandBuffer,
        image_.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
        {
            imageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_PIPELINE_STAGE_2_NONE
                : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            imageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_ACCESS_2_NONE
                : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            imageLayout_,
        },
        {
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        });
    imageLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    ++stats.imageBarriers;

    VkRenderingAttachmentInfo depthAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = image_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
    };
    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = { config::shadowMapSize, config::shadowMapSize },
        },
        .layerCount = 1,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++stats.renderPasses;

    VkViewport viewport {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(config::shadowMapSize),
        .height = static_cast<float>(config::shadowMapSize),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor {
        .offset = { 0, 0 },
        .extent = { config::shadowMapSize, config::shadowMapSize },
    };
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tilePipeline);
    ++stats.pipelineBinds;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthWriteEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);
}

void VulkanShadowPass::bindModelPipeline(
    VkCommandBuffer commandBuffer,
    VkPipeline modelPipeline,
    RenderStats& stats) const
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline);
    ++stats.pipelineBinds;
}

void VulkanShadowPass::end(VkCommandBuffer commandBuffer, RenderStats& stats)
{
    vkCmdEndRendering(commandBuffer);

    vulkanResources::transitionImage(
        commandBuffer,
        image_.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
        {
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        },
        {
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        });
    imageLayout_ = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    ++stats.imageBarriers;
}

bool VulkanShadowPass::valid() const
{
    return image_.image && image_.view && sampler_;
}

} // namespace sokoban
