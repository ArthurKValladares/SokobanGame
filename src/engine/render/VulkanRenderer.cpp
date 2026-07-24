#include "engine/render/VulkanRenderer.hpp"

#include "engine/Log.hpp"
#include "engine/Config.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanDeviceSelection.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
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
    , antiAliasingMode_(antiAliasingMode)
{
    wireframeLineWidth_ = std::clamp(
        wireframeLineWidth_,
        1.0f,
        deviceContext_.wireframeLineWidthRange()[1]);
    // The default MSAA mode is a request; drop to what the device supports.
    activeSampleCount_ = sampleCountForMode(antiAliasingMode_);
    swapchainResources_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        deviceContext_.surface(),
        window_,
        {
            .graphics = deviceContext_.queueFamilies().graphics,
            .present = deviceContext_.queueFamilies().present,
        },
        activeSampleCount_,
        renderScalePercent,
        depthFormat_,
        vsync);
    logRenderConfiguration();
    shadowPass_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        shadowFormat_);
    ssaoPass_.create(
        deviceContext_.physicalDevice(),
        deviceContext_.device(),
        swapchainResources_.renderExtent());
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
    sceneDescriptors_.create(
        deviceContext_.device(),
        descriptorResources(),
        maxFramesInFlight_);
    descriptorSync_.markAllUpdated();
    createPipeline();
    createFrameResources();
    initializeDebugUi();
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

    destroyPipeline();
    sceneDescriptors_.destroy();
    modelResources_.destroy();
    uiResources_.destroy();

    ssaoPass_.destroy();
    shadowPass_.destroy();
    swapchainResources_.destroy();
}

VulkanRenderer::PreparedFrame VulkanRenderer::prepareFrame(
    RenderFrameData frameData)
{
    const VkExtent2D extent = swapchainResources_.renderExtent();
    const uint32_t scratchIndex = nextPreparedFrameSlot_;
    nextPreparedFrameSlot_ =
        (nextPreparedFrameSlot_ + 1) % preparedFrameSlotCount_;
    PreparedFrameScratch& scratch = preparedFrameScratch_[scratchIndex];
    scratch.frameData = std::move(frameData);
    scratch.generation = nextPreparedFrameGeneration_++;
    scenePreparer_.prepare(
        scratch.frameData,
        {
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
        },
        scratch.scene);

    PreparedFrame frame;
    frame.levelWidth = scratch.frameData.levelWidth;
    frame.levelHeight = scratch.frameData.levelHeight;
    frame.scratchIndex = scratchIndex;
    frame.generation = scratch.generation;
    return frame;
}

const VulkanRenderer::PreparedFrameScratch&
VulkanRenderer::resolvePreparedFrame(const PreparedFrame& frame) const
{
    if (frame.scratchIndex >= preparedFrameScratch_.size()) {
        throw std::logic_error("Prepared frame has an invalid scratch slot");
    }
    if (frame.generation == 0) {
        throw std::logic_error("Prepared frame was never initialized");
    }
    const PreparedFrameScratch& scratch =
        preparedFrameScratch_[frame.scratchIndex];
    if (scratch.generation != frame.generation) {
        throw std::logic_error(
            "Prepared frame scratch was reused before consumption");
    }
    return scratch;
}

void VulkanRenderer::drawFrame(
    const PreparedFrame& preparedFrame,
    const UiDrawData& uiDrawData)
{
    const PreparedFrameScratch& prepared =
        resolvePreparedFrame(preparedFrame);
    const RenderFrameData& frameData = prepared.frameData;
    ensureAssets(renderAssetRequirementsForFrame(frameData));

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
    modelResources_.retireCompletedUploads();
    if (modelResources_.publishReadyAssets(1)) {
        descriptorSync_.resourcesChanged();
    }
    if (descriptorSync_.needsUpdate(currentFrame_)) {
        sceneDescriptors_.update(currentFrame_, descriptorResources());
        descriptorSync_.markUpdated(currentFrame_);
    }
    modelResources_.updateAnimations(frameData);

    uint32_t imageIndex = 0;
    VkResult acquired = swapchainResources_.acquire(frame.imageAvailable, imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquired, "vkAcquireNextImageKHR failed");
    }

    vkCheck(
        vkResetFences(
            deviceContext_.device(), 1, &frame.inFlight),
        "vkResetFences failed");
    vkCheck(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");

    lastStats_ = sceneRecorder_.record(
        {
            .swapchain = swapchainResources_,
            .shadowPass = shadowPass_,
            .ssaoPass = ssaoPass_,
            .sceneDescriptors = sceneDescriptors_,
            .pipelines = pipelines_,
            .modelResources = modelResources_,
        },
        {
            .descriptorFrameIndex = currentFrame_,
            .activeSamples = sampleCountValue(),
            .wireframeEnabled = wireframeEnabled_,
            .wireframeLineWidth = wireframeLineWidth_,
            .statsFrameIndex = nextStatsFrameIndex_++,
            .pipelineRebuilds = pipelineRebuilds_,
            .swapchainRecreations = swapchainRecreations_,
            .swapchainRecreationDeferrals =
                swapchainRecreationDeferrals_,
        },
        frame.commandBuffer,
        imageIndex,
        frameData,
        prepared.scene,
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

    const VkResult presented = swapchainResources_.present(
        deviceContext_.presentQueue(),
        frame.renderFinished,
        imageIndex);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        vkCheck(presented, "vkQueuePresentKHR failed");
    }

    currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight_;
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
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
#else
    return false;
#endif
}

bool VulkanRenderer::wantsMouseCapture() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
#else
    return false;
