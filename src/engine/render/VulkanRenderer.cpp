#include "engine/render/VulkanRenderer.hpp"

#include "engine/Log.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanDeviceSelection.hpp"
#include "engine/render/VulkanFrameCapture.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include "engine/ui/UiConfig.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {
namespace {

Vec3 transformedModelPoint(
    const ModelTransformPoints& transform,
    Vec3 localPoint)
{
    const Vec3 xAxis {
        transform.xPoint.x - transform.origin.x,
        transform.xPoint.y - transform.origin.y,
        transform.xPoint.z - transform.origin.z,
    };
    const Vec3 yAxis {
        transform.yPoint.x - transform.origin.x,
        transform.yPoint.y - transform.origin.y,
        transform.yPoint.z - transform.origin.z,
    };
    const Vec3 zAxis {
        transform.zPoint.x - transform.origin.x,
        transform.zPoint.y - transform.origin.y,
        transform.zPoint.z - transform.origin.z,
    };
    return {
        transform.origin.x + xAxis.x * localPoint.x +
            yAxis.x * localPoint.y + zAxis.x * localPoint.z,
        transform.origin.y + xAxis.y * localPoint.x +
            yAxis.y * localPoint.y + zAxis.y * localPoint.z,
        transform.origin.z + xAxis.z * localPoint.x +
            yAxis.z * localPoint.y + zAxis.z * localPoint.z,
    };
}

float cross(Vec2 origin, Vec2 first, Vec2 second)
{
    return (first.x - origin.x) * (second.y - origin.y) -
        (first.y - origin.y) * (second.x - origin.x);
}

bool pointInConvexHull(std::array<Vec2, 8> points, Vec2 point)
{
    std::ranges::sort(points, {}, [](Vec2 value) {
        return std::pair { value.x, value.y };
    });
    std::array<Vec2, 16> hull {};
    std::size_t count = 0;
    for (Vec2 candidate : points) {
        while (count >= 2 &&
               cross(hull[count - 2], hull[count - 1], candidate) <= 0.0f) {
            --count;
        }
        hull[count++] = candidate;
    }
    const std::size_t lowerCount = count;
    for (std::size_t index = points.size() - 1; index-- > 0;) {
        const Vec2 candidate = points[index];
        while (count > lowerCount &&
               cross(hull[count - 2], hull[count - 1], candidate) <= 0.0f) {
            --count;
        }
        hull[count++] = candidate;
    }
    if (count < 4) {
        return false;
    }
    --count;
    constexpr float edgeTolerancePixels = 1.5f;
    for (std::size_t index = 0; index < count; ++index) {
        if (cross(hull[index], hull[(index + 1) % count], point) <
            -edgeTolerancePixels) {
            return false;
        }
    }
    return true;
}

} // namespace

VulkanRenderer::VulkanRenderer(
    SDL_Window* window,
    std::filesystem::path assetRoot,
    const AssetManifest& manifest,
    const FontAtlas& uiFont,
    AntiAliasingMode antiAliasingMode,
    int renderScalePercent,
    bool vsync)
    : window_(window)
    , assetRoot_(std::move(assetRoot))
    , deviceContext_(window)
    , reconfigurationQueue_({
          .antiAliasing = antiAliasingMode,
          .renderScalePercent = renderScalePercent,
          .wireframe = false,
      })
    , vsync_(vsync)
{
    wireframeLineWidth_ = std::clamp(
        wireframeLineWidth_,
        1.0f,
        deviceContext_.wireframeLineWidthRange()[1]);
    // The default MSAA mode is a request; drop to what the device supports.
    activeSampleCount_ = sampleCountForMode(antiAliasingMode);
    shadowPass_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        shadowFormat_);
    uiResources_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        deviceContext_.commandPool(),
        deviceContext_.graphicsQueue(),
        uiFont,
        loadRgbaImage(assetRoot_ / config::titleBackgroundPath));
    modelResources_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        deviceContext_.commandPool(),
        deviceContext_.graphicsQueue(),
        assetRoot_, manifest);
    activeResources_ = createRenderResources(
        reconfigurationQueue_.active());
    descriptorSync_.markAllUpdated();
    logRenderConfiguration();
    createFrameResources();
    initializeDebugUi();
#if SOKOBAN_ENABLE_DEBUG_UI
    // After initializeDebugUi: registering a thumbnail with ImGui needs the
    // Vulkan backend to exist. Failure here only costs the editor its
    // thumbnails, so it is not fatal.
    thumbnailPass_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        deviceContext_.commandPool(),
        deviceContext_.graphicsQueue(),
        assetRoot_);
#endif
}

VulkanRenderer::~VulkanRenderer()
{
    deviceContext_.waitIdle();

    shutdownDebugUi();

    for (auto& frame : frames_) {
        if (frame.imageAvailable) {
            vkDestroySemaphore(
                deviceContext_.device(),
                frame.imageAvailable,
                nullptr);
        }
        if (frame.renderFinished) {
            vkDestroySemaphore(
                deviceContext_.device(),
                frame.renderFinished,
                nullptr);
        }
        if (frame.inFlight) {
            vkDestroyFence(
                deviceContext_.device(),
                frame.inFlight,
                nullptr);
        }
    }

    retiredResources_.clear();
    activeResources_ = {};
    modelResources_.destroy();
    uiResources_.destroy();

    shadowPass_.destroy();
}

