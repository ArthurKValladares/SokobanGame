#include "engine/render/VulkanSceneRecorder.hpp"

#include "engine/Config.hpp"
#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanPipelineFactory.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanSceneDescriptors.hpp"
#include "engine/render/VulkanShadowPass.hpp"
#include "engine/render/VulkanSsaoPass.hpp"
#include "engine/render/VulkanSwapchainResources.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(message) + " (VkResult " +
            std::to_string(result) + ")");
    }
}

Vec4 subtract(Vec4 left, Vec4 right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
        left.w - right.w,
    };
}

std::array<Vec4, 4> affineTransformColumns(
    Vec4 origin,
    Vec4 xPoint,
    Vec4 yPoint,
    Vec4 zPoint)
{
    return {
        subtract(xPoint, origin),
        subtract(yPoint, origin),
        subtract(zPoint, origin),
        origin,
    };
}

struct ModelTransformPoints {
    Vec3 origin {};
    Vec3 xPoint {};
    Vec3 yPoint {};
    Vec3 zPoint {};
};

ModelTransformPoints modelTransformPoints(
    const RenderFrameData::Tile& tile)
{
    const float x = tile.position.x;
    const float y = tile.position.y;
    const float z = tile.baseElevation;
    const float width = tile.size.x;
    const float depth = tile.size.y;
    const float height = std::max(tile.height, 0.0f);

    ModelTransformPoints result;
    switch (tile.modelRotationQuarterTurns % 4) {
    case 0:
        result.origin = { x, y, z };
        result.xPoint = { x + width, y, z };
        result.yPoint = { x, y + depth, z };
        break;
    case 1:
        result.origin = { x + width, y, z };
        result.xPoint = { x + width, y + depth, z };
        result.yPoint = { x, y, z };
        break;
    case 2:
        result.origin = { x + width, y + depth, z };
        result.xPoint = { x, y + depth, z };
        result.yPoint = { x + width, y, z };
        break;
    case 3:
        result.origin = { x, y + depth, z };
        result.xPoint = { x, y, z };
        result.yPoint = { x + width, y + depth, z };
        break;
    }
    result.zPoint = {
        result.origin.x,
        result.origin.y,
        z + height,
    };
    return result;
}

void renderDebugUi(VkCommandBuffer commandBuffer)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(), commandBuffer);
#else
    (void)commandBuffer;
#endif
}

class SceneRecordingSession {
public:
    SceneRecordingSession(
        VulkanSceneRecorder::Resources resources,
        const VulkanSceneRecorder::FrameConfiguration& configuration)
        : swapchain_(resources.swapchain)
        , shadowPass_(resources.shadowPass)
        , ssaoPass_(resources.ssaoPass)
        , descriptors_(resources.sceneDescriptors)
        , pipelines_(resources.pipelines)
        , models_(resources.modelResources)
        , configuration_(configuration)
    {
    }

