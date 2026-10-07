#include "engine/render/VulkanRenderer.hpp"

#include "engine/Log.hpp"
#include "engine/ProcessMemory.hpp"
#include "engine/Profiler.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/GroundRimGeometry.hpp"
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
// Deliberately fatal rather than defaulting to 0. This flag decides whether
// Application and DebugUi declare some of their members, so a translation unit
// that quietly assumed a value would disagree with the rest of the program
// about those class layouts - and link anyway. CMake defines it PUBLIC on
// sokoban_core, so anything linking a Sokoban library already has it.
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

namespace sokoban {
namespace {

// Resolve one recipe for all ground caps and bodies. Waiting for the complete
// participating cohort avoids mismatched bevel heights at shared tile edges.
// Recheck after asset maintenance, when publication or eviction can change it.
bool resolveGroundRims(RenderFrameData& frame, const VulkanModelResources& models,
    uint32_t frameIndex, bool allowRims = true)
{
    const float width = frame.requestedGroundRimWidth;
    const float depth = frame.requestedGroundRimDepth;
    bool supported = allowRims &&
        groundRimProfileValid({ .width = width, .depth = depth });
    if (supported) {
        for (const auto& tile : frame.tiles) {
            if (tile.groundGeometryEligible &&
                (!models.modelReady(tile.model) ||
                    !models.meshForTile(tile, frameIndex).groundGeometryVariant)) {
                supported = false;
                break;
            }
        }
    }
    bool changed = false;
    for (auto& tile : frame.tiles) {
        const bool boundary = tile.groundGeometryEligible &&
            (tile.groundRimSides != 0 || tile.groundRimConcaveCorners != 0);
        const float resolvedWidth = supported && boundary ? width : 0.0f;
        const float resolvedDepth = supported && boundary ? depth : 0.0f;
        changed |= tile.groundRimWidth != resolvedWidth || tile.groundRimDepth != resolvedDepth;
        tile.groundRimWidth = resolvedWidth;
        tile.groundRimDepth = resolvedDepth;
    }
    return changed;
}

double elapsedMilliseconds(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

RenderPhaseTiming renderPhaseTiming(const FrameTimeSummary& summary)
{
    return {
        .available = summary.available(),
        .samples = summary.sampleCount,
        .latestMilliseconds = summary.latestMilliseconds,
        .averageMilliseconds = summary.averageMilliseconds,
        .minimumMilliseconds = summary.minimumMilliseconds,
        .medianMilliseconds = summary.medianMilliseconds,
        .p95Milliseconds = summary.p95Milliseconds,
        .p99Milliseconds = summary.p99Milliseconds,
        .maximumMilliseconds = summary.maximumMilliseconds,
        .standardDeviationMilliseconds =
            summary.standardDeviationMilliseconds,
    };
}

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

template <typename BackgroundFn, typename ForegroundFn>
void runConcurrently(
    TaskSystem& tasks,
    BackgroundFn background,
    ForegroundFn foreground)
{
    // parallelFor reuses its bounded coordination slot; enqueue/future builds
    // an owning task and shared completion state on every preview frame.
    tasks.parallelFor(2, 1, [&](std::size_t begin, std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
            if (index == 0) background();
            else foreground();
        }
    });
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
    PresentationPolicy presentationPolicy,
    AssetLoadingBudget assetLoadingBudget,
    bool parallelScenePreparationEnabled,
    bool pointShadowOptimizationsEnabled,
    bool recorderScratchReuseEnabled,
    bool showFailureDialogs,
    bool waterCellCacheEnabled)
    : window_(window)
    , assetRoot_(std::move(assetRoot))
    , runtimeTextureCatalog_(
          collectRuntimeTextureCatalog(assetRoot_, manifest))
    , deviceContext_(window, runtimeTextureCatalog_.textures().size())
    , reconfigurationQueue_({
          .antiAliasing = antiAliasingMode,
          .renderScalePercent = renderScalePercent,
          .wireframe = false,
      })
    , presentationPolicy_(presentationPolicy)
    , parallelScenePreparationEnabled_(parallelScenePreparationEnabled)
    , pointShadowOptimizationsEnabled_(pointShadowOptimizationsEnabled)
    , recorderScratchReuseEnabled_(recorderScratchReuseEnabled)
    , showFailureDialogs_(showFailureDialogs)
    , waterCellCacheEnabled_(waterCellCacheEnabled)
{
    using StartupClock = std::chrono::steady_clock;
    const StartupClock::time_point rendererSetupStarted = StartupClock::now();
    auto phaseStarted = rendererSetupStarted;
    const auto finishPhase = [&phaseStarted]() {
        const auto now = StartupClock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - phaseStarted).count();
        phaseStarted = now;
        return elapsed;
    };