VulkanRenderer::PreparedFrame VulkanRenderer::prepareFrame(
    RenderFrameData frameData,
    std::optional<RenderFrameData> previewFrameData)
{
    const VkExtent2D extent =
        activeResources_.swapchain->renderExtent();
    std::shared_ptr<PreparedFrameScratch> scratch =
        preparedFrameScratch_.acquire();
    scratch->frameData = std::move(frameData);
    scratch->generation = nextPreparedFrameGeneration_++;
    scenePreparer_.prepare(
        scratch->frameData,
        {
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
        },
        scratch->scene);
    scratch->previewFrameData = std::move(previewFrameData);
    scratch->previewScene.reset();
    if (scratch->previewFrameData) {
        scratch->previewScene.emplace();
        scenePreparer_.prepare(
            *scratch->previewFrameData,
            {
                static_cast<float>(extent.width) * 0.75f,
                static_cast<float>(extent.height) * 0.75f,
            },
            *scratch->previewScene);
    }

    PreparedFrame frame;
    frame.levelWidth = scratch->frameData.levelWidth;
    frame.levelHeight = scratch->frameData.levelHeight;
    frame.generation = scratch->generation;
    frame.scratch = std::move(scratch);
    return frame;
}

const VulkanRenderer::PreparedFrameScratch&
VulkanRenderer::resolvePreparedFrame(const PreparedFrame& frame) const
{
    if (!frame.scratch || frame.generation == 0) {
        throw std::logic_error("Prepared frame was never initialized");
    }
    if (frame.scratch->generation != frame.generation) {
        throw std::logic_error(
            "Prepared frame scratch changed while it was leased");
    }
    return *frame.scratch;
}

void VulkanRenderer::drawFrame(
    const PreparedFrame& preparedFrame,
    const UiDrawData& uiDrawData,
    bool developerWorkspaceVisible)
{
    const PreparedFrameScratch& prepared =
        resolvePreparedFrame(preparedFrame);
    const RenderFrameData& frameData = prepared.frameData;
    renderAssetRequirementsForFrame(frameData, frameAssetRequirements_);
    if (prepared.previewFrameData) {
        RenderAssetRequirements previewRequirements;
        renderAssetRequirementsForFrame(
            *prepared.previewFrameData, previewRequirements);
        frameAssetRequirements_.merge(previewRequirements);
    }
    ensureAssets(frameAssetRequirements_);

#if SOKOBAN_ENABLE_DEBUG_UI
    // Finish the ImGui frame even when swapchain acquisition is out of date
    // and this render frame has to be skipped during a window-mode change.
    ImGui::Render();
#endif

    auto& frame = frames_[currentFrame_];
    vkCheck(
        vkWaitForFences(
            deviceContext_.device(),
            1,
            &frame.inFlight,
            VK_TRUE,
            UINT64_MAX),
        "vkWaitForFences failed");
    completeFrame(currentFrame_);
    modelResources_.retireCompletedUploads();
    if (modelResources_.publishReadyAssets(1)) {
        descriptorSync_.resourcesChanged();
    }
    if (descriptorSync_.needsUpdate(currentFrame_)) {
        activeResources_.sceneDescriptors->update(
            currentFrame_,
            descriptorResources(activeResources_));
        descriptorSync_.markUpdated(currentFrame_);
    }
    modelResources_.updateAnimations(frameData, currentFrame_);
    if (prepared.previewFrameData) {
        modelResources_.updateAnimations(
            *prepared.previewFrameData, currentFrame_);
    }

    uint32_t imageIndex = 0;
    VkResult acquired = activeResources_.swapchain->acquire(
        frame.imageAvailable, imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainRecreationRequested_ = true;
        applyPendingReconfiguration();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquired, "vkAcquireNextImageKHR failed");
    }
    if (acquired == VK_SUBOPTIMAL_KHR) {
        swapchainRecreationRequested_ = true;
    }

    vkCheck(
        vkResetFences(
            deviceContext_.device(), 1, &frame.inFlight),
        "vkResetFences failed");
    vkCheck(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");

    lastStats_ = sceneRecorder_.record(
        {
            .swapchain = *activeResources_.swapchain,
            .shadowPass = shadowPass_,
            .ssaoPass = *activeResources_.ssaoPass,
            .sceneDescriptors =
                *activeResources_.sceneDescriptors,
            .pipelines = *activeResources_.pipelines,
            .modelResources = modelResources_,
        },
        {
            .descriptorFrameIndex = currentFrame_,
            .activeSamples = sampleCountValue(),
            .wireframeEnabled =
                reconfigurationQueue_.active().wireframe,
            .wireframeLineWidth = wireframeLineWidth_,
            .statsFrameIndex = nextStatsFrameIndex_++,
            .pipelineRebuilds = pipelineRebuilds_,
            .swapchainRecreations = swapchainRecreations_,
            .swapchainRecreationDeferrals =
                swapchainRecreationDeferrals_,
            .renderResourceReconfigurations =
                renderResourceReconfigurations_,
            .presentQueueRetirementWaits =
                presentQueueRetirementWaits_,
            .retiredRenderResourceSets =
                static_cast<uint32_t>(retiredResources_.size()),
            .rendererReconfigurationPending =
                reconfigurationQueue_
                    .plan(swapchainRecreationRequested_)
                    .has_value(),
            .developerWorkspaceVisible = developerWorkspaceVisible,
        },
        frame.commandBuffer,
        imageIndex,
        frameData,
        prepared.scene,
        prepared.previewFrameData
            ? &*prepared.previewFrameData
            : nullptr,
        prepared.previewScene ? &*prepared.previewScene : nullptr,
        uiDrawData);

    VkSemaphoreSubmitInfo waitSemaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkCommandBufferSubmitInfo commandBuffer {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };

    VkSemaphoreSubmitInfo signalSemaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.renderFinished,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };

    VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSemaphore,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBuffer,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphore,
    };

    vkCheck(
        vkQueueSubmit2(
            deviceContext_.graphicsQueue(),
            1,
            &submit,
            frame.inFlight),
        "vkQueueSubmit2 failed");
    frameResourceTracker_.markSubmitted(
        currentFrame_, activeResourceGeneration_);

    const VkResult presented = activeResources_.swapchain->present(
        deviceContext_.presentQueue(),
        frame.renderFinished,
        imageIndex);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        swapchainRecreationRequested_ = true;
    } else {
        vkCheck(presented, "vkQueuePresentKHR failed");
    }

    currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight_;
    applyPendingReconfiguration();
}

