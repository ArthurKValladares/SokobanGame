#include "engine/render/VulkanSsaoPass.hpp"

#include "engine/render/LightingConfig.hpp"
#include "engine/render/SsaoMath.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanGpuProfiler.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <array>

namespace sokoban {
namespace {

std::array<Vec4, 4> matrixColumns(const Mat4& matrix)
{
    return {
        Vec4 { at(matrix, 0, 0), at(matrix, 1, 0),
            at(matrix, 2, 0), at(matrix, 3, 0) },
        Vec4 { at(matrix, 0, 1), at(matrix, 1, 1),
            at(matrix, 2, 1), at(matrix, 3, 1) },
        Vec4 { at(matrix, 0, 2), at(matrix, 1, 2),
            at(matrix, 2, 2), at(matrix, 3, 2) },
        Vec4 { at(matrix, 0, 3), at(matrix, 1, 3),
            at(matrix, 2, 3), at(matrix, 3, 3) },
    };
}

} // namespace

VulkanSsaoPass::~VulkanSsaoPass()
{
    destroy();
}

void VulkanSsaoPass::create(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkExtent2D extent)
{
    destroy();
    device_ = device;
    allocator_ = &allocator;
    renderExtent_ = extent;
    const PixelExtent half = ssaoBufferExtent({ extent.width, extent.height });
    aoExtent_ = { half.width, half.height };

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
    renderExtent_ = extent;
    const PixelExtent half = ssaoBufferExtent({ extent.width, extent.height });
    aoExtent_ = { half.width, half.height };
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
    renderExtent_ = {};
    aoExtent_ = {};
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

// The second of the pass's two draws: the estimated occlusion composited over
// the scene colour, full resolution, covering every pixel.
//
// Split from record() because the two draws share only the push-constant block
// and the descriptor set - the estimator works at half resolution into its own
// attachment and this one does not - and record() was 206 lines of the two
// interleaved.
void VulkanSsaoPass::recordAmbientComposite(
    VkCommandBuffer commandBuffer,
    VkImageView targetView,
    VkDescriptorSet descriptorSet,
    VkPipelineLayout pipelineLayout,
    Pipelines pipelines,
    VulkanGpuProfiler& gpuProfiler,
    uint32_t frameIndex,
    RenderStats& stats,
    const GpuDrawInstance& pushConstants) const
{
    gpuProfiler.beginPhase(
        commandBuffer, frameIndex, VulkanGpuPhase::SsaoComposite);
    VkViewport compositeViewport {
        .x = 0.0f,
        .y = static_cast<float>(renderExtent_.height),
        .width = static_cast<float>(renderExtent_.width),
        .height = -static_cast<float>(renderExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D compositeScissor {
        .offset = { 0, 0 },
        .extent = renderExtent_,
    };

    // The composite covers every pixel and no longer blends with what is
    // already there - it samples the copy the recorder took instead - so the
    // attachment's previous contents are worth nothing to it.
    VkRenderingAttachmentInfo compositeAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = targetView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    VkRenderingInfo compositeRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = renderExtent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &compositeAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &compositeRenderingInfo);
    ++stats.renderPasses;
    vkCmdBindPipeline(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composite);
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
    vkCmdSetViewport(commandBuffer, 0, 1, &compositeViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &compositeScissor);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(GpuDrawInstance),
        &pushConstants);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    ++stats.drawCalls;
    vkCmdEndRendering(commandBuffer);
    gpuProfiler.endPhase(
        commandBuffer, frameIndex, VulkanGpuPhase::SsaoComposite);
}

void VulkanSsaoPass::record(
    VkCommandBuffer commandBuffer,
    VkImageView targetView,
    const RenderFrameData::Lighting::AmbientOcclusion& settings,
    const Mat4& clipFromView,
    VkDescriptorSet descriptorSet,
    VkPipelineLayout pipelineLayout,
    Pipelines pipelines,
    VulkanGpuProfiler& gpuProfiler,
    uint32_t frameIndex,
    RenderStats& stats) const
{
    if (!samplesSceneDepth(settings) ||
        !valid() ||
        !targetView ||
        !descriptorSet ||
        !pipelineLayout ||
        !pipelines.occlusion ||
        !pipelines.composite) {
        // Every profiler query must be written each submitted frame or the
        // whole frame's non-blocking query read remains unavailable.
        gpuProfiler.beginPhase(
            commandBuffer, frameIndex, VulkanGpuPhase::SsaoOcclusion);
        gpuProfiler.endPhase(
            commandBuffer, frameIndex, VulkanGpuPhase::SsaoOcclusion);
        gpuProfiler.beginPhase(
            commandBuffer, frameIndex, VulkanGpuPhase::SsaoComposite);
        gpuProfiler.endPhase(
            commandBuffer, frameIndex, VulkanGpuPhase::SsaoComposite);
        return;
    }

    gpuProfiler.beginPhase(
        commandBuffer, frameIndex, VulkanGpuPhase::SsaoOcclusion);
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

    VkViewport aoViewport {
        .x = 0.0f,
        .y = static_cast<float>(aoExtent_.height),
        .width = static_cast<float>(aoExtent_.width),
        .height = -static_cast<float>(aoExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D aoScissor { .offset = { 0, 0 }, .extent = aoExtent_ };
    using Debug = RenderFrameData::Lighting::AmbientOcclusion::Debug;
    // Both draws read the same block. The estimator uses the half-resolution
    // extent to map its fragment coordinates over the full depth image. The
    // composite reuses the inverse projection and bilateral thresholds.
    const float debugMode = settings.debug == Debug::AmbientMask
        ? 2.0f
        : (settings.debug == Debug::Occlusion ? 1.0f : 0.0f);
    GpuDrawInstance pushConstants {};
    pushConstants.vertices = matrixColumns(inverse(clipFromView));
    pushConstants.passData = matrixColumns(clipFromView);
    pushConstants.color = {
        settings.strength,
        config::ssaoRadiusWorld,
        config::ssaoBiasWorld,
        debugMode,
    };
    pushConstants.normalAndAmbientRed = {
        static_cast<float>(aoExtent_.width),
        static_cast<float>(aoExtent_.height),
        config::ssaoBilateralDepthSigmaWorld,
        config::ssaoBilateralNormalThreshold,
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
        .renderArea = { .offset = { 0, 0 }, .extent = aoExtent_ },
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
    vkCmdSetViewport(commandBuffer, 0, 1, &aoViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &aoScissor);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(GpuDrawInstance),
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
    gpuProfiler.endPhase(
        commandBuffer, frameIndex, VulkanGpuPhase::SsaoOcclusion);

    recordAmbientComposite(
        commandBuffer,
        targetView,
        descriptorSet,
        pipelineLayout,
        pipelines,
        gpuProfiler,
        frameIndex,
        stats,
        pushConstants);
}

bool VulkanSsaoPass::valid() const
{
    return image_.image && image_.view && sampler_ &&
        renderExtent_.width > 0 && renderExtent_.height > 0 &&
        aoExtent_.width > 0 && aoExtent_.height > 0;
}

void VulkanSsaoPass::createImage()
{
    if (!device_ || aoExtent_.width == 0 || aoExtent_.height == 0) {
        return;
    }
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = {
            .width = aoExtent_.width,
            .height = aoExtent_.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    image_ = vulkanResources::createImage(
        *allocator_,
        device_,
        imageInfo,
        VK_IMAGE_ASPECT_COLOR_BIT,
        "Half-resolution SSAO buffer");
}

void VulkanSsaoPass::destroyImage()
{
    if (device_) {
        vulkanResources::destroyImage(*allocator_, device_, image_);
    }
}

} // namespace sokoban