    scenePreparer_.setPointShadowRangeCulling(
        pointShadowOptimizationsEnabled_);
    previewScenePreparer_.setPointShadowRangeCulling(
        pointShadowOptimizationsEnabled_);
    sceneRecorder_.setPointShadowCacheEnabled(
        pointShadowOptimizationsEnabled_);
    sceneRecorder_.setScratchReuseEnabled(recorderScratchReuseEnabled_);
#if SOKOBAN_ENABLE_DEBUG_UI
    sceneRecorder_.setDebugLabelFont(uiFont);
#endif
    depthFormat_ = deviceContext_.sceneDepthFormat();
    pipelineCache_.create(
        deviceContext_.device(),
        deviceContext_.physicalDeviceProperties(),
        std::move(pipelineCachePath));
    const auto pipelineCacheMicroseconds = finishPhase();
    wireframeLineWidth_ = std::clamp(
        wireframeLineWidth_,
        1.0f,
        deviceContext_.wireframeLineWidthRange()[1]);
    // The default MSAA mode is a request; drop to what the device supports.
    activeSampleCount_ = sampleCountForMode(antiAliasingMode);
    std::int64_t shadowResourcesMicroseconds = 0;
    std::int64_t uiResourcesMicroseconds = 0;
    std::int64_t modelResourcesMicroseconds = 0;
    std::future<void> startupPrerequisites = std::async(
        std::launch::async,
        [this, &manifest, &uiFont, assetLoadingBudget,
            &shadowResourcesMicroseconds, &uiResourcesMicroseconds,
            &modelResourcesMicroseconds] {
            const auto started = StartupClock::now();
            auto resourceStarted = started;
            shadowPass_.create(
                deviceContext_.memoryAllocator(),
                deviceContext_.device(),
                shadowFormat_);
            auto now = StartupClock::now();
            shadowResourcesMicroseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - resourceStarted).count();
            resourceStarted = now;
            uiResources_.create(
                deviceContext_.memoryAllocator(),
                deviceContext_.device(),
                deviceContext_.commandPool(),
                deviceContext_.graphicsQueue(),
                uiFont,
                loadRgbaImage(assetRoot_ / config::titleBackgroundPath));
            now = StartupClock::now();
            uiResourcesMicroseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - resourceStarted).count();
            resourceStarted = now;
            modelResources_.create(
                deviceContext_.physicalDevice(),
                deviceContext_.memoryAllocator(),
                deviceContext_.device(),
                deviceContext_.commandPool(),
                deviceContext_.graphicsQueue(),
                assetRoot_, manifest, runtimeTextureCatalog_,
                deviceContext_.textureDescriptorCapacity(),
                deviceContext_.maxSamplerAnisotropy(),
                assetLoadingBudget);
            modelResourcesMicroseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    StartupClock::now() - resourceStarted).count();
        });
    activeResources_ = createRenderResources(
        reconfigurationQueue_.active(), &startupPrerequisites);
    const auto renderResourcesMicroseconds = finishPhase();
    descriptorSync_.markAllUpdated();
    logRenderConfiguration();
    if (deviceContext_.graphicsTimestampsSupported()) {
        gpuProfiler_.create(
            deviceContext_.device(),
            deviceContext_.timestampPeriodNanoseconds(),
            deviceContext_.graphicsTimestampValidBits(),
            maxFramesInFlight_);
    }
    const auto gpuProfilerMicroseconds = finishPhase();
    createFrameResources();
    const auto frameResourcesMicroseconds = finishPhase();
    initializeDebugUi();
    const auto debugUiMicroseconds = finishPhase();
#if SOKOBAN_ENABLE_DEBUG_UI
    // After initializeDebugUi: registering a thumbnail with ImGui needs the
    // Vulkan backend to exist. Failure here only costs the editor its
    // thumbnails, so it is not fatal.
    thumbnailPass_.create(
        deviceContext_.memoryAllocator(),
        deviceContext_.device(),
        deviceContext_.commandPool(),
        deviceContext_.graphicsQueue(),
        assetRoot_);