void VulkanRenderer::preloadAssets(const RenderAssetRequirements& requirements)
{
    modelResources_.requestAssets(requirements);
}

void VulkanRenderer::ensureAssets(const RenderAssetRequirements& requirements)
{
    if (modelResources_.ensureAssets(requirements)) {
        descriptorSync_.resourcesChanged();
    }
}

VkDescriptorSet VulkanRenderer::tileThumbnail(TileType tile)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    return thumbnailPass_.thumbnailFor(tile);
#else
    (void)tile;
    return VK_NULL_HANDLE;
#endif
}

void VulkanRenderer::invalidateTileThumbnails()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    thumbnailPass_.invalidate();
#endif
}

VkExtent2D VulkanRenderer::renderExtent() const
{
    return activeResources_.swapchain
        ? activeResources_.swapchain->renderExtent()
        : VkExtent2D { 0, 0 };
}

uint64_t VulkanRenderer::gameViewportTexture() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    return reinterpret_cast<uint64_t>(
        activeResources_.gameViewportTexture);
#else
    return 0;
#endif
}

ImageData VulkanRenderer::captureRenderedFrame(std::optional<VkRect2D> region)
{
    if (!activeResources_.swapchain) {
        throw std::runtime_error("No swapchain to capture from");
    }
    const VkExtent2D full = activeResources_.swapchain->renderExtent();
    const VkRect2D rect =
        region.value_or(VkRect2D { .offset = { 0, 0 }, .extent = full });

    // Everything submitted must have landed before the copy reads the image.
    deviceContext_.waitIdle();
    return captureImageRegion(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        deviceContext_.commandPool(),
        deviceContext_.graphicsQueue(),
        activeResources_.swapchain->resolvedColorImage(),
        activeResources_.swapchain->colorFormat(),
        // The final upscale leaves the resolve target as a transfer source.
        // Capture preserves that layout; the next frame deliberately discards
        // and transitions it back to a color attachment in beginFrame().
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        rect.offset,
        rect.extent);
}

void VulkanRenderer::syncManifestTextures()
{
    if (modelResources_.syncManifestTextures()) {
        // The descriptor array is padded to maxModelTextures with the fallback
        // texture, so the new slot already has something valid bound; the
        // rewrite is what points it at the real image once it publishes.
        descriptorSync_.resourcesChanged();
    }
}

void VulkanRenderer::syncManifestModels()
{
    (void)modelResources_.syncManifestModels();
}

bool VulkanRenderer::updateTexture(
    RenderTexture texture, const ImageData& image)
{
    const VulkanModelResources::TextureUpdate result =
        modelResources_.updateTexture(texture, image);
    // A same-size repaint reuses the image, view and sampler, so descriptors
    // stay valid. A resize recreates them, and every set pointing at the old
    // view has to be rewritten or the ground samples a destroyed image.
    if (result.descriptorsChanged) {
        descriptorSync_.resourcesChanged();
    }
    return result.updated;
}

void VulkanRenderer::handleEvent(const SDL_Event& event)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui_ImplSDL3_ProcessEvent(&event);
#else
    (void)event;
#endif
}

void VulkanRenderer::beginDebugUiFrame()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
#endif
}

bool VulkanRenderer::wantsKeyboardCapture() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const bool gameViewportFocused =
        gameViewportDisplay_ && gameViewportDisplay_->focused;
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard &&
        !gameViewportFocused;
#else
    return false;
#endif
}

bool VulkanRenderer::wantsMouseCapture() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const bool gameViewportHovered =
        gameViewportDisplay_ && gameViewportDisplay_->hovered;
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse &&
        !gameViewportHovered;
#else
    return false;
#endif
}