#endif
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
        swapchainResources_.extent().width == 0 ||
        swapchainResources_.extent().height == 0) {
        return std::nullopt;
    }

    const VkExtent2D outputExtent = swapchainResources_.extent();
    return scenePreparer_.pickGridCell(
        prepared.scene,
        pixelPosition,
        {
            static_cast<float>(outputExtent.width),
            static_cast<float>(outputExtent.height),
        },
        frameData.levelWidth,
        frameData.levelHeight);
}

void VulkanRenderer::waitIdle() const
{
    deviceContext_.waitIdle();
}

AntiAliasingMode VulkanRenderer::antiAliasingMode() const
{
    return antiAliasingMode_;
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
    switch (swapchainResources_.presentMode()) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
    default: return "Other";
    }
}

bool VulkanRenderer::wireframeEnabled() const
{
    return wireframeEnabled_;
}

void VulkanRenderer::setWireframeEnabled(bool enabled)
{
    if (enabled == wireframeEnabled_) {
        return;
    }

    waitIdle();
    wireframeEnabled_ = enabled;
    destroyPipeline();
    createPipeline();
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
    if (mode == antiAliasingMode_) {
        return;
    }

    waitIdle();
    antiAliasingMode_ = mode;
    activeSampleCount_ = sampleCountForMode(mode);

    destroyPipeline();
    swapchainResources_.recreateAttachments(
        activeSampleCount_,
        swapchainResources_.renderScalePercent());
    sceneDescriptors_.update(descriptorResources());
    descriptorSync_.markAllUpdated();
    createPipeline();
}

int VulkanRenderer::renderScalePercent() const
{
    return swapchainResources_.renderScalePercent();
}

void VulkanRenderer::setRenderScalePercent(int percent)
{
    if (percent == swapchainResources_.renderScalePercent()) {
        return;
    }

    waitIdle();
    swapchainResources_.recreateAttachments(activeSampleCount_, percent);
    ssaoPass_.recreate(swapchainResources_.renderExtent());
    sceneDescriptors_.update(descriptorResources());
    descriptorSync_.markAllUpdated();
    logRenderConfiguration();
}

VulkanSceneDescriptors::Resources VulkanRenderer::descriptorResources() const
{
    return {
        .shadow = {
            .sampler = shadowPass_.sampler(),
            .imageView = shadowPass_.imageView(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        },
        .sceneColor = {
            .sampler = swapchainResources_.sceneColorSampler(),
            .imageView = swapchainResources_.sceneColorView(),
        },
        .sceneDepth = {
            .sampler = shadowPass_.sampler(),
            .imageView = swapchainResources_.sampledDepthView(),
        },
        .ssao = {
            .sampler = ssaoPass_.sampler(),
            .imageView = ssaoPass_.imageView(),
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

void VulkanRenderer::setAnimationPreview(const GltfAnimationClip* clip, float timeSeconds)
{
    modelResources_.setAnimationPreview(clip, timeSeconds);
}

void VulkanRenderer::createPipeline()
{
    pipelines_.create({
        .device = deviceContext_.device(),
        .assetRoot = assetRoot_,
        .descriptorSetLayout = sceneDescriptors_.layout(),
        .colorFormat = swapchainResources_.colorFormat(),
        .depthFormat = depthFormat_,
        .shadowFormat = shadowFormat_,
        .sampleCount = activeSampleCount_,
        .wireframe = wireframeEnabled_,
    });
    ++pipelineRebuilds_;
}

void VulkanRenderer::destroyPipeline()
{
    pipelines_.destroy();
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForVulkan(window_)) {
        throw std::runtime_error("ImGui_ImplSDL3_InitForVulkan failed");
    }

    const VkFormat colorAttachmentFormat = swapchainResources_.colorFormat();
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
    initInfo.ImageCount = std::max(2U, swapchainResources_.imageCount());
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRendering;
    initInfo.UseDynamicRendering = true;
    initInfo.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }
#endif
}

void VulkanRenderer::shutdownDebugUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
#endif
}

void VulkanRenderer::recreateSwapchain()
{
    if (!swapchainResources_.canRecreate()) {
        ++swapchainRecreationDeferrals_;
        return;
    }

    deviceContext_.waitIdle();
    if (!swapchainResources_.canRecreate()) {
        ++swapchainRecreationDeferrals_;
        return;
    }
    const VkFormat oldColorFormat = swapchainResources_.colorFormat();
    swapchainResources_.recreate();
    ssaoPass_.recreate(swapchainResources_.renderExtent());
    sceneDescriptors_.update(descriptorResources());
    descriptorSync_.markAllUpdated();
    if (oldColorFormat != swapchainResources_.colorFormat()) {
        destroyPipeline();
        createPipeline();
    }
    ++swapchainRecreations_;
    logRenderConfiguration();
}

void VulkanRenderer::logRenderConfiguration() const
{
    const VkExtent2D extent = swapchainResources_.extent();
    const VkExtent2D renderExtent = swapchainResources_.renderExtent();
    const uint64_t pixels =
        static_cast<uint64_t>(renderExtent.width) * renderExtent.height;
    const uint64_t samplePixels = pixels * sampleCountValue();
    log::info() << "Vulkan swapchain: " << extent.width << 'x' << extent.height
        << ", " << swapchainResources_.imageCount() << " images, "
        << presentModeName() << ", " << sampleCountValue()
        << "x MSAA; scene " << renderExtent.width << 'x'
        << renderExtent.height << " at "
        << swapchainResources_.renderScalePercent() << "% ("
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