#endif
    const auto thumbnailMicroseconds = finishPhase();
    const auto totalMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            StartupClock::now() - rendererSetupStarted).count();
    log::info(log::Category::Rendering)
        << "Renderer startup phases (us): pipeline-cache="
        << pipelineCacheMicroseconds
        << " shadow-resources=" << shadowResourcesMicroseconds
        << " ui-resources=" << uiResourcesMicroseconds
        << " model-resources=" << modelResourcesMicroseconds
        << " render-resources=" << renderResourcesMicroseconds
        << " gpu-profiler=" << gpuProfilerMicroseconds
        << " frame-resources=" << frameResourcesMicroseconds
        << " debug-ui=" << debugUiMicroseconds
        << " thumbnails=" << thumbnailMicroseconds
        << " total=" << totalMicroseconds;
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
    SOKOBAN_PROFILE_SCOPE("Renderer.Prepare frame");
    const auto preparationStart = std::chrono::steady_clock::now();
    const VkExtent2D extent =
        activeResources_.swapchain->renderExtent();
    std::shared_ptr<PreparedFrameScratch> scratch =
        preparedFrameScratch_.acquire();
    scratch->frameData = std::move(frameData);
    scratch->generation = nextPreparedFrameGeneration_++;
    scratch->groundRimBudgetFallback = false;
    scratch->previewFrameData = std::move(previewFrameData);
    resolveGroundRims(scratch->frameData, modelResources_, currentFrame_);
    if (scratch->previewFrameData) {
        resolveGroundRims(*scratch->previewFrameData, modelResources_, currentFrame_);
    }
    if (!scratch->previewFrameData) {
        scratch->previewScene.reset();
    } else if (!scratch->previewScene) {
        scratch->previewScene.emplace();
    }
    const Vec2 mainExtent {
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
    };
    if (scratch->previewFrameData && parallelScenePreparationEnabled_) {
        const Vec2 previewExtent {
            mainExtent.x * 0.75f,
            mainExtent.y * 0.75f,
        };
        // Whole-scene preparation is the coarsest useful split. The main and
        // preview preparers own separate retained caches and write separate
        // frame-scratch subobjects, so no ordering or cache synchronization is
        // required between them.
        runConcurrently(
            framePreparationTasks_,
            [this, &scratch, previewExtent] {
                previewScenePreparer_.prepare(
                    *scratch->previewFrameData,
                    previewExtent,
                    *scratch->previewScene);
            },
            [this, &scratch, mainExtent] {
                scenePreparer_.prepare(
                    scratch->frameData,
                    mainExtent,
                    scratch->scene);
            });
    } else if (scratch->previewFrameData) {
        scenePreparer_.prepare(
            scratch->frameData,
            mainExtent,
            scratch->scene);
        previewScenePreparer_.prepare(
            *scratch->previewFrameData,
            {
                mainExtent.x * 0.75f,
                mainExtent.y * 0.75f,
            },
            *scratch->previewScene);
    } else {
        // With one scene, overlap its independent shadow/particle lists with
        // ordered main-scene projection, culling, and sorting.
        scenePreparer_.prepare(
            scratch->frameData,
            mainExtent,
            scratch->scene,
            parallelScenePreparationEnabled_
                ? &framePreparationTasks_
                : nullptr);
    }
    applyGroundRimBudgetFallback(*scratch, 0);
    scenePreparationTimeTelemetry_.record(
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - preparationStart)
            .count());

    PreparedFrame frame;
    frame.levelWidth = scratch->frameData.levelWidth;
    frame.levelHeight = scratch->frameData.levelHeight;
    frame.cameraPosition = scratch->scene.isoLayout.cameraPosition;
    frame.cameraForward = scratch->scene.isoLayout.cameraForward;
    frame.cameraVerticalFovDegrees = radiansToDegrees(
        2.0f * std::atan(
            1.0f / std::max(
                scratch->scene.isoLayout.focalLength *
                    scratch->scene.isoLayout.fitScale,
                0.001f)));
    frame.cameraValid = scratch->frameData.viewMode ==
            RenderViewMode::Isometric3D &&
        lengthSquared(frame.cameraForward) > normalizeEpsilonSquared;
    frame.generation = scratch->generation;
    frame.scratch = std::move(scratch);
    return frame;
}