void VulkanRenderer::setGameViewportDisplay(
    std::optional<GameViewportDisplay> display)
{
    if (display &&
        (display->size.x <= 0.0f || display->size.y <= 0.0f)) {
        display.reset();
    }
    gameViewportDisplay_ = std::move(display);
}

bool VulkanRenderer::hasGameViewportDisplay() const
{
    return gameViewportDisplay_.has_value();
}

Vec2 VulkanRenderer::mapPointerToGameViewport(
    Vec2 pointer,
    Vec2 gameUiExtent) const
{
    if (!gameViewportDisplay_) {
        return pointer;
    }
    const GameViewportDisplay& display = *gameViewportDisplay_;
    const Vec2 local {
        pointer.x - display.position.x,
        pointer.y - display.position.y,
    };
    if (local.x < 0.0f || local.y < 0.0f ||
        local.x > display.size.x || local.y > display.size.y) {
        return { -1.0f, -1.0f };
    }
    return {
        local.x * gameUiExtent.x / display.size.x,
        local.y * gameUiExtent.y / display.size.y,
    };
}

std::optional<GridPosition3> VulkanRenderer::pickIsoGridCell(
    const PreparedFrame& frame,
    Vec2 pixelPosition) const
{
    const PreparedFrameScratch& prepared = resolvePreparedFrame(frame);
    const RenderFrameData& frameData = prepared.frameData;
    if (frameData.viewMode != RenderViewMode::Isometric3D ||
        frameData.levelWidth == 0 ||
        frameData.levelHeight == 0 ||
        activeResources_.swapchain->extent().width == 0 ||
        activeResources_.swapchain->extent().height == 0) {
        return std::nullopt;
    }

    const VkExtent2D outputExtent =
        activeResources_.swapchain->extent();
    const Vec2 mappedPosition = mapPointerToGameViewport(
        pixelPosition,
        {
            static_cast<float>(outputExtent.width),
            static_cast<float>(outputExtent.height),
        });
    if (mappedPosition.x < 0.0f || mappedPosition.y < 0.0f) {
        return std::nullopt;
    }
    return scenePreparer_.pickGridCell(
        prepared.scene,
        mappedPosition,
        {
            static_cast<float>(outputExtent.width),
            static_cast<float>(outputExtent.height),
        },
        frameData.levelWidth,
        frameData.levelHeight,
        frameData.gridPickBorder);
}

std::optional<Vec3> VulkanRenderer::pickIsoGroundPoint(
    const PreparedFrame& frame,
    Vec2 pixelPosition) const
{
    const PreparedFrameScratch& prepared = resolvePreparedFrame(frame);
    const RenderFrameData& frameData = prepared.frameData;
    if (frameData.viewMode != RenderViewMode::Isometric3D ||
        activeResources_.swapchain->extent().width == 0 ||
        activeResources_.swapchain->extent().height == 0) {
        return std::nullopt;
    }

    const VkExtent2D outputExtent = activeResources_.swapchain->extent();
    const Vec2 mappedPosition = mapPointerToGameViewport(
        pixelPosition,
        {
            static_cast<float>(outputExtent.width),
            static_cast<float>(outputExtent.height),
        });
    if (mappedPosition.x < 0.0f || mappedPosition.y < 0.0f) {
        return std::nullopt;
    }
    return scenePreparer_.pickGroundPoint(
        prepared.scene,
        mappedPosition,
        {
            static_cast<float>(outputExtent.width),
            static_cast<float>(outputExtent.height),
        });
}

