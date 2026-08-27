#include "engine/render/VulkanRenderer.hpp"

#include "engine/Log.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
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
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
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

// Orientation of the turn origin->first->second. Named apart from the shared
// cross2D because it takes three points rather than two vectors.
float turn(Vec2 origin, Vec2 first, Vec2 second)
{
    return cross2D(first - origin, second - origin);
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
               turn(hull[count - 2], hull[count - 1], candidate) <= 0.0f) {
            --count;
        }
        hull[count++] = candidate;
    }
    const std::size_t lowerCount = count;
    for (std::size_t index = points.size() - 1; index-- > 0;) {
        const Vec2 candidate = points[index];
        while (count > lowerCount &&
               turn(hull[count - 2], hull[count - 1], candidate) <= 0.0f) {
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
        if (turn(hull[index], hull[(index + 1) % count], point) <
            -edgeTolerancePixels) {
            return false;
        }
    }
    return true;
}

} // namespace

SwapchainPresentSemaphores::SwapchainPresentSemaphores(
    VkDevice device,
    uint32_t imageCount)
    : device_(device)
{
    const VkSemaphoreCreateInfo semaphoreInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    semaphores_.reserve(imageCount);
    try {
        for (uint32_t index = 0; index < imageCount; ++index) {
            VkSemaphore semaphore = VK_NULL_HANDLE;
            vkCheck(
                vkCreateSemaphore(
                    device_, &semaphoreInfo, nullptr, &semaphore),
                "vkCreateSemaphore render finished failed");
            semaphores_.push_back(semaphore);
            vulkanDebug::setObjectName(
                device_,
                VK_OBJECT_TYPE_SEMAPHORE,
                semaphore,
                "Swapchain image " + std::to_string(index) +
                    " render finished");
        }
    } catch (...) {
        destroy();
        throw;
    }
}

SwapchainPresentSemaphores::~SwapchainPresentSemaphores()
{
    destroy();
}

SwapchainPresentSemaphores::SwapchainPresentSemaphores(
    SwapchainPresentSemaphores&& other) noexcept
    : device_(other.device_)
    , semaphores_(std::move(other.semaphores_))
{
    other.device_ = VK_NULL_HANDLE;
    other.semaphores_.clear();
}

SwapchainPresentSemaphores& SwapchainPresentSemaphores::operator=(
    SwapchainPresentSemaphores&& other) noexcept
{
    if (this != &other) {
        destroy();
        device_ = other.device_;
        semaphores_ = std::move(other.semaphores_);
        other.device_ = VK_NULL_HANDLE;
        other.semaphores_.clear();
    }
    return *this;
}

VkSemaphore SwapchainPresentSemaphores::forImage(uint32_t imageIndex) const
{
    // The caller only ever passes an index vkAcquireNextImageKHR produced for
    // the swapchain this set was sized from, so an out-of-range index is a
    // programming error rather than a runtime condition.
    if (imageIndex >= semaphores_.size()) {
        throw std::runtime_error(
            "Swapchain image index has no render-finished semaphore");
    }
    return semaphores_[imageIndex];
}

