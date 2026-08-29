#include "engine/render/VulkanShadowPass.hpp"

#include "engine/render/LightingConfig.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <stdexcept>

namespace sokoban {
VulkanShadowPass::~VulkanShadowPass()
{
    destroy();
}

void VulkanShadowPass::create(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkFormat format)
{
    destroy();
    device_ = device;
    allocator_ = &allocator;
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
            allocator,
            device_,
            imageInfo,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            "Directional shadow map");

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
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_SAMPLER, sampler_, "Directional shadow sampler");

        constexpr uint32_t pointLayerCount =
            static_cast<uint32_t>(RenderFrameData::pointLightCapacity * 6);
        const VkImageCreateInfo pointImageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format_,
            .extent = {
                .width = config::pointShadowMapSize,
                .height = config::pointShadowMapSize,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = pointLayerCount,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        allocator.createDeviceImage(
            pointImageInfo,
            pointImage_.image,
            pointImage_.allocation,
            "Point shadow cube array");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_IMAGE, pointImage_.image,
            "Point shadow cube array");

        VkImageViewCreateInfo pointViewInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = pointImage_.image,
            .viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
            .format = format_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = pointLayerCount,
            },
        };
        vkCheck(vkCreateImageView(
                    device_, &pointViewInfo, nullptr, &pointImage_.view),
            "vkCreateImageView point shadow cube array failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_IMAGE_VIEW, pointImage_.view,
            "Point shadow cube array view");
        pointViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        pointViewInfo.subresourceRange.layerCount = 1;
        for (uint32_t layer = 0; layer < pointLayerCount; ++layer) {
            pointViewInfo.subresourceRange.baseArrayLayer = layer;
            vkCheck(vkCreateImageView(
                        device_, &pointViewInfo, nullptr,
                        &pointLayerViews_[layer]),
                "vkCreateImageView point shadow face failed");
        }
        vkCheck(vkCreateSampler(
                    device_, &samplerInfo, nullptr, &pointSampler_),
            "vkCreateSampler point shadow map failed");
        vulkanDebug::setObjectName(
            device_, VK_OBJECT_TYPE_SAMPLER, pointSampler_, "Point shadow sampler");
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanShadowPass::destroy()
{
    if (device_) {
        if (pointSampler_) {
            vkDestroySampler(device_, pointSampler_, nullptr);
        }
        for (VkImageView& view : pointLayerViews_) {
            if (view) {
                vkDestroyImageView(device_, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }
        vulkanResources::destroyImage(*allocator_, device_, pointImage_);
        if (sampler_) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
        vulkanResources::destroyImage(*allocator_, device_, image_);
    }
    sampler_ = VK_NULL_HANDLE;
    pointSampler_ = VK_NULL_HANDLE;
    imageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    pointImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    format_ = VK_FORMAT_UNDEFINED;
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
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

void VulkanShadowPass::beginPointFace(
    VkCommandBuffer commandBuffer,
    uint32_t lightIndex,
    uint32_t cubeFace,
    VkPipeline tilePipeline,
    RenderStats& stats)
{
    if (!valid() || !tilePipeline ||
        lightIndex >= RenderFrameData::pointLightCapacity || cubeFace >= 6) {
        throw std::runtime_error(
            "Point-shadow resources, face, and pipeline must be valid");
    }
    constexpr uint32_t pointLayerCount =
        static_cast<uint32_t>(RenderFrameData::pointLightCapacity * 6);
    if (pointImageLayout_ != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
        const VkImageSubresourceRange range {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = pointLayerCount,
        };
        vulkanResources::transitionImage(
            commandBuffer,
            pointImage_.image,
            range,
            {
                pointImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_2_NONE
                    : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                pointImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_ACCESS_2_NONE
                    : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                pointImageLayout_,
            },
            {
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            });
        pointImageLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        ++stats.imageBarriers;
    }

    const uint32_t layer = lightIndex * 6 + cubeFace;
    const VkRenderingAttachmentInfo depthAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = pointLayerViews_[layer],
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
    };
    const VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = {
                config::pointShadowMapSize,
                config::pointShadowMapSize,
            },
        },
        .layerCount = 1,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++stats.renderPasses;

    const VkViewport viewport {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(config::pointShadowMapSize),
        .height = static_cast<float>(config::pointShadowMapSize),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor {
        .offset = { 0, 0 },
        .extent = {
            config::pointShadowMapSize,
            config::pointShadowMapSize,
        },
    };
    vkCmdBindPipeline(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tilePipeline);
    ++stats.pipelineBinds;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(
        commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthWriteEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);
}

void VulkanShadowPass::endPointFace(VkCommandBuffer commandBuffer) const
{
    vkCmdEndRendering(commandBuffer);
}

void VulkanShadowPass::finishPointShadows(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    if (pointImageLayout_ == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL) {
        return;
    }
    if (pointImageLayout_ != VK_IMAGE_LAYOUT_UNDEFINED &&
        pointImageLayout_ != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
        throw std::runtime_error(
            "Point-shadow array has an unexpected image layout");
    }
    const VkImageSubresourceRange range {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = static_cast<uint32_t>(
            RenderFrameData::pointLightCapacity * 6),
    };
    vulkanResources::transitionImage(
        commandBuffer,
        pointImage_.image,
        range,
        {
            pointImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_PIPELINE_STAGE_2_NONE
                : VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            pointImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                ? VK_ACCESS_2_NONE
                : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            pointImageLayout_,
        },
        {
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        });
    pointImageLayout_ = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    ++stats.imageBarriers;
}

bool VulkanShadowPass::valid() const
{
    return image_.image && image_.view && sampler_ &&
        pointImage_.image && pointImage_.view && pointSampler_;
}

} // namespace sokoban