    RenderStats record(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        const UiDrawData& uiDrawData)
    {
        const VkExtent2D extent = swapchain_.extent();
        const VkExtent2D renderExtent = swapchain_.renderExtent();
        stats_ = {
            .frameIndex = configuration_.statsFrameIndex,
            .totalTiles = static_cast<uint32_t>(
                frameData.tiles.size() + frameData.waterSurfaces.size()),
            .scenePreparations = 1,
            .preparedIsoFaces =
                static_cast<uint32_t>(scene.isoFaces.size()),
            .preparedShadowFaces =
                static_cast<uint32_t>(scene.shadowFaces.size()),
            .preparedModels = static_cast<uint32_t>(
                scene.opaqueModelIndices.size() +
                scene.translucentModelIndices.size()),
            .swapchainWidth = extent.width,
            .swapchainHeight = extent.height,
            .swapchainImages = swapchain_.imageCount(),
            .renderWidth = renderExtent.width,
            .renderHeight = renderExtent.height,
            .renderScalePercent = static_cast<uint32_t>(
                swapchain_.renderScalePercent()),
            .activeSamples = configuration_.activeSamples,
            .wireframeEnabled =
                configuration_.wireframeEnabled,
            .wireframeLineWidth =
                configuration_.wireframeLineWidth,
            .pipelineRebuilds =
                configuration_.pipelineRebuilds,
            .swapchainRecreations =
                configuration_.swapchainRecreations,
            .swapchainRecreationDeferrals =
                configuration_.swapchainRecreationDeferrals,
            .renderResourceReconfigurations =
                configuration_.renderResourceReconfigurations,
            .presentQueueRetirementWaits =
                configuration_.presentQueueRetirementWaits,
            .retiredRenderResourceSets =
                configuration_.retiredRenderResourceSets,
            .rendererReconfigurationPending =
                configuration_.rendererReconfigurationPending,
        };

        const VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkCheck(
            vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer failed");
        swapchain_.beginFrame(commandBuffer, imageIndex, stats_);
        recordGameRendering(
            commandBuffer,
            swapchain_.renderColorView(),
            swapchain_.resolveColorView(),
            frameData,
            scene);
        ssaoPass_.record(
            commandBuffer,
            swapchain_.resolvedColorView(),
            swapchain_.depthSourceImage(),
            frameData.lighting.ambientOcclusion,
            descriptorSet(),
            pipelines_.layout(),
            {
                .occlusion = pipelines_.ssao(),
                .composite = pipelines_.ssaoComposite(),
                .visualize = pipelines_.ssaoVisualize(),
            },
            stats_);
        swapchain_.upscaleSceneToSwapchain(
            commandBuffer, imageIndex, stats_);
        recordOverlayRendering(
            commandBuffer,
            swapchain_.image(imageIndex),
            swapchain_.imageView(imageIndex),
            uiDrawData);
        swapchain_.endFrame(commandBuffer, imageIndex, stats_);
        vkCheck(
            vkEndCommandBuffer(commandBuffer),
            "vkEndCommandBuffer failed");
        return stats_;
    }

private:
    VkDescriptorSet descriptorSet() const
    {
        return descriptors_.set(
            configuration_.descriptorFrameIndex);
    }

    void bindDescriptorSet(VkCommandBuffer commandBuffer) const
    {
        const VkDescriptorSet set = descriptorSet();
        if (set) {
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines_.layout(),
                0,
                1,
                &set,
                0,
                nullptr);
        }
    }

    void recordShadowMapRendering(
        VkCommandBuffer commandBuffer,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene)
    {
        shadowPass_.begin(
            commandBuffer, pipelines_.shadow(), stats_);
        for (const std::array<Vec4, 4>& face : scene.shadowFaces) {
            drawShadowFace(commandBuffer, face);
        }
        bool modelPipelineBound = false;
        for (std::size_t tileIndex : scene.shadowModelIndices) {
            if (!modelPipelineBound) {
                shadowPass_.bindModelPipeline(
                    commandBuffer,
                    pipelines_.modelShadow(),
                    stats_);
                modelPipelineBound = true;
            }
            drawModelShadow(
                commandBuffer,
                scene.shadowLayout,
                frameData.tiles[tileIndex]);
        }
        shadowPass_.end(commandBuffer, stats_);
    }

    void recordGameRendering(
        VkCommandBuffer commandBuffer,
        VkImageView colorView,
        VkImageView resolveView,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene)
    {
        if (shadowPass_.valid() && pipelines_.shadow()) {
            recordShadowMapRendering(
                commandBuffer, frameData, scene);
        }
        swapchain_.ensureSceneColorReadable(
            commandBuffer, stats_);
        recordScenePass(
            commandBuffer,
            colorView,
            resolveView,
            frameData,
            scene,
            false,
            false,
            scene.hasTranslucentContent || !resolveView,
            false,
            true);
        if (!scene.hasTranslucentContent) {
            return;
        }
        swapchain_.copyResolvedSceneColor(
            commandBuffer, stats_);
        recordScenePass(
            commandBuffer,
            colorView,
            resolveView,
            frameData,
            scene,
            true,
            true,
            !resolveView,
            true,
            false);
    }