std::optional<std::size_t> VulkanRenderer::pickDecoration(
    const PreparedFrame& frame,
    Vec2 pixelPosition) const
{
    const PreparedFrameScratch& prepared = resolvePreparedFrame(frame);
    if (prepared.frameData.viewMode != RenderViewMode::Isometric3D) {
        return std::nullopt;
    }
    const VkExtent2D outputExtent = activeResources_.swapchain->extent();
    if (outputExtent.width == 0 || outputExtent.height == 0) {
        return std::nullopt;
    }
    const Vec2 mappedPosition = mapPointerToGameViewport(
        pixelPosition,
        {
            static_cast<float>(outputExtent.width),
            static_cast<float>(outputExtent.height),
        });
    if (mappedPosition.x < 0.0f || mappedPosition.y < 0.0f) {
        return std::nullopt;
    }

    std::optional<std::size_t> result;
    float nearestDepth = std::numeric_limits<float>::max();
    for (const RenderFrameData::Tile& tile : prepared.frameData.tiles) {
        if (!tile.editorDecorationIndex || tile.model.isCube()) {
            continue;
        }
        VulkanModelResources::ModelBounds bounds =
            modelResources_.boundsForModel(tile.model);
        if (!bounds.valid) {
            bounds = {
                .minimum = { 0.0f, 0.0f, 0.0f },
                .maximum = { 1.0f, 1.0f, 1.0f },
                .valid = true,
            };
        }

        const ModelTransformPoints transform =
            IsoScenePreparer::modelTransformPoints(tile);
        const std::array<Vec3, 8> localCorners {
            Vec3 { bounds.minimum.x, bounds.minimum.y, bounds.minimum.z },
            Vec3 { bounds.maximum.x, bounds.minimum.y, bounds.minimum.z },
            Vec3 { bounds.minimum.x, bounds.maximum.y, bounds.minimum.z },
            Vec3 { bounds.maximum.x, bounds.maximum.y, bounds.minimum.z },
            Vec3 { bounds.minimum.x, bounds.minimum.y, bounds.maximum.z },
            Vec3 { bounds.maximum.x, bounds.minimum.y, bounds.maximum.z },
            Vec3 { bounds.minimum.x, bounds.maximum.y, bounds.maximum.z },
            Vec3 { bounds.maximum.x, bounds.maximum.y, bounds.maximum.z },
        };
        Vec2 minimum {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
        };
        Vec2 maximum {
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
        };
        std::array<Vec2, 8> projectedCorners {};
        float depth = 0.0f;
        for (std::size_t corner = 0; corner < localCorners.size(); ++corner) {
            const Vec3 localCorner = localCorners[corner];
            const Vec3 projected = IsoScenePreparer::projectIsoPoint(
                prepared.scene.isoLayout,
                prepared.scene.renderExtent,
                transformedModelPoint(transform, localCorner));
            const Vec2 pixel {
                (projected.x + 1.0f) * 0.5f *
                    static_cast<float>(outputExtent.width),
                (1.0f - projected.y) * 0.5f *
                    static_cast<float>(outputExtent.height),
            };
            projectedCorners[corner] = pixel;
            minimum.x = std::min(minimum.x, pixel.x);
            minimum.y = std::min(minimum.y, pixel.y);
            maximum.x = std::max(maximum.x, pixel.x);
            maximum.y = std::max(maximum.y, pixel.y);
            depth += projected.z;
        }
        constexpr float pickPaddingPixels = 3.0f;
        if (mappedPosition.x < minimum.x - pickPaddingPixels ||
            mappedPosition.y < minimum.y - pickPaddingPixels ||
            mappedPosition.x > maximum.x + pickPaddingPixels ||
            mappedPosition.y > maximum.y + pickPaddingPixels) {
            continue;
        }
        if (!pointInConvexHull(projectedCorners, mappedPosition)) {
            continue;
        }
        depth /= static_cast<float>(localCorners.size());
        if (depth < nearestDepth) {
            nearestDepth = depth;
            result = static_cast<std::size_t>(*tile.editorDecorationIndex);
        }
    }
    return result;
}

std::optional<Vec2> VulkanRenderer::projectToPixels(
    const PreparedFrame& frame,
    Vec3 worldPoint) const
{
    const PreparedFrameScratch& prepared = resolvePreparedFrame(frame);
    if (prepared.frameData.viewMode != RenderViewMode::Isometric3D) {
        return std::nullopt;
    }
    const VkExtent2D outputExtent = activeResources_.swapchain->extent();
    if (outputExtent.width == 0 || outputExtent.height == 0) {
        return std::nullopt;
    }
    const Vec3 clip = IsoScenePreparer::projectIsoPoint(
        prepared.scene.isoLayout, prepared.scene.renderExtent, worldPoint);
    const Vec2 normalized {
        (clip.x + 1.0f) * 0.5f,
        (1.0f - clip.y) * 0.5f,
    };
    if (gameViewportDisplay_) {
        return Vec2 {
            gameViewportDisplay_->position.x +
                normalized.x * gameViewportDisplay_->size.x,
            gameViewportDisplay_->position.y +
                normalized.y * gameViewportDisplay_->size.y,
        };
    }
    return Vec2 {
        normalized.x * static_cast<float>(outputExtent.width),
        normalized.y * static_cast<float>(outputExtent.height),
    };
}