void SwapchainPresentSemaphores::destroy() noexcept
{
    if (device_) {
        for (VkSemaphore semaphore : semaphores_) {
            if (semaphore) {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
    }
    semaphores_.clear();
    device_ = VK_NULL_HANDLE;
}

VulkanRenderer::VulkanRenderer(
    SDL_Window* window,
    std::filesystem::path assetRoot,
    std::filesystem::path pipelineCachePath,
    const AssetManifest& manifest,
    const FontAtlas& uiFont,
    AntiAliasingMode antiAliasingMode,
    int renderScalePercent,
    PresentationPolicy presentationPolicy)
    : window_(window)
    , assetRoot_(std::move(assetRoot))
    , deviceContext_(window, manifest.textures().size())
    , reconfigurationQueue_({
          .antiAliasing = antiAliasingMode,
          .renderScalePercent = renderScalePercent,
          .wireframe = false,
      })
    , presentationPolicy_(presentationPolicy)
{
    pipelineCache_.create(
        deviceContext_.device(),
        deviceContext_.physicalDeviceProperties(),
        std::move(pipelineCachePath));
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
        assetRoot_, manifest,
        deviceContext_.textureDescriptorCapacity(),
        deviceContext_.maxSamplerAnisotropy());
    activeResources_ = createRenderResources(
        reconfigurationQueue_.active());
    descriptorSync_.markAllUpdated();
    logRenderConfiguration();
    if (deviceContext_.graphicsTimestampsSupported()) {
        gpuProfiler_.create(
            deviceContext_.device(),
            deviceContext_.timestampPeriodNanoseconds(),
            deviceContext_.graphicsTimestampValidBits(),
            maxFramesInFlight_);
    }
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
    pipelineCache_.persist();

    shutdownDebugUi();

    for (auto& frame : frames_) {
        if (frame.imageAvailable) {
            vkDestroySemaphore(
                deviceContext_.device(),
                frame.imageAvailable,
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
    gpuProfiler_.destroy();
    pipelineCache_.destroy();
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
    if (fatalFailure_) {
        return;
    }
    const auto cpuFrameStart = std::chrono::steady_clock::now();
    try {
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
    for (const UiDrawCommand& command : uiDrawData.commands) {
        frameAssetRequirements_.requireTexture(command.texture);
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
    gpuProfiler_.collectCompletedFrame(currentFrame_);
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
    modelResources_.beginAnimationFrame(currentFrame_);
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
            .device = deviceContext_.device(),
            .gpuProfiler = gpuProfiler_,
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
            .modelBackfaceCulling = modelBackfaceCulling_,
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
    lastStats_.gpuTimestampsSupported = gpuProfiler_.supported();
    const FrameTimeSummary gpuTiming = gpuProfiler_.frameTimeSummary();
    if (gpuTiming.available()) {
        lastStats_.gpuFrameTimingAvailable = true;
        lastStats_.gpuFrameTimingSamples = gpuTiming.sampleCount;
        lastStats_.gpuFrameMilliseconds = gpuTiming.latestMilliseconds;
        lastStats_.gpuFrameAverageMilliseconds = gpuTiming.averageMilliseconds;
        lastStats_.gpuFrameP95Milliseconds = gpuTiming.p95Milliseconds;
        lastStats_.gpuFrameMaximumMilliseconds = gpuTiming.maximumMilliseconds;
    }

    VkSemaphoreSubmitInfo waitSemaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAvailable,
        // The shipping path first writes the acquired image during the
        // upscale blit, while the developer workspace first uses it as a
        // colour attachment. Gate both possible first accesses.
        .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT |
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkCommandBufferSubmitInfo commandBuffer {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };

    // Signalled per swapchain image, not per frame slot: the present that
    // consumes it is bound to the image, and nothing here proves an earlier
    // present on a different image has stopped waiting.
    const VkSemaphore renderFinished =
        activeResources_.presentSemaphores.forImage(imageIndex);

    VkSemaphoreSubmitInfo signalSemaphore {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = renderFinished,
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
    gpuProfiler_.markSubmitted(currentFrame_);

    const VkResult presented = activeResources_.swapchain->present(
        deviceContext_.presentQueue(),
        renderFinished,
        imageIndex);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        swapchainRecreationRequested_ = true;
    } else {
        vkCheck(presented, "vkQueuePresentKHR failed");
    }

    cpuFrameTimeTelemetry_.record(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cpuFrameStart).count());
    const FrameTimeSummary cpuTiming = cpuFrameTimeTelemetry_.summary();
    lastStats_.cpuFrameTimingAvailable = cpuTiming.available();
    lastStats_.cpuFrameTimingSamples = cpuTiming.sampleCount;
    lastStats_.cpuFrameMilliseconds = cpuTiming.latestMilliseconds;
    lastStats_.cpuFrameAverageMilliseconds = cpuTiming.averageMilliseconds;
    lastStats_.cpuFrameP95Milliseconds = cpuTiming.p95Milliseconds;
    lastStats_.cpuFrameMaximumMilliseconds = cpuTiming.maximumMilliseconds;

    currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight_;
    applyPendingReconfiguration();
    } catch (const VulkanError& error) {
        if (const std::optional<VulkanFailure> failure =
                vulkanFailureForResult(error.result())) {
            reportFatalFailure(*failure);
            return;
        }
        throw;
    }
}

void VulkanRenderer::preloadAssets(const RenderAssetRequirements& requirements)
{
    modelResources_.requestAssets(requirements, AssetLoadPriority::Prefetch);
}

void VulkanRenderer::cancelQueuedAssetPrefetches()
{
    modelResources_.cancelQueuedPrefetches();
}

void VulkanRenderer::ensureAssets(const RenderAssetRequirements& requirements)
{
    modelResources_.requestAssets(requirements, AssetLoadPriority::Visible);
}

void VulkanRenderer::waitForAssets(const RenderAssetRequirements& requirements)
{
    if (modelResources_.waitForAssets(requirements)) {
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
        // The display image, not the scene target: it is tonemapped and in a
        // format PngWriter understands.
        activeResources_.swapchain->displayColorImage(),
        activeResources_.swapchain->colorFormat(),
        // Both of the things this assumes hold only with the developer
        // workspace hidden, which is how the thumbnail bake drives the
        // renderer: the upscale blit leaves the display image a transfer
        // source, and the game's UI composites onto the swapchain rather
        // than onto this image. With the workspace visible the image is in
        // SHADER_READ_ONLY_OPTIMAL and has the UI drawn on it.
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        rect.offset,
        rect.extent);
}

void VulkanRenderer::syncManifestTextures()
{
    if (modelResources_.syncManifestTextures()) {
        // Reserved descriptor slots are padded with the fallback texture, so
        // the new slot already has something valid bound; the
        // rewrite is what points it at the real image once it publishes.
        descriptorSync_.resourcesChanged();
    }
}

uint32_t VulkanRenderer::textureDescriptorCapacity() const
{
    return deviceContext_.textureDescriptorCapacity();
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

std::string_view VulkanRenderer::fatalFailureMessage() const
{
    return fatalFailure_
        ? vulkanFailureMessage(*fatalFailure_)
        : std::string_view {};
}

void VulkanRenderer::reportFatalFailure(VulkanFailure failure) noexcept
{
    if (fatalFailure_) {
        return;
    }
    fatalFailure_ = failure;
    log::error(log::Category::Rendering)
        << vulkanFailureTitle(failure) << ": "
        << vulkanFailureMessage(failure);
    showVulkanFailureDialog(window_, failure);
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
    reconfigurationQueue_.requestWireframe(
        enabled && deviceContext_.wireframeSupported());
}

bool VulkanRenderer::wireframeSupported() const
{
    return deviceContext_.wireframeSupported();
}

bool VulkanRenderer::modelBackfaceCullingEnabled() const
{
    return modelBackfaceCulling_;
}

void VulkanRenderer::setModelBackfaceCullingEnabled(bool enabled)
{
    // Pure dynamic state: no pipeline rebuild, no resource replacement, so
    // this does not go through the reconfiguration queue the way wireframe
    // does. The next recorded frame picks it up.
    modelBackfaceCulling_ = enabled;
}

bool VulkanRenderer::opaqueFrontToBackSortEnabled() const
{
    return scenePreparer_.opaqueFrontToBackSort();
}

void VulkanRenderer::setOpaqueFrontToBackSortEnabled(bool enabled)
{
    // Read by prepareFrame, which runs on whichever thread builds the frame.
    // A torn read is not possible for a bool and the worst case is that one
    // frame sorts the old way, which is exactly what this toggle is for.
    scenePreparer_.setOpaqueFrontToBackSort(enabled);
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

void VulkanRenderer::setPresentationPolicy(PresentationPolicy policy)
{
    if (presentationPolicy_ == policy) {
        return;
    }
    presentationPolicy_ = policy;
    // Present modes are selected only while creating a swapchain. Queue a
    // normal fence-safe replacement instead of mutating a live swapchain.
    swapchainRecreationRequested_ = true;
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
        .pointShadows = {
            .sampler = shadowPass_.pointSampler(),
            .imageView = shadowPass_.pointImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        },
        .sceneColor = {
            .sampler = resources.swapchain->sceneColorSampler(),
            .imageView = resources.swapchain->sceneColorView(),
        },
        // The scene target itself, for the tonemap pass. It is a colour
        // attachment for most of the frame; beginTonemap is what puts it in
        // the shader-read layout this binding declares.
        .sceneHdrColor = {
            .sampler = resources.swapchain->sceneColorSampler(),
            .imageView = resources.swapchain->resolvedColorView(),
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
        .skinning = modelResources_.skinningBuffer(),
        .drawInstances = modelResources_.drawInstanceBuffer(),
        .materials = modelResources_.materialBuffer(),
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
        presentationPolicy_,
        activeResources_.swapchain
            ? activeResources_.swapchain->handle()
            : VK_NULL_HANDLE);
    // Must follow swapchain creation: the count comes from the images the
    // driver actually handed back, not from the requested minimum.
    resources.presentSemaphores = SwapchainPresentSemaphores(
        deviceContext_.device(), resources.swapchain->imageCount());
    resources.ssaoPass = std::make_unique<VulkanSsaoPass>();
    resources.ssaoPass->create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        resources.swapchain->renderExtent());
    resources.sceneDescriptors =
        std::make_unique<VulkanSceneDescriptors>();
    resources.sceneDescriptors->create(
        deviceContext_.physicalDevice(),
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
        .pipelineCache = pipelineCache_.handle(),
        .assetRoot = assetRoot_,
        .descriptorSetLayout =
            resources.sceneDescriptors->layout(),
        .textureDescriptorSetLayout =
            resources.sceneDescriptors->textureLayout(),
        .colorFormat = resources.swapchain->colorFormat(),
        .sceneColorFormat = resources.swapchain->sceneColorFormat(),
        .depthFormat = depthFormat_,
        .shadowFormat = shadowFormat_,
        .sampleCount =
            sampleCountForMode(settings.antiAliasing),
        .wireframe = settings.wireframe && deviceContext_.wireframeSupported(),
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
        const std::string frameLabel = "Frame " + std::to_string(i);
        vulkanDebug::setObjectName(
            deviceContext_.device(),
            VK_OBJECT_TYPE_COMMAND_BUFFER,
            frames_[i].commandBuffer,
            frameLabel + " command buffer");
        vkCheck(
            vkCreateSemaphore(
                deviceContext_.device(),
                &semaphoreInfo,
                nullptr,
                &frames_[i].imageAvailable),
            "vkCreateSemaphore failed");
        vulkanDebug::setObjectName(
            deviceContext_.device(),
            VK_OBJECT_TYPE_SEMAPHORE,
            frames_[i].imageAvailable,
            frameLabel + " image available");
        vkCheck(
            vkCreateFence(
                deviceContext_.device(),
                &fenceInfo,
                nullptr,
                &frames_[i].inFlight),
            "vkCreateFence failed");
        vulkanDebug::setObjectName(
            deviceContext_.device(),
            VK_OBJECT_TYPE_FENCE,
            frames_[i].inFlight,
            frameLabel + " in flight fence");
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
    initInfo.ApiVersion = VK_API_VERSION_1_3;
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
        resources.swapchain->displayColorView(),
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