    void recordScenePass(
        VkCommandBuffer commandBuffer,
        VkImageView colorView,
        VkImageView resolveView,
        const RenderFrameData& frameData,
        const PreparedRenderScene& scene,
        bool translucentPass,
        bool loadColor,
        bool storeColor,
        bool loadDepth,
        bool writeDepth)
    {
        const VkClearValue clearValue {
            .color = { { 0.03f, 0.04f, 0.06f, 1.0f } },
        };
        const VkRenderingAttachmentInfo colorAttachment {
            .sType =
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = colorView,
            .imageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = resolveView
                ? VK_RESOLVE_MODE_AVERAGE_BIT
                : VK_RESOLVE_MODE_NONE,
            .resolveImageView = resolveView,
            .resolveImageLayout = resolveView
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = loadColor
                ? VK_ATTACHMENT_LOAD_OP_LOAD
                : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = storeColor
                ? VK_ATTACHMENT_STORE_OP_STORE
                : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = clearValue,
        };
        const VkClearValue depthClear {
            .depthStencil = { .depth = 1.0f, .stencil = 0 },
        };
        const VkRenderingAttachmentInfo depthAttachment {
            .sType =
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_.depthView(),
            .imageLayout =
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .resolveMode = swapchain_.resolveDepthView()
                ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT
                : VK_RESOLVE_MODE_NONE,
            .resolveImageView = swapchain_.resolveDepthView(),
            .resolveImageLayout =
                swapchain_.resolveDepthView()
                ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = loadDepth
                ? VK_ATTACHMENT_LOAD_OP_LOAD
                : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = depthClear,
        };
        const VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = swapchain_.renderExtent(),
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = swapchain_.depthView()
                ? &depthAttachment
                : nullptr,
        };
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ++stats_.renderPasses;