std::optional<UiRect> VulkanRenderer::primaryPlayerBoundsToPixels(
    const PreparedFrame& frame) const
{
    const PreparedFrameScratch& prepared = resolvePreparedFrame(frame);
    if (prepared.frameData.viewMode != RenderViewMode::Isometric3D) {
        return std::nullopt;
    }
    const VkExtent2D outputExtent = activeResources_.swapchain->extent();
    if (outputExtent.width == 0 || outputExtent.height == 0) {
        return std::nullopt;
    }

    const auto player = std::ranges::find_if(
        prepared.frameData.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.isPrimaryPlayer;
        });
    if (player == prepared.frameData.tiles.end()) {
        return std::nullopt;
    }

    const ModelTransformPoints transform =
        IsoScenePreparer::modelTransformPoints(*player);
    constexpr std::array<Vec3, 8> localCorners {
        Vec3 { 0.0f, 0.0f, 0.0f },
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 1.0f, 1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
        Vec3 { 1.0f, 0.0f, 1.0f },
        Vec3 { 0.0f, 1.0f, 1.0f },
        Vec3 { 1.0f, 1.0f, 1.0f },
    };
    Vec2 minimum {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec2 maximum {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (const Vec3 localCorner : localCorners) {
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(
            prepared.scene.isoLayout,
            prepared.scene.renderExtent,
            transformedModelPoint(transform, localCorner));
        const Vec2 normalized {
            (clip.x + 1.0f) * 0.5f,
            (1.0f - clip.y) * 0.5f,
        };
        // SelectorPrompt is part of UiDrawData, which is composited into the
        // game image before that image is displayed in the Debug viewport.
        // Its coordinates therefore belong to the game UI's full output
        // extent. Mapping them to gameViewportDisplay_ here would apply the
        // viewport translation and scale twice and make the prompt drift as
        // the player moves across the board.
        const Vec2 pixel {
            normalized.x * static_cast<float>(outputExtent.width),
            normalized.y * static_cast<float>(outputExtent.height),
        };
        minimum.x = std::min(minimum.x, pixel.x);
        minimum.y = std::min(minimum.y, pixel.y);
        maximum.x = std::max(maximum.x, pixel.x);
        maximum.y = std::max(maximum.y, pixel.y);
    }
    return UiRect {
        .position = minimum,
        .size = { maximum.x - minimum.x, maximum.y - minimum.y },
    };
}

void VulkanRenderer::waitIdle() const
{
    deviceContext_.waitIdle();
}

AntiAliasingMode VulkanRenderer::antiAliasingMode() const
{
    return reconfigurationQueue_.requested().antiAliasing;
}

VkSampleCountFlagBits VulkanRenderer::activeSampleCount() const
{
    return activeSampleCount_;
}

RenderStats VulkanRenderer::renderStats() const
{
    return lastStats_;
}

VulkanModelResources::LoadingStats VulkanRenderer::assetLoadingStats() const
{
    return modelResources_.loadingStats();
}

std::string_view VulkanRenderer::physicalDeviceName() const
{
    return deviceContext_.physicalDeviceProperties().deviceName;
}

const char* VulkanRenderer::physicalDeviceTypeName() const
{
    return vulkanDeviceTypeName(
        deviceContext_.physicalDeviceProperties().deviceType);
}

const char* VulkanRenderer::presentModeName() const
{
    switch (activeResources_.swapchain->presentMode()) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
    default: return "Other";
    }
}

bool VulkanRenderer::wireframeEnabled() const
{
    return reconfigurationQueue_.requested().wireframe;
}

void VulkanRenderer::setWireframeEnabled(bool enabled)
{
    reconfigurationQueue_.requestWireframe(enabled);
}

bool VulkanRenderer::wideLinesSupported() const
{
    return deviceContext_.wideLinesSupported();
}

float VulkanRenderer::wireframeLineWidth() const
{
    return wireframeLineWidth_;
}

std::array<float, 2> VulkanRenderer::wireframeLineWidthRange() const
{
    return deviceContext_.wireframeLineWidthRange();
}

void VulkanRenderer::setWireframeLineWidth(float lineWidth)
{
    const float maxLineWidth = deviceContext_.wideLinesSupported()
        ? deviceContext_.wireframeLineWidthRange()[1]
        : 1.0f;
    wireframeLineWidth_ = std::clamp(lineWidth, 1.0f, maxLineWidth);
}

void VulkanRenderer::setAntiAliasingMode(AntiAliasingMode mode)
{
    reconfigurationQueue_.requestAntiAliasing(mode);
}

int VulkanRenderer::renderScalePercent() const
{
    return reconfigurationQueue_.requested().renderScalePercent;
}

void VulkanRenderer::setRenderScalePercent(int percent)
{
    reconfigurationQueue_.requestRenderScalePercent(percent);
}

VulkanSceneDescriptors::Resources VulkanRenderer::descriptorResources(
    const RenderResourceSet& resources) const
{
    return {
        .shadow = {
            .sampler = shadowPass_.sampler(),
            .imageView = shadowPass_.imageView(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        },
        .sceneColor = {
            .sampler = resources.swapchain->sceneColorSampler(),
            .imageView = resources.swapchain->sceneColorView(),
        },
        .sceneDepth = {
            .sampler = shadowPass_.sampler(),
            .imageView = resources.swapchain->sceneDepthView(),
        },
        .ssao = {
            .sampler = resources.ssaoPass->sampler(),
            .imageView = resources.ssaoPass->imageView(),
        },
        .uiFont = {
            .sampler = uiResources_.sampler(),
            .imageView = uiResources_.fontImageView(),
        },
        .titleBackground = {
            .sampler = uiResources_.sampler(),
            .imageView = uiResources_.titleBackgroundImageView(),
        },
        .modelTextures = modelResources_.textures(),
    };
}

void VulkanRenderer::setAnimationPreview(
    RenderModel model,
    const GltfAnimationClip* clip,
    float timeSeconds)
{
    modelResources_.setAnimationPreview(model, clip, timeSeconds);
}

VulkanRenderer::RenderResourceSet
VulkanRenderer::createRenderResources(
    const RendererSettingsSnapshot& settings)
{
    RenderResourceSet resources;
    const VkSampleCountFlagBits sampleCount =
        sampleCountForMode(settings.antiAliasing);
    resources.swapchain =
        std::make_unique<VulkanSwapchainResources>();
    resources.swapchain->create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        deviceContext_.surface(),
        window_,
        {
            .graphics = deviceContext_.queueFamilies().graphics,
            .present = deviceContext_.queueFamilies().present,
        },
        sampleCount,
        settings.renderScalePercent,
        depthFormat_,
        vsync_,
        activeResources_.swapchain
            ? activeResources_.swapchain->handle()
            : VK_NULL_HANDLE);
    resources.ssaoPass = std::make_unique<VulkanSsaoPass>();
    resources.ssaoPass->create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        resources.swapchain->renderExtent());
    resources.sceneDescriptors =
        std::make_unique<VulkanSceneDescriptors>();
    resources.sceneDescriptors->create(
        deviceContext_.device(),
        descriptorResources(resources),
        maxFramesInFlight_);
    resources.pipelines = createPipelines(resources, settings);
    return resources;
}