void VulkanRenderer::applyGroundRimBudgetFallback(
    const PreparedFrameScratch& prepared, std::size_t uiDrawCount)
{
    if (prepared.groundRimBudgetFallback ||
        groundRimDrawInstanceBudgetFits(
            ordinarySceneDrawInstanceReserve(prepared.frameData, prepared.scene),
            prepared.previewFrameData && prepared.previewScene
                ? ordinarySceneDrawInstanceReserve(
                    *prepared.previewFrameData, *prepared.previewScene) : 0,
            uiDrawCount, drawInstanceDiscardSlot)) {
        return;
    }
    // Preserve the existing draw buffer and flat-ground capacity. Subdivided
    // caps must never make an otherwise drawable view lose tiles or UI.
    const bool mainChanged = resolveGroundRims(
        prepared.frameData, modelResources_, currentFrame_, false);
    const bool previewChanged = prepared.previewFrameData && resolveGroundRims(
        *prepared.previewFrameData, modelResources_, currentFrame_, false);
    if (!mainChanged && !previewChanged) return;
    prepared.groundRimBudgetFallback = true;
    if (mainChanged) {
        scenePreparer_.prepare(prepared.frameData,
            prepared.scene.renderExtent, prepared.scene);
    }
    if (previewChanged) {
        previewScenePreparer_.prepare(*prepared.previewFrameData,
            prepared.previewScene->renderExtent, *prepared.previewScene);
    }
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

// Between the fence wait and image acquisition: retire what the completed
// frame owned, publish whatever finished loading, and bring the descriptor
// heap up to date if that changed it. Nothing here touches the swapchain.
void VulkanRenderer::runAssetMaintenance(
    const PreparedFrameScratch& prepared, const RenderFrameData& frameData)
{
    SOKOBAN_PROFILE_SCOPE("Renderer.Asset maintenance");
    const auto assetMaintenanceStart = std::chrono::steady_clock::now();
    completeFrame(currentFrame_);
    gpuProfiler_.collectCompletedFrame(currentFrame_);
    modelResources_.retireCompletedUploads();
    const auto assetPublicationStart = std::chrono::steady_clock::now();
    const VulkanModelResources::PublicationResult publication =
        modelResources_.publishReadyAssets(1, pendingFrameMask());
    const double assetPublicationMilliseconds =
        elapsedMilliseconds(assetPublicationStart);
    if (publication.publications != 0) {
        assetPublicationEventTimeTelemetry_.record(
            assetPublicationMilliseconds);
        assetPublications_ += publication.publications;
        ++assetPublicationFrames_;
    }
    if (publication.descriptorsChanged) {
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
    assetMaintenanceTimeTelemetry_.record(
        elapsedMilliseconds(assetMaintenanceStart));
}

// The submit and the present, with the two semaphores that order them, and
// the telemetry that times the pair.
//
// The signal semaphore is per swapchain image rather than per frame slot,
// which is the subtlety this keeps in one place: the present that consumes it
// is bound to the image, and nothing here proves an earlier present on a
// different image has stopped waiting.
void VulkanRenderer::submitAndPresent(
    const FrameResources& frame, uint32_t imageIndex)
{
    SOKOBAN_PROFILE_SCOPE("Renderer.Submit and present");
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

    const auto submitPresentStart = std::chrono::steady_clock::now();
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
    submitPresentTimeTelemetry_.record(
        elapsedMilliseconds(submitPresentStart));
}

void VulkanRenderer::drawFrame(
    const PreparedFrame& preparedFrame,
    const UiDrawData& uiDrawData,
    bool developerWorkspaceVisible)
{
    SOKOBAN_PROFILE_SCOPE("Renderer.Draw frame");
    if (fatalFailure_) {
        return;
    }
    const auto cpuFrameStart = std::chrono::steady_clock::now();
    try {
    const auto assetSchedulingStart = std::chrono::steady_clock::now();
    const PreparedFrameScratch& prepared = resolvePreparedFrame(preparedFrame);
    RenderFrameData& frameData = prepared.frameData;
    renderAssetRequirementsForFrame(frameData, frameAssetRequirements_);
    if (prepared.previewFrameData) {
        renderAssetRequirementsForFrame(
            *prepared.previewFrameData, previewAssetRequirements_);
        frameAssetRequirements_.merge(previewAssetRequirements_);
    }
    for (const UiDrawCommand& command : uiDrawData.commands) {
        frameAssetRequirements_.requireTexture(command.texture);
    }
    ensureAssets(frameAssetRequirements_);
    assetSchedulingTimeTelemetry_.record(
        elapsedMilliseconds(assetSchedulingStart));

#if SOKOBAN_ENABLE_DEBUG_UI
    // Finish the ImGui frame even when swapchain acquisition is out of date
    // and this render frame has to be skipped during a window-mode change.
    ImGui::Render();
#endif

    auto& frame = frames_[currentFrame_];
    const auto frameFenceWaitStart = std::chrono::steady_clock::now();
    {
        SOKOBAN_PROFILE_SCOPE("Renderer.Wait frame fence");
        vkCheck(
            vkWaitForFences(
                deviceContext_.device(),
                1,
                &frame.inFlight,
                VK_TRUE,
                UINT64_MAX),
            "vkWaitForFences failed");
    }
    frameFenceWaitTimeTelemetry_.record(
        elapsedMilliseconds(frameFenceWaitStart));

    runAssetMaintenance(prepared, frameData);
    const auto sceneExtent = activeResources_.swapchain->renderExtent();
    if (resolveGroundRims(frameData, modelResources_, currentFrame_,
            !prepared.groundRimBudgetFallback)) {
        scenePreparer_.prepare(frameData,
            { static_cast<float>(sceneExtent.width), static_cast<float>(sceneExtent.height) },
            prepared.scene);
    }
    if (prepared.previewFrameData &&
        resolveGroundRims(*prepared.previewFrameData, modelResources_, currentFrame_,
            !prepared.groundRimBudgetFallback)) {
        previewScenePreparer_.prepare(*prepared.previewFrameData,
            { static_cast<float>(sceneExtent.width) * 0.75f,
                static_cast<float>(sceneExtent.height) * 0.75f },
            *prepared.previewScene);
    }
    applyGroundRimBudgetFallback(prepared, uiDrawData.commands.size());

    uint32_t imageIndex = 0;
    const auto imageAcquisitionStart = std::chrono::steady_clock::now();
    VkResult acquired = VK_SUCCESS;
    {
        SOKOBAN_PROFILE_SCOPE("Renderer.Acquire swapchain image");
        acquired = activeResources_.swapchain->acquire(
            frame.imageAvailable, imageIndex);
    }
    imageAcquisitionTimeTelemetry_.record(
        elapsedMilliseconds(imageAcquisitionStart));
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

    const auto commandRecordingStart = std::chrono::steady_clock::now();
    vkCheck(
        vkResetFences(
            deviceContext_.device(), 1, &frame.inFlight),
        "vkResetFences failed");
    vkCheck(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");

    if (prepared.previewFrameData.has_value() !=
        prepared.previewScene.has_value()) {
        throw std::logic_error(
            "Prepared preview frame and scene must be published together");
    }
    std::optional<VulkanSceneRecorder::SceneInput> previewInput;
    if (prepared.previewFrameData && prepared.previewScene) {
        previewInput.emplace(
            VulkanSceneRecorder::SceneInput {
                *prepared.previewFrameData,
                *prepared.previewScene,
            });
    }
    lastStats_ = sceneRecorder_.record(
        {
            .device = deviceContext_.device(),
            .gpuProfiler = gpuProfiler_,
            .swapchain = *activeResources_.swapchain,
            .shadowPass = shadowPass_,
            .ssaoPass = *activeResources_.ssaoPass,
            .atmospherePass = *activeResources_.atmospherePass,
            .bloomPass = *activeResources_.bloomPass,
            .waterCellCache = *activeResources_.waterCellCache,
            .sceneDescriptors =
                *activeResources_.sceneDescriptors,
            .pipelines = *activeResources_.pipelines,
            .modelResources = modelResources_,
            .uiResources = uiResources_,
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
        {
            .commandBuffer = frame.commandBuffer,
            .imageIndex = imageIndex,
            .game = { frameData, prepared.scene },
            .preview = previewInput,
            .uiDrawData = uiDrawData,
        });
    commandRecordingTimeTelemetry_.record(
        elapsedMilliseconds(commandRecordingStart));
    const auto resolvedRimTileCount = [](const RenderFrameData& data) {
        return static_cast<uint32_t>(std::ranges::count_if(data.tiles, [](const auto& tile) {
            return tile.groundGeometryEligible && tile.groundRimWidth > 0.0f;
        }));
    };
    lastStats_.resolvedGroundRimTiles = resolvedRimTileCount(frameData) +
        (prepared.previewFrameData ? resolvedRimTileCount(*prepared.previewFrameData) : 0);
    lastStats_.groundRimBudgetFallback = prepared.groundRimBudgetFallback;
    const auto* previewScene = prepared.previewScene
        ? &*prepared.previewScene : nullptr;
    lastStats_.reusedGroundRimSurfaces = prepared.scene.reusedGroundRimSurfaces +
        (previewScene ? previewScene->reusedGroundRimSurfaces : 0);
    lastStats_.generatedGroundRimSurfaces = prepared.scene.generatedGroundRimSurfaces +
        (previewScene ? previewScene->generatedGroundRimSurfaces : 0);
    lastStats_.groundRimSurfaceCacheHits = prepared.scene.groundRimSurfaceCacheHits +
        (previewScene ? previewScene->groundRimSurfaceCacheHits : 0);
    lastStats_.groundRimSurfaceCacheRebuilds = prepared.scene.groundRimSurfaceCacheRebuilds +
        (previewScene ? previewScene->groundRimSurfaceCacheRebuilds : 0);
    lastStats_.groundRimSurfaceCacheBytes = prepared.scene.groundRimSurfaceCacheBytes +
        (previewScene ? previewScene->groundRimSurfaceCacheBytes : 0);
    lastStats_.gpuTimestampsSupported = gpuProfiler_.supported();
    lastStats_.parallelScenePreparationEnabled =
        parallelScenePreparationEnabled_;
    lastStats_.pointShadowOptimizationsEnabled =
        pointShadowOptimizationsEnabled_;
    lastStats_.recorderScratchReuseEnabled = recorderScratchReuseEnabled_;
    lastStats_.waterCellCacheEnabled = activeResources_.waterCellCache->enabled();
    lastStats_.waterCellCacheRebuilds = activeResources_.waterCellCache->rebuilds();
    lastStats_.waterCellCacheBytes = activeResources_.waterCellCache->bytes();
    lastStats_.scenePreparationTiming =
        renderPhaseTiming(scenePreparationTimeTelemetry_.summary());
    const FrameTimeSummary gpuTiming = gpuProfiler_.frameTimeSummary();
    // Guarded, unlike the other two: on a device without timestamp support
    // this must keep whatever it last held rather than being overwritten with
    // an unavailable summary every frame.
    if (gpuTiming.available()) {
        lastStats_.gpuFrameTiming = renderPhaseTiming(gpuTiming);
    }

    submitAndPresent(frame, imageIndex);

    cpuFrameTimeTelemetry_.record(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cpuFrameStart).count());
    lastStats_.cpuFrameTiming =
        renderPhaseTiming(cpuFrameTimeTelemetry_.summary());
    lastStats_.assetPublications = assetPublications_;
    lastStats_.assetPublicationFrames = assetPublicationFrames_;
    const VulkanModelResources::LoadingStats loadingStats =
        modelResources_.loadingStats();
    lastStats_.textureUploadSubmissions =
        loadingStats.textureUploadSubmissions;
    lastStats_.textureUploadCompletions =
        loadingStats.textureUploadCompletions;
    lastStats_.textureUploadsInFlight = loadingStats.uploadingTextures;

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
    if (rect.offset.x < 0 || rect.offset.y < 0 || !rect.extent.width || !rect.extent.height ||
        uint64_t(rect.offset.x) + rect.extent.width > full.width ||
        uint64_t(rect.offset.y) + rect.extent.height > full.height) {
        throw std::invalid_argument("Frame capture region lies outside the scene");
    }
    const VkExtent2D display = activeResources_.swapchain->displayExtent();
    const auto mapX = [&](uint32_t x) { return uint32_t(uint64_t(x) * display.width / full.width); };
    const auto mapY = [&](uint32_t y) { return uint32_t(uint64_t(y) * display.height / full.height); };
    const VkOffset2D offset { static_cast<int32_t>(mapX(static_cast<uint32_t>(rect.offset.x))),
        static_cast<int32_t>(mapY(static_cast<uint32_t>(rect.offset.y))) };
    const VkExtent2D captureExtent {
        mapX(static_cast<uint32_t>(rect.offset.x) + rect.extent.width) - static_cast<uint32_t>(offset.x),
        mapY(static_cast<uint32_t>(rect.offset.y) + rect.extent.height) - static_cast<uint32_t>(offset.y) };

    // Everything submitted must have landed before the copy reads the image.
    deviceContext_.waitIdle();
    return captureImageRegion(
        deviceContext_.memoryAllocator(),
        deviceContext_.device(),
        deviceContext_.commandPool(),
        deviceContext_.graphicsQueue(),
        // The display image, not the scene target: it is tonemapped and in a
        // format PngWriter understands.
        activeResources_.swapchain->displayColorImage(),
        activeResources_.swapchain->colorFormat(),
        // The workspace samples this image; the full-window path blits it.
        // Capture restores whichever layout the last frame actually left.
        activeResources_.swapchain->displayColorLayout(),
        offset,
        captureExtent,
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
    gameViewportDisplay_ = display;
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

std::optional<VulkanRenderer::PickTarget> VulkanRenderer::pickTargetFor(
    const PreparedFrameScratch& prepared,
    Vec2 pixelPosition) const
{
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
    // Negative means the pointer is outside the game viewport - in the debug
    // UI's chrome, say - which is not a miss but a question that does not
    // apply.
    if (mappedPosition.x < 0.0f || mappedPosition.y < 0.0f) {
        return std::nullopt;
    }
    return PickTarget { mappedPosition, outputExtent };
}

std::optional<GridPosition3> VulkanRenderer::pickIsoGridCell(
    const PreparedFrame& frame,
    Vec2 pixelPosition) const
{
    const PreparedFrameScratch& prepared = resolvePreparedFrame(frame);
    const RenderFrameData& frameData = prepared.frameData;
    // The one guard that is not shared: a grid cell needs a board to be on.
    if (frameData.levelWidth == 0 || frameData.levelHeight == 0) {
        return std::nullopt;
    }
    const std::optional<PickTarget> target =
        pickTargetFor(prepared, pixelPosition);
    if (!target) {
        return std::nullopt;
    }
    const VkExtent2D outputExtent = target->extent;
    const Vec2 mappedPosition = target->position;
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
    const std::optional<PickTarget> target =
        pickTargetFor(prepared, pixelPosition);
    if (!target) {
        return std::nullopt;
    }
    const VkExtent2D outputExtent = target->extent;
    const Vec2 mappedPosition = target->position;
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
    const std::optional<PickTarget> target =
        pickTargetFor(prepared, pixelPosition);
    if (!target) {
        return std::nullopt;
    }
    const VkExtent2D outputExtent = target->extent;
    const Vec2 mappedPosition = target->position;

    std::optional<std::size_t> result;
    float nearestDepth = std::numeric_limits<float>::max();
    for (const RenderFrameData::Tile& tile : prepared.frameData.tiles) {
        if (!tile.editorDecorationIndex || tile.model.isCube()) {
            continue;
        }
        Aabb bounds = modelResources_.boundsForModel(tile.model);
        if (!bounds.valid()) {
            bounds = Aabb {
                Vec3 { 0.0f, 0.0f, 0.0f },
                Vec3 { 1.0f, 1.0f, 1.0f },
            };
        }

        const ModelTransformPoints transform =
            IsoScenePreparer::modelTransformPoints(tile);
        const std::array<Vec3, 8> localCorners = corners(bounds);
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
    // The player's retained unit transform, not its mesh bounds - see the
    // handoff note about model-backed tiles failing open here.
    constexpr std::array<Vec3, 8> localCorners = corners(
        Aabb { Vec3 { 0.0f, 0.0f, 0.0f }, Vec3 { 1.0f, 1.0f, 1.0f } });
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
    if (showFailureDialogs_) {
        showVulkanFailureDialog(window_, failure);
    }
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
    RenderStats stats = lastStats_;
    const VkPresentModeKHR presentMode =
        activeResources_.swapchain->presentMode();
    stats.fifoPresentationEnabled =
        presentMode == VK_PRESENT_MODE_FIFO_KHR ||
        presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    stats.sceneDepthBits = vulkanDepthFormatBits(depthFormat_);
    stats.assetSchedulingTiming = renderPhaseTiming(
        assetSchedulingTimeTelemetry_.summary());
    stats.frameFenceWaitTiming = renderPhaseTiming(
        frameFenceWaitTimeTelemetry_.summary());
    stats.assetMaintenanceTiming = renderPhaseTiming(
        assetMaintenanceTimeTelemetry_.summary());
    stats.imageAcquisitionTiming = renderPhaseTiming(
        imageAcquisitionTimeTelemetry_.summary());
    stats.commandRecordingTiming = renderPhaseTiming(
        commandRecordingTimeTelemetry_.summary());
    stats.submitPresentTiming = renderPhaseTiming(
        submitPresentTimeTelemetry_.summary());
    stats.assetPublicationEventTiming = renderPhaseTiming(
        assetPublicationEventTimeTelemetry_.summary());
    stats.gpuShadowTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::Shadows));
    stats.gpuSceneTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::Scene));
    stats.gpuSceneRasterTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SceneRaster));
    stats.gpuSceneFacesTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SceneFaces));
    stats.gpuSceneModelsTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SceneModels));
    stats.gpuSceneDepthPublishTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SceneDepthPublish));
    stats.gpuSceneTranslucencyTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SceneTranslucency));
    stats.gpuParticleTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SceneParticles));
    stats.gpuSceneMirrorContinuationTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(
            VulkanGpuPhase::SceneMirrorContinuation));
    stats.gpuSsaoTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::Ssao));
    stats.gpuSsaoSnapshotTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SsaoSnapshot));
    stats.gpuSsaoOcclusionTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SsaoOcclusion));
    stats.gpuSsaoCompositeTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::SsaoComposite));
    stats.gpuAtmosphereTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::Atmosphere));
    stats.gpuAtmosphereGlobalTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::AtmosphereGlobal));
    stats.gpuAtmosphereGlobalIntegrationTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(
            VulkanGpuPhase::AtmosphereGlobalIntegration));
    stats.gpuAtmosphereGlobalCompositeTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(
            VulkanGpuPhase::AtmosphereGlobalComposite));
    stats.gpuAtmosphereVolumesTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::AtmosphereVolumes));
    stats.gpuOutputTiming = renderPhaseTiming(
        gpuProfiler_.phaseTimeSummary(VulkanGpuPhase::Output));
    const VulkanMemoryStatistics memory =
        deviceContext_.memoryAllocator().statistics();
    stats.gpuMemoryBlockCount = memory.blockCount;
    stats.gpuMemoryAllocationCount = memory.allocationCount;
    stats.gpuImageAllocationCount = memory.imageCount;
    stats.gpuBufferAllocationCount = memory.bufferCount;
    stats.gpuMemoryBlockBytes = memory.blockBytes;
    stats.gpuMemoryAllocationBytes = memory.allocationBytes;
    stats.gpuMemoryPeakAllocationBytes = memory.peakAllocationBytes;
    stats.gpuMemoryTotalAllocatedBytes = memory.totalAllocatedBytes;
    stats.gpuMemoryTotalFreedBytes = memory.totalFreedBytes;
    stats.gpuImageBytes = memory.imageBytes;
    stats.gpuBufferBytes = memory.bufferBytes;
    stats.gpuDeviceLocalBytes = memory.deviceLocalBytes;
    stats.gpuHostVisibleBytes = memory.hostVisibleBytes;
    stats.gpuLifetimeAllocations = memory.lifetimeAllocations;
    stats.gpuLifetimeFrees = memory.lifetimeFrees;
    stats.gpuMemoryHeapCount = memory.heapCount;
    for (uint32_t index = 0; index < memory.heapCount; ++index) {
        const VulkanMemoryHeapStatistics& heap = memory.heaps[index];
        stats.gpuMemoryHeaps[index] = {
            .deviceLocal = heap.deviceLocal,
            .blockBytes = heap.blockBytes,
            .allocationBytes = heap.allocationBytes,
            .usageBytes = heap.usageBytes,
            .budgetBytes = heap.budgetBytes,
        };
    }
    const ProcessMemoryStatistics process = processMemoryStatistics();
    stats.processMemoryAvailable = process.available;
    stats.processResidentBytes = process.residentBytes;
    stats.processPeakResidentBytes = process.peakResidentBytes;
    stats.processPrivateBytes = process.privateBytes;
    sceneRecorder_.populateTimingStats(stats);
    return stats;
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