        const VkViewport viewport {
            .x = 0.0f,
            .y = static_cast<float>(
                swapchain_.renderExtent().height),
            .width = static_cast<float>(
                swapchain_.renderExtent().width),
            .height = -static_cast<float>(
                swapchain_.renderExtent().height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const VkRect2D scissor {
            .offset = { 0, 0 },
            .extent = swapchain_.renderExtent(),
        };
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines_.scene());
        ++stats_.pipelineBinds;
        bindDescriptorSet(commandBuffer);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
        vkCmdSetFrontFace(
            commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetPrimitiveTopology(
            commandBuffer,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetLineWidth(
            commandBuffer,
            configuration_.wireframeEnabled
                ? configuration_.wireframeLineWidth
                : 1.0f);
        vkCmdSetDepthTestEnable(
            commandBuffer,
            swapchain_.depthView() ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(
            commandBuffer,
            swapchain_.depthView() && writeDepth
                ? VK_TRUE
                : VK_FALSE);
        vkCmdSetDepthCompareOp(
            commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);

        if (frameData.viewMode == RenderViewMode::Isometric3D) {
            drawIsoFrame(
                commandBuffer,
                scene,
                frameData,
                translucentPass);
        } else {
            for (const RenderFrameData::Tile& tile :
                 frameData.tiles) {
                drawTile(
                    commandBuffer,
                    scene.tileLayout,
                    tile,
                    frameData.lighting);
            }
            if (!translucentPass) {
                vkCmdSetDepthWriteEnable(
                    commandBuffer, VK_FALSE);
                drawTopDownGridOverlay(
                    commandBuffer,
                    scene.tileLayout,
                    frameData);
            }
        }
        vkCmdEndRendering(commandBuffer);
    }

    void recordOverlayRendering(
        VkCommandBuffer commandBuffer,
        VkImage colorImage,
        VkImageView colorView,
        const UiDrawData& uiDrawData)
    {
        const bool hasGameUi =
            !uiDrawData.commands.empty() &&
            uiDrawData.viewportSize.x > 0.0f &&
            uiDrawData.viewportSize.y > 0.0f;
#if !SOKOBAN_ENABLE_DEBUG_UI
        if (!hasGameUi) {
            return;
        }
#endif
        const VkImageMemoryBarrier2 barrier {
            .sType =
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask =
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask =
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask =
                VK_ACCESS_2_MEMORY_READ_BIT |
                VK_ACCESS_2_MEMORY_WRITE_BIT,
            .oldLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = colorImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        const VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
        ++stats_.imageBarriers;

        const VkRenderingAttachmentInfo colorAttachment {
            .sType =
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = colorView,
            .imageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        const VkRenderingInfo renderingInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = swapchain_.extent(),
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ++stats_.renderPasses;

        if (hasGameUi) {
            const VkViewport viewport {
                .x = 0.0f,
                .y = static_cast<float>(
                    swapchain_.extent().height),
                .width = static_cast<float>(
                    swapchain_.extent().width),
                .height = -static_cast<float>(
                    swapchain_.extent().height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            const VkRect2D scissor {
                .offset = { 0, 0 },
                .extent = swapchain_.extent(),
            };
            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines_.ui());
            ++stats_.pipelineBinds;
            bindDescriptorSet(commandBuffer);
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
            vkCmdSetFrontFace(
                commandBuffer,
                VK_FRONT_FACE_COUNTER_CLOCKWISE);
            vkCmdSetPrimitiveTopology(
                commandBuffer,
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            vkCmdSetLineWidth(commandBuffer, 1.0f);
            vkCmdSetDepthTestEnable(commandBuffer, VK_FALSE);
            vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
            vkCmdSetDepthCompareOp(
                commandBuffer, VK_COMPARE_OP_ALWAYS);

            const RenderFrameData::Lighting unlit {};
            for (const UiDrawCommand& command :
                 uiDrawData.commands) {
                drawUiRect(
                    commandBuffer,
                    command,
                    uiDrawData.viewportSize,
                    unlit);
            }
        }
        renderDebugUi(commandBuffer);
        vkCmdEndRendering(commandBuffer);
    }

    void drawTile(
        VkCommandBuffer commandBuffer,
        const TileRenderLayout& layout,
        const RenderFrameData::Tile& tile,
        const RenderFrameData::Lighting& lighting)
    {
        const Vec2 origin {
            layout.boardBottomLeft.x +
                tile.position.x * layout.tileSize.x,
            layout.boardBottomLeft.y +
                tile.position.y * layout.tileSize.y,
        };
        const Vec2 size {
            layout.tileSize.x * tile.size.x,
            layout.tileSize.y * tile.size.y,
        };
        drawFace(
            commandBuffer,
            {
                Vec3 { origin.x, origin.y, 0.0f },
                Vec3 { origin.x + size.x, origin.y, 0.0f },
                Vec3 {
                    origin.x + size.x,
                    origin.y + size.y,
                    0.0f },
                Vec3 { origin.x, origin.y + size.y, 0.0f },
            },
            {},
            tile.color,
            {},
            lighting);
    }

    void drawIsoFrame(
        VkCommandBuffer commandBuffer,
        const PreparedRenderScene& scene,
        const RenderFrameData& frameData,
        bool translucentPass)
    {
        const std::vector<std::size_t>& faceIndices =
            translucentPass
            ? scene.translucentFaceIndices
            : scene.opaqueFaceIndices;
        VkPipeline boundFacePipeline = pipelines_.scene();
        for (std::size_t faceIndex : faceIndices) {
            const PreparedIsoFace& face =
                scene.isoFaces[faceIndex];
            const VkPipeline desiredPipeline =
                face.material == PreparedSurfaceMaterial::Water
                ? pipelines_.water()
                : pipelines_.scene();
            if (desiredPipeline != boundFacePipeline) {
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    desiredPipeline);
                ++stats_.pipelineBinds;
                boundFacePipeline = desiredPipeline;
            }
            if (face.material == PreparedSurfaceMaterial::Water) {
                drawWaterFace(
                    commandBuffer,
                    face.vertices,
                    face.color,
                    face.worldOrigin,
                    face.gridSize,
                    frameData.waterAnimationTimeSeconds,
                    face.isEditorPreview);
            } else {
                drawFace(
                    commandBuffer,
                    face.vertices,
                    face.shadowVertices,
                    face.color,
                    face.normal,
                    frameData.lighting,
                    face.blurBehind,
                    face.showGrid
                        ? frameData.gridOverlay.color
                        : Vec4 {},
                    face.gridSize,
                    frameData.gridOverlay.width,
                    face.isEditorPreview);
            }
        }

        bool pipelineBound = false;
        const std::vector<std::size_t>& modelIndices =
            translucentPass
            ? scene.translucentModelIndices
            : scene.opaqueModelIndices;
        for (std::size_t tileIndex : modelIndices) {
            if (!pipelineBound) {
                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelines_.model());
                ++stats_.pipelineBinds;
                pipelineBound = true;
            }
            drawModel(
                commandBuffer,
                scene.isoLayout,
                scene.shadowLayout,
                frameData.tiles[tileIndex],
                frameData.lighting);
        }
    }

    void drawTopDownGridOverlay(
        VkCommandBuffer commandBuffer,
        const TileRenderLayout& layout,
        const RenderFrameData& frameData)
    {
        if (frameData.levelWidth == 0 ||
            frameData.levelHeight == 0 ||
            frameData.gridOverlay.color.w <= 0.0f) {
            return;
        }
        const float tileWidthPixels = std::max(
            layout.tileSize.x *
                static_cast<float>(
                    swapchain_.renderExtent().width) *
                0.5f,
            0.001f);
        const float lineWidth = std::clamp(
            frameData.gridOverlay.width / tileWidthPixels,
            0.0f,
            0.5f);
        if (lineWidth <= 0.0f) {
            return;
        }

        const RenderFrameData::Lighting unlit {};
        const float levelWidth =
            static_cast<float>(frameData.levelWidth);
        const float levelHeight =
            static_cast<float>(frameData.levelHeight);
        const float halfLineWidth = lineWidth * 0.5f;
        auto drawRect = [&](float left, float bottom,
                            float right, float top) {
            if (right <= left || top <= bottom) {
                return;
            }
            const Vec2 min {
                layout.boardBottomLeft.x +
                    left * layout.tileSize.x,
                layout.boardBottomLeft.y +
                    bottom * layout.tileSize.y,
            };
            const Vec2 max {
                layout.boardBottomLeft.x +
                    right * layout.tileSize.x,
                layout.boardBottomLeft.y +
                    top * layout.tileSize.y,
            };
            drawFace(
                commandBuffer,
                {
                    Vec3 { min.x, min.y, 0.0f },
                    Vec3 { max.x, min.y, 0.0f },
                    Vec3 { max.x, max.y, 0.0f },
                    Vec3 { min.x, max.y, 0.0f },
                },
                {},
                frameData.gridOverlay.color,
                {},
                unlit);
        };
        for (uint32_t x = 0; x <= frameData.levelWidth; ++x) {
            const float center = static_cast<float>(x);
            drawRect(
                std::clamp(
                    center - halfLineWidth, 0.0f, levelWidth),
                0.0f,
                std::clamp(
                    center + halfLineWidth, 0.0f, levelWidth),
                levelHeight);
        }
        for (uint32_t y = 0; y <= frameData.levelHeight; ++y) {
            const float center = static_cast<float>(y);
            drawRect(
                0.0f,
                std::clamp(
                    center - halfLineWidth, 0.0f, levelHeight),
                levelWidth,
                std::clamp(
                    center + halfLineWidth, 0.0f, levelHeight));
        }
    }

    void drawFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        const std::array<Vec4, 4>& shadowVertices,
        Vec4 color,
        Vec3 normal,
        const RenderFrameData::Lighting& lighting,
        bool blurBehind = false,
        Vec4 gridColor = {},
        Vec2 gridSize = {},
        float gridLineWidth = 0.0f,
        bool isEditorPreview = false)
    {
        vkCmdSetPrimitiveTopology(
            commandBuffer,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        ++stats_.visibleFaces;
        ++stats_.drawCalls;
        stats_.vertices += 6;
        stats_.triangles += 2;

        const Vec3 sunRadiance {
            lighting.sun.color.x * lighting.sun.intensity,
            lighting.sun.color.y * lighting.sun.intensity,
            lighting.sun.color.z * lighting.sun.intensity,
        };
        const Vec3 ambientRadiance {
            lighting.ambient.color.x *
                lighting.ambient.intensity,
            lighting.ambient.color.y *
                lighting.ambient.intensity,
            lighting.ambient.color.z *
                lighting.ambient.intensity,
        };
        const TilePushConstants constants {
            .vertices = {
                Vec4 {
                    vertices[0].x,
                    vertices[0].y,
                    vertices[0].z,
                    1.0f },
                Vec4 {
                    vertices[1].x,
                    vertices[1].y,
                    vertices[1].z,
                    1.0f },
                Vec4 {
                    vertices[2].x,
                    vertices[2].y,
                    vertices[2].z,
                    1.0f },
                Vec4 {
                    vertices[3].x,
                    vertices[3].y,
                    vertices[3].z,
                    1.0f },
            },
            .shadowVertices = shadowVertices,
            .color = color,
            .normalAndAmbientRed = {
                normal.x,
                normal.y,
                normal.z,
                ambientRadiance.x,
            },
            .sunDirectionAndAmbientGreen = {
                lighting.sun.direction.x,
                lighting.sun.direction.y,
                lighting.sun.direction.z,
                ambientRadiance.y,
            },
            .sunRadianceAndAmbientBlue = {
                sunRadiance.x,
                sunRadiance.y,
                sunRadiance.z,
                ambientRadiance.z,
            },
            .shadowOptions = {
                lighting.shadows.enabled ? 1.0f : 0.0f,
                std::clamp(
                    lighting.shadows.opacity, 0.0f, 1.0f),
                std::max(lighting.shadows.bias, 0.0f),
                gridColor.w > 0.0f &&
                        gridLineWidth > 0.0f &&
                        gridSize.x > 0.0f &&
                        gridSize.y > 0.0f
                    ? gridLineWidth
                    : 0.0f,
            },
            .materialOptions = {
                blurBehind ? 1.0f : 0.0f,
                gridSize.x,
                gridSize.y,
                isEditorPreview
                    ? -config::iceBlurRadiusPixels
                    : config::iceBlurRadiusPixels,
            },
            .gridColor = gridColor,
            .textureOptions = {
                0.0f,
                0.0f,
                std::max(lighting.specularStrength, 0.0f),
                std::max(lighting.specularPower, 1.0f),
            },
        };
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TilePushConstants),
            &constants);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    void drawWaterFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        Vec4 color,
        Vec2 worldOrigin,
        Vec2 size,
        float animationTimeSeconds,
        bool isEditorPreview)
    {
        vkCmdSetPrimitiveTopology(
            commandBuffer,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        ++stats_.visibleFaces;
        ++stats_.drawCalls;
        stats_.vertices += 6;
        stats_.triangles += 2;

        const TilePushConstants constants {
            .vertices = {
                Vec4 { vertices[0].x, vertices[0].y, vertices[0].z, 1.0f },
                Vec4 { vertices[1].x, vertices[1].y, vertices[1].z, 1.0f },
                Vec4 { vertices[2].x, vertices[2].y, vertices[2].z, 1.0f },
                Vec4 { vertices[3].x, vertices[3].y, vertices[3].z, 1.0f },
            },
            .color = color,
            .materialOptions = {
                0.0f,
                size.x,
                size.y,
                isEditorPreview ? -1.0f : 1.0f,
            },
            .gridColor = {
                worldOrigin.x,
                worldOrigin.y,
                animationTimeSeconds,
                0.0f,
            },
            .textureOptions = {
                config::waterRippleSpatialFrequency,
                config::waterRippleSpeed,
                config::waterRefractionStrength,
                0.0f,
            },
        };
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TilePushConstants),
            &constants);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    void drawShadowFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec4, 4>& shadowVertices)
    {
        const TilePushConstants constants {
            .shadowVertices = shadowVertices,
        };
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TilePushConstants),
            &constants);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    void drawModel(
        VkCommandBuffer commandBuffer,
        const IsoRenderLayout& layout,
        const ShadowRenderLayout& shadowLayout,
        const RenderFrameData::Tile& tile,
        const RenderFrameData::Lighting& lighting)
    {
        const VulkanModelResources::MeshView mesh =
            models_.meshForModel(tile.model);
        const VulkanModelResources::MaterialBinding material =
            models_.materialForModel(tile.model);
        const ModelTransformPoints transform =
            modelTransformPoints(tile);
        const VkExtent2D extent = swapchain_.renderExtent();
        auto isoPoint = [&](Vec3 point) {
            const Vec3 projected =
                IsoScenePreparer::projectIsoPoint(
                    layout,
                    {
                        static_cast<float>(extent.width),
                        static_cast<float>(extent.height),
                    },
                    point);
            return Vec4 {
                projected.x,
                projected.y,
                projected.z,
                1.0f,
            };
        };
        const Vec3 sunRadiance {
            lighting.sun.color.x * lighting.sun.intensity,
            lighting.sun.color.y * lighting.sun.intensity,
            lighting.sun.color.z * lighting.sun.intensity,
        };
        const Vec3 ambientRadiance {
            lighting.ambient.color.x *
                lighting.ambient.intensity,
            lighting.ambient.color.y *
                lighting.ambient.intensity,
            lighting.ambient.color.z *
                lighting.ambient.intensity,
        };
        const TilePushConstants constants {
            .vertices = affineTransformColumns(
                isoPoint(transform.origin),
                isoPoint(transform.xPoint),
                isoPoint(transform.yPoint),
                isoPoint(transform.zPoint)),
            .shadowVertices = affineTransformColumns(
                IsoScenePreparer::projectShadowPoint(
                    shadowLayout, transform.origin),
                IsoScenePreparer::projectShadowPoint(
                    shadowLayout, transform.xPoint),
                IsoScenePreparer::projectShadowPoint(
                    shadowLayout, transform.yPoint),
                IsoScenePreparer::projectShadowPoint(
                    shadowLayout, transform.zPoint)),
            .color = tile.color,
            .normalAndAmbientRed = {
                0.0f, 0.0f, 0.0f, ambientRadiance.x },
            .sunDirectionAndAmbientGreen = {
                lighting.sun.direction.x,
                lighting.sun.direction.y,
                lighting.sun.direction.z,
                ambientRadiance.y,
            },
            .sunRadianceAndAmbientBlue = {
                sunRadiance.x,
                sunRadiance.y,
                sunRadiance.z,
                ambientRadiance.z,
            },
            .shadowOptions = {
                lighting.shadows.enabled ? 1.0f : 0.0f,
                std::clamp(
                    lighting.shadows.opacity *
                        std::clamp(
                            lighting.modelShadowReceive,
                            0.0f,
                            1.0f),
                    0.0f,
                    1.0f),
                std::max(lighting.shadows.bias, 0.0f),
                0.0f,
            },
            .materialOptions = {
                tile.blurBehind ? 1.0f : 0.0f,
                tile.beltScrollOffset,
                static_cast<float>(material.textureIndex),
                tile.isEditorPreview
                    ? -config::iceBlurRadiusPixels
                    : config::iceBlurRadiusPixels,
            },
            .textureOptions = {
                shaderValue(material.mode),
                static_cast<float>(
                    tile.modelRotationQuarterTurns % 4),
                std::max(lighting.specularStrength, 0.0f),
                std::max(lighting.specularPower, 1.0f),
            },
        };
        const VkBuffer vertexBuffer = mesh.vertexBuffer;
        constexpr VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(
            commandBuffer,
            mesh.indexBuffer,
            0,
            VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TilePushConstants),
            &constants);
        vkCmdDrawIndexed(
            commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        stats_.visibleFaces += mesh.indexCount / 3;
        ++stats_.drawCalls;
        stats_.vertices += mesh.indexCount;
        stats_.triangles += mesh.indexCount / 3;
    }

    void drawModelShadow(
        VkCommandBuffer commandBuffer,
        const ShadowRenderLayout& layout,
        const RenderFrameData::Tile& tile)
    {
        const VulkanModelResources::MeshView mesh =
            models_.meshForModel(tile.model);
        const ModelTransformPoints transform =
            modelTransformPoints(tile);
        const TilePushConstants constants {
            .shadowVertices = affineTransformColumns(
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.origin),
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.xPoint),
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.yPoint),
                IsoScenePreparer::projectShadowPoint(
                    layout, transform.zPoint)),
        };
        const VkBuffer vertexBuffer = mesh.vertexBuffer;
        constexpr VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(
            commandBuffer,
            mesh.indexBuffer,
            0,
            VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TilePushConstants),
            &constants);
        vkCmdDrawIndexed(
            commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }

    void drawUiRect(
        VkCommandBuffer commandBuffer,
        const UiDrawCommand& command,
        Vec2 viewportSize,
        const RenderFrameData::Lighting& lighting)
    {
        const float left =
            -1.0f + 2.0f * command.rect.position.x /
                viewportSize.x;
        const float right =
            -1.0f + 2.0f *
                (command.rect.position.x +
                 command.rect.size.x) /
                viewportSize.x;
        const float top =
            1.0f - 2.0f * command.rect.position.y /
                viewportSize.y;
        const float bottom =
            1.0f - 2.0f *
                (command.rect.position.y +
                 command.rect.size.y) /
                viewportSize.y;
        const std::array vertices {
            Vec3 { left, top, 0.0f },
            Vec3 { right, top, 0.0f },
            Vec3 { right, bottom, 0.0f },
            Vec3 { left, bottom, 0.0f },
        };
        if (command.kind == UiDrawKind::Solid) {
            drawFace(
                commandBuffer,
                vertices,
                {},
                command.color,
                {},
                lighting);
            return;
        }

        ++stats_.visibleFaces;
        ++stats_.drawCalls;
        stats_.vertices += 6;
        stats_.triangles += 2;
        const float materialMode =
            command.kind == UiDrawKind::FontGlyph
            ? 3.0f
            : 4.0f;
        const TilePushConstants constants {
            .vertices = {
                Vec4 { left, top, 0.0f, 1.0f },
                Vec4 { right, top, 0.0f, 1.0f },
                Vec4 { right, bottom, 0.0f, 1.0f },
                Vec4 { left, bottom, 0.0f, 1.0f },
            },
            .color = command.color,
            .materialOptions = {
                0.0f,
                command.uvRect.size.x,
                command.uvRect.size.y,
                0.0f,
            },
            .gridColor = {
                command.uvRect.position.x,
                command.uvRect.position.y,
                0.0f,
                0.0f,
            },
            .textureOptions = {
                materialMode, 0.0f, 0.0f, 1.0f },
        };
        vkCmdPushConstants(
            commandBuffer,
            pipelines_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TilePushConstants),
            &constants);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }

    VulkanSwapchainResources& swapchain_;
    VulkanShadowPass& shadowPass_;
    VulkanSsaoPass& ssaoPass_;
    VulkanSceneDescriptors& descriptors_;
    VulkanPipelineFactory& pipelines_;
    VulkanModelResources& models_;
    const VulkanSceneRecorder::FrameConfiguration& configuration_;
    RenderStats stats_ {};
};

} // namespace

RenderStats VulkanSceneRecorder::record(
    Resources resources,
    const FrameConfiguration& configuration,
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    const RenderFrameData& frameData,
    const PreparedRenderScene& scene,
    const UiDrawData& uiDrawData) const
{
    return SceneRecordingSession(resources, configuration).record(
        commandBuffer,
        imageIndex,
        frameData,
        scene,
        uiDrawData);
}

} // namespace sokoban