std::unique_ptr<VulkanPipelineFactory>
VulkanRenderer::createPipelines(
    const RenderResourceSet& resources,
    const RendererSettingsSnapshot& settings)
{
    auto pipelines = std::make_unique<VulkanPipelineFactory>();
    pipelines->create({
        .device = deviceContext_.device(),
        .assetRoot = assetRoot_,
        .descriptorSetLayout =
            resources.sceneDescriptors->layout(),
        .colorFormat = resources.swapchain->colorFormat(),
        .depthFormat = depthFormat_,
        .shadowFormat = shadowFormat_,
        .sampleCount =
            sampleCountForMode(settings.antiAliasing),
        .wireframe = settings.wireframe,
    });
    ++pipelineRebuilds_;
    return pipelines;
}

uint32_t VulkanRenderer::pendingFrameMask() const
{
    return frameResourceTracker_.pendingMask();
}

uint32_t VulkanRenderer::pendingFrameMaskForGeneration(
    uint64_t generation) const
{
    return frameResourceTracker_.pendingMaskForGeneration(
        generation);
}

void VulkanRenderer::retireResources(
    RenderResourceSet resources,
    uint32_t pendingFrameMask)
{
    retiredResources_.push_back({
        .resources = std::move(resources),
        .pendingFrameMask = pendingFrameMask,
    });
    destroyCompletedRetirements();
}

void VulkanRenderer::destroyCompletedRetirements()
{
    const bool hasCompletedSwapchain =
        std::ranges::any_of(
            retiredResources_,
            [](const RetiredRenderResources& retired) {
                return retired.pendingFrameMask == 0 &&
                    retired.resources.swapchain != nullptr;
            });
    if (hasCompletedSwapchain) {
        // Render fences do not cover presentation completion. Wait only the
        // present queue once the old swapchain has no in-flight render users.
        vkCheck(
            vkQueueWaitIdle(deviceContext_.presentQueue()),
            "vkQueueWaitIdle failed while retiring swapchain");
        ++presentQueueRetirementWaits_;
    }
    std::erase_if(
        retiredResources_,
        [this](RetiredRenderResources& retired) {
            if (retired.pendingFrameMask != 0) {
                return false;
            }
            releaseGameViewportTexture(retired.resources);
            return true;
        });
}

void VulkanRenderer::completeFrame(uint32_t frameIndex)
{
    if (!frameResourceTracker_.complete(frameIndex)) {
        return;
    }
    const uint32_t completedBit = ~(1U << frameIndex);
    for (RetiredRenderResources& retired : retiredResources_) {
        retired.pendingFrameMask &= completedBit;
    }
    destroyCompletedRetirements();
}

void VulkanRenderer::applyPendingReconfiguration()
{
    const std::optional<RendererReconfigurationPlan> plan =
        reconfigurationQueue_.plan(
            swapchainRecreationRequested_);
    if (!plan) {
        return;
    }

    const uint64_t oldGeneration = activeResourceGeneration_;
    if (plan->rebuildRenderResources) {
        if (!activeResources_.swapchain->canRecreate()) {
            if (swapchainRecreationRequested_) {
                ++swapchainRecreationDeferrals_;
            }
            return;
        }

        RenderResourceSet replacement =
            createRenderResources(plan->settings);
        registerGameViewportTexture(replacement);
        RenderResourceSet retired = std::move(activeResources_);
        activeResources_ = std::move(replacement);
        activeSampleCount_ = sampleCountForMode(
            plan->settings.antiAliasing);
        ++activeResourceGeneration_;
        reconfigurationQueue_.commit(*plan);
        descriptorSync_.markAllUpdated();
        // Attachments and descriptors are shared across pipeline generations,
        // so the full replacement follows every currently submitted frame.
        retireResources(std::move(retired), pendingFrameMask());
        ++renderResourceReconfigurations_;
        if (swapchainRecreationRequested_) {
            ++swapchainRecreations_;
        }
        swapchainRecreationRequested_ = false;
        logRenderConfiguration();
        return;
    }

    std::unique_ptr<VulkanPipelineFactory> replacement =
        createPipelines(activeResources_, plan->settings);
    RenderResourceSet retired;
    retired.pipelines = std::move(activeResources_.pipelines);
    activeResources_.pipelines = std::move(replacement);
    ++activeResourceGeneration_;
    reconfigurationQueue_.commit(*plan);
    retireResources(
        std::move(retired),
        pendingFrameMaskForGeneration(oldGeneration));
    ++renderResourceReconfigurations_;
}