void VulkanRenderer::requestShaderReload()
{
    reconfigurationQueue_.requestShaderReload();
}

uint64_t VulkanRenderer::appliedShaderRevision() const
{
    return reconfigurationQueue_.active().shaderRevision;
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

bool VulkanRenderer::frustumCullingEnabled() const
{
    return scenePreparer_.frustumCulling();
}

void VulkanRenderer::setFrustumCullingEnabled(bool enabled)
{
    scenePreparer_.setFrustumCulling(enabled);
    previewScenePreparer_.setFrustumCulling(enabled);
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
        .previewSceneColor = {
            .sampler = resources.swapchain->sceneColorSampler(),
            .imageView = resources.swapchain->previewSceneColorView(),
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
            .imageView = resources.swapchain->sampledDepthView(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        },
        .ssao = {
            .sampler = resources.ssaoPass->sampler(),
            .imageView = resources.ssaoPass->imageView(),
        },
        .atmosphere = {
            .sampler = resources.atmospherePass->sampler(),
            .imageView = resources.atmospherePass->imageView(),
        },
        .bloomExtract = {
            .sampler = resources.bloomPass->sampler(),
            .imageView = resources.bloomPass->extractImageView(),
        },
        .bloom = {
            .sampler = resources.bloomPass->sampler(),
            .imageView = resources.bloomPass->bloomImageView(),
        },
        .uiFont = {
            .sampler = uiResources_.sampler(),
            .imageView = uiResources_.fontImageView(),
        },
        .uiCurves = { .sampler = uiResources_.curveSampler(), .imageView = uiResources_.curveImageView() },
        .titleBackground = {
            .sampler = uiResources_.sampler(),
            .imageView = uiResources_.titleBackgroundImageView(),
        },
        .modelTextures = modelResources_.textures(),
        .skinning = modelResources_.skinningBuffer(),
        .drawInstances = modelResources_.drawInstanceBuffer(),
        .materials = modelResources_.materialBuffer(),
        .waterCells = resources.waterCellCache->buffers(),
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
    const RendererSettingsSnapshot& settings,
    std::future<void>* startupPrerequisites)
{
    using StartupClock = std::chrono::steady_clock;
    const bool initialCreation = pipelineRebuilds_ == 0;
    const StartupClock::time_point started = StartupClock::now();
    auto phaseStarted = started;
    const auto finishPhase = [&phaseStarted]() {
        const auto now = StartupClock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - phaseStarted).count();
        phaseStarted = now;
        return elapsed;
    };
    RenderResourceSet resources;
    const VkSampleCountFlagBits sampleCount =
        sampleCountForMode(settings.antiAliasing);
    resources.swapchain =
        std::make_unique<VulkanSwapchainResources>();
    resources.swapchain->create(
        deviceContext_.physicalDevice(),
        deviceContext_.memoryAllocator(),
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
    const auto swapchainMicroseconds = finishPhase();
    // Must follow swapchain creation: the count comes from the images the
    // driver actually handed back, not from the requested minimum.
    resources.presentSemaphores = SwapchainPresentSemaphores(
        deviceContext_.device(), resources.swapchain->imageCount());
    resources.ssaoPass = std::make_unique<VulkanSsaoPass>();
    resources.ssaoPass->create(
        deviceContext_.memoryAllocator(),
        deviceContext_.device(),
        resources.swapchain->renderExtent());
    const auto ssaoMicroseconds = finishPhase();
    resources.atmospherePass = std::make_unique<VulkanAtmospherePass>();
    resources.atmospherePass->create(
        deviceContext_.memoryAllocator(),
        deviceContext_.device(),
        resources.swapchain->renderExtent(),
        resources.swapchain->sceneColorFormat());
    const auto atmosphereMicroseconds = finishPhase();
    resources.bloomPass = std::make_unique<VulkanBloomPass>();
    resources.bloomPass->create(
        deviceContext_.memoryAllocator(),
        deviceContext_.device(),
        resources.swapchain->renderExtent(),
        resources.swapchain->sceneColorFormat());
    const auto bloomMicroseconds = finishPhase();
    if (startupPrerequisites) {
        startupPrerequisites->get();
    }
    const auto prerequisiteWaitMicroseconds = finishPhase();
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        deviceContext_.physicalDevice(), &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        deviceContext_.physicalDevice(), &queueFamilyCount, queueFamilies.data());
    const bool computeSupported = (queueFamilies.at(deviceContext_.queueFamilies().graphics)
        .queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    resources.waterCellCache = std::make_unique<VulkanWaterCellCache>();
    resources.waterCellCache->create(deviceContext_.memoryAllocator(),
        maxFramesInFlight_, waterCellCacheEnabled_ && computeSupported);
    resources.sceneDescriptors =
        std::make_unique<VulkanSceneDescriptors>();
    resources.sceneDescriptors->create(
        deviceContext_.memoryAllocator(),
        deviceContext_.device(),
        descriptorResources(resources),
        maxFramesInFlight_);
    const auto descriptorsMicroseconds = finishPhase();
    resources.pipelines = createPipelines(resources, settings);
    const auto pipelinesMicroseconds = finishPhase();
    if (initialCreation) {
        const auto totalMicroseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(
                StartupClock::now() - started).count();
        log::info(log::Category::Rendering)
            << "Render-resource startup phases (us): swapchain="
            << swapchainMicroseconds
            << " ssao=" << ssaoMicroseconds
            << " atmosphere=" << atmosphereMicroseconds
            << " bloom=" << bloomMicroseconds
            << " prerequisite-wait=" << prerequisiteWaitMicroseconds
            << " descriptors=" << descriptorsMicroseconds
            << " pipelines=" << pipelinesMicroseconds
            << " total=" << totalMicroseconds;
    }
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
        .waterCellCacheEnabled = resources.waterCellCache->enabled(),
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
    modelResources_.completeFrame(frameIndex);
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

    // A shader edited while the game runs can compile and still fail to
    // become a pipeline. That must not take the session down: keep drawing
    // with the pipelines already built and report why. Any other pipeline
    // rebuild failing is still fatal, as before.
    RendererSettingsSnapshot withoutShaderChange = plan->settings;
    withoutShaderChange.shaderRevision =
        reconfigurationQueue_.active().shaderRevision;
    const bool shaderReloadOnly =
        withoutShaderChange == reconfigurationQueue_.active();
    std::unique_ptr<VulkanPipelineFactory> replacement;
    try {
        replacement = createPipelines(activeResources_, plan->settings);
    } catch (const std::exception& error) {
        if (!shaderReloadOnly) {
            throw;
        }
        shaderReloadError_ = error.what();
        log::error(log::Category::Rendering)
            << "Shader reload failed; keeping the previous pipelines: "
            << shaderReloadError_;
        reconfigurationQueue_.commit(*plan);
        return;
    }
    if (plan->settings.shaderRevision !=
        reconfigurationQueue_.active().shaderRevision) {
        shaderReloadError_.clear();
    }
    RenderResourceSet retired;
    retired.pipelines = std::move(activeResources_.pipelines);
    activeResources_.pipelines = std::move(replacement);
    activeResources_.waterCellCache->invalidate();
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
    initInfo.MinAllocationSize = VkDeviceSize { 1024 } * 1024;

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
        << static_cast<double>(samplePixels) / 1'000'000.0
        << " M sample-pixels), scene depth "
        << vulkanDepthFormatName(depthFormat_);
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