void VulkanRenderer::createFrameResources()
{
    std::array<VkCommandBuffer, maxFramesInFlight_> commandBuffers {};
    VkCommandBufferAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = deviceContext_.commandPool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };
    vkCheck(
        vkAllocateCommandBuffers(
            deviceContext_.device(),
            &allocateInfo,
            commandBuffers.data()),
        "vkAllocateCommandBuffers failed");

    VkSemaphoreCreateInfo semaphoreInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (size_t i = 0; i < frames_.size(); ++i) {
        frames_[i].commandBuffer = commandBuffers[i];
        vkCheck(
            vkCreateSemaphore(
                deviceContext_.device(),
                &semaphoreInfo,
                nullptr,
                &frames_[i].imageAvailable),
            "vkCreateSemaphore failed");
        vkCheck(
            vkCreateSemaphore(
                deviceContext_.device(),
                &semaphoreInfo,
                nullptr,
                &frames_[i].renderFinished),
            "vkCreateSemaphore failed");
        vkCheck(
            vkCreateFence(
                deviceContext_.device(),
                &fenceInfo,
                nullptr,
                &frames_[i].inFlight),
            "vkCreateFence failed");
    }
}

void VulkanRenderer::initializeDebugUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
        ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForVulkan(window_)) {
        throw std::runtime_error("ImGui_ImplSDL3_InitForVulkan failed");
    }

    const VkFormat colorAttachmentFormat =
        activeResources_.swapchain->colorFormat();
    VkPipelineRenderingCreateInfoKHR pipelineRendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorAttachmentFormat,
    };

    ImGui_ImplVulkan_InitInfo initInfo {};
    initInfo.ApiVersion = VK_API_VERSION_1_4;
    initInfo.Instance = deviceContext_.instance();
    initInfo.PhysicalDevice = deviceContext_.physicalDevice();
    initInfo.Device = deviceContext_.device();
    initInfo.QueueFamily = deviceContext_.queueFamilies().graphics;
    initInfo.Queue = deviceContext_.graphicsQueue();
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = std::max(
        2U, activeResources_.swapchain->imageCount());
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRendering;
    initInfo.UseDynamicRendering = true;
    initInfo.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }
    registerGameViewportTexture(activeResources_);
#endif
}

void VulkanRenderer::registerGameViewportTexture(
    RenderResourceSet& resources)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (resources.gameViewportTexture || !resources.swapchain ||
        !ImGui::GetCurrentContext() ||
        ImGui::GetIO().BackendRendererUserData == nullptr) {
        return;
    }
    resources.gameViewportTexture = ImGui_ImplVulkan_AddTexture(
        resources.swapchain->sceneColorView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#else
    (void)resources;
#endif
}

void VulkanRenderer::releaseGameViewportTexture(
    RenderResourceSet& resources)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (resources.gameViewportTexture && ImGui::GetCurrentContext() &&
        ImGui::GetIO().BackendRendererUserData != nullptr) {
        ImGui_ImplVulkan_RemoveTexture(resources.gameViewportTexture);
    }
#endif
    resources.gameViewportTexture = VK_NULL_HANDLE;
}

void VulkanRenderer::shutdownDebugUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    // Thumbnail descriptor sets are allocated by the ImGui Vulkan backend.
    // Release them while its descriptor pool and backend state still exist.
    thumbnailPass_.destroy();

    if (ImGui::GetCurrentContext()) {
        releaseGameViewportTexture(activeResources_);
        for (RetiredRenderResources& retired : retiredResources_) {
            releaseGameViewportTexture(retired.resources);
        }
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
#endif
}

void VulkanRenderer::logRenderConfiguration() const
{
    const VkExtent2D extent =
        activeResources_.swapchain->extent();
    const VkExtent2D renderExtent =
        activeResources_.swapchain->renderExtent();
    const uint64_t pixels =
        static_cast<uint64_t>(renderExtent.width) * renderExtent.height;
    const uint64_t samplePixels = pixels * sampleCountValue();
    log::info(log::Category::Rendering)
        << "Vulkan swapchain: " << extent.width << 'x' << extent.height
        << ", " << activeResources_.swapchain->imageCount()
        << " images, "
        << presentModeName() << ", " << sampleCountValue()
        << "x MSAA; scene " << renderExtent.width << 'x'
        << renderExtent.height << " at "
        << activeResources_.swapchain->renderScalePercent()
        << "% ("
        << samplePixels / 1'000'000.0
        << " M sample-pixels)";
}

VkSampleCountFlagBits VulkanRenderer::sampleCountForMode(AntiAliasingMode mode) const
{
    VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT;
    switch (mode) {
    case AntiAliasingMode::None:
        requested = VK_SAMPLE_COUNT_1_BIT;
        break;
    case AntiAliasingMode::Msaa2x:
        requested = VK_SAMPLE_COUNT_2_BIT;
        break;
    case AntiAliasingMode::Msaa4x:
        requested = VK_SAMPLE_COUNT_4_BIT;
        break;
    case AntiAliasingMode::Msaa8x:
        requested = VK_SAMPLE_COUNT_8_BIT;
        break;
    }

    return deviceContext_.supportedSampleCount(requested);
}

uint32_t VulkanRenderer::sampleCountValue() const
{
    switch (activeSampleCount_) {
    case VK_SAMPLE_COUNT_2_BIT:
        return 2;
    case VK_SAMPLE_COUNT_4_BIT:
        return 4;
    case VK_SAMPLE_COUNT_8_BIT:
        return 8;
    default:
        return 1;
    }
}

} // namespace sokoban
