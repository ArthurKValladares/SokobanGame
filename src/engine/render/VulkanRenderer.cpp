#include "engine/render/VulkanRenderer.hpp"

#include "engine/BoardLayout.hpp"
#include "engine/Log.hpp"
#include "engine/Config.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanDeviceSelection.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

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
#include <set>
#include <stdexcept>
#include <utility>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {
namespace {

constexpr std::array<const char*, 2> requiredDeviceExtensions {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
};

struct IsoFace {
    std::array<Vec3, 4> vertices {};
    std::array<Vec4, 4> shadowVertices {};
    Vec3 normal {};
    Vec4 color {};
    bool blurBehind = false;
    bool showGrid = false;
    bool isEditorPreview = false;
    Vec2 gridSize {};
    float depth = 0.0f;
};

Vec3 subtract(Vec3 left, Vec3 right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

Vec3 add(Vec3 left, Vec3 right)
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.0001f) {
        return {};
    }

    return multiply(value, 1.0f / length);
}

float cross2D(Vec2 left, Vec2 right)
{
    return left.x * right.y - left.y * right.x;
}

Vec2 subtract(Vec2 left, Vec2 right)
{
    return {
        left.x - right.x,
        left.y - right.y,
    };
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

std::array<Vec4, 4> affineTransformColumns(Vec4 origin, Vec4 xPoint, Vec4 yPoint, Vec4 zPoint)
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

ModelTransformPoints modelTransformPoints(const RenderFrameData::Tile& tile)
{
    const float x = tile.position.x;
    const float y = tile.position.y;
    const float z = tile.baseElevation;
    const float width = tile.size.x;
    const float depth = tile.size.y;
    const float height = std::max(tile.height, 0.0f);

    const uint32_t quarterTurns = tile.modelRotationQuarterTurns % 4;
    ModelTransformPoints result;
    switch (quarterTurns) {
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
    result.zPoint = { result.origin.x, result.origin.y, z + height };
    return result;
}

bool pointInTriangle(Vec2 point, Vec2 a, Vec2 b, Vec2 c)
{
    constexpr float epsilon = 0.001f;
    const float ab = cross2D(subtract(b, a), subtract(point, a));
    const float bc = cross2D(subtract(c, b), subtract(point, b));
    const float ca = cross2D(subtract(a, c), subtract(point, c));
    const bool hasNegative = ab < -epsilon || bc < -epsilon || ca < -epsilon;
    const bool hasPositive = ab > epsilon || bc > epsilon || ca > epsilon;
    return !(hasNegative && hasPositive);
}

bool pointInQuad(Vec2 point, const std::array<Vec2, 4>& quad)
{
    return pointInTriangle(point, quad[0], quad[1], quad[2]) ||
        pointInTriangle(point, quad[0], quad[2], quad[3]);
}

std::optional<float> pointDepthInTriangle(Vec2 point, Vec3 a, Vec3 b, Vec3 c)
{
    constexpr float epsilon = 0.00001f;
    const Vec2 a2 { a.x, a.y };
    const Vec2 b2 { b.x, b.y };
    const Vec2 c2 { c.x, c.y };
    const float area = cross2D(subtract(b2, a2), subtract(c2, a2));
    if (std::abs(area) <= epsilon) {
        return std::nullopt;
    }

    const float weightA = cross2D(subtract(b2, point), subtract(c2, point)) / area;
    const float weightB = cross2D(subtract(c2, point), subtract(a2, point)) / area;
    const float weightC = 1.0f - weightA - weightB;
    if (weightA < -epsilon || weightB < -epsilon || weightC < -epsilon) {
        return std::nullopt;
    }

    return weightA * a.z + weightB * b.z + weightC * c.z;
}

std::optional<float> pointDepthInQuad(Vec2 point, const std::array<Vec3, 4>& quad)
{
    if (const std::optional<float> depth = pointDepthInTriangle(point, quad[0], quad[1], quad[2])) {
        return depth;
    }
    return pointDepthInTriangle(point, quad[0], quad[2], quad[3]);
}

std::vector<const char*> validationLayers()
{
#if SOKOBAN_ENABLE_VALIDATION
    return { "VK_LAYER_KHRONOS_validation" };
#else
    return {};
#endif
}

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

bool supportsValidationLayer()
{
    uint32_t layerCount = 0;
    vkCheck(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties failed");

    std::vector<VkLayerProperties> layers(layerCount);
    vkCheck(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "vkEnumerateInstanceLayerProperties failed");

    for (const auto& layer : layers) {
        if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
            return true;
        }
    }
    return false;
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
    , antiAliasingMode_(antiAliasingMode)
{
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createDevice();
    // The default MSAA mode is a request; drop to what the device supports.
    activeSampleCount_ = sampleCountForMode(antiAliasingMode_);
    swapchainResources_.create(
        physicalDevice_,
        device_,
        surface_,
        window_,
        { .graphics = queueFamilies_.graphics, .present = queueFamilies_.present },
        activeSampleCount_,
        renderScalePercent,
        depthFormat_,
        vsync);
    logRenderConfiguration();
    shadowPass_.create(physicalDevice_, device_, shadowFormat_);
    ssaoPass_.create(physicalDevice_, device_, swapchainResources_.renderExtent());
    createCommandPool();
    uiResources_.create(
        physicalDevice_, device_, commandPool_, graphicsQueue_, uiFont,
        loadRgbaImage(assetRoot_ / config::titleBackgroundPath));
    modelResources_.create(
        physicalDevice_, device_, commandPool_, graphicsQueue_,
        assetRoot_, manifest);
    sceneDescriptors_.create(
        device_, descriptorResources(), maxFramesInFlight_);
    descriptorSync_.markAllUpdated();
    createPipeline();
    createFrameResources();
    initializeDebugUi();
}

VulkanRenderer::~VulkanRenderer()
{
    if (device_) {
        vkDeviceWaitIdle(device_);
    }

    shutdownDebugUi();

    for (auto& frame : frames_) {
        if (frame.imageAvailable) {
            vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        }
        if (frame.renderFinished) {
            vkDestroySemaphore(device_, frame.renderFinished, nullptr);
        }
        if (frame.inFlight) {
            vkDestroyFence(device_, frame.inFlight, nullptr);
        }
    }

    destroyPipeline();
    sceneDescriptors_.destroy();
    modelResources_.destroy();
    uiResources_.destroy();
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }

    ssaoPass_.destroy();
    shadowPass_.destroy();
    swapchainResources_.destroy();

    if (device_) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void VulkanRenderer::drawFrame(const RenderFrameData& frameData, const UiDrawData& uiDrawData)
{
    ensureAssets(renderAssetRequirementsForFrame(frameData));

#if SOKOBAN_ENABLE_DEBUG_UI
    // Finish the ImGui frame even when swapchain acquisition is out of date
    // and this render frame has to be skipped during a window-mode change.
    ImGui::Render();
#endif

    auto& frame = frames_[currentFrame_];
    vkCheck(vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences failed");
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

    vkCheck(vkResetFences(device_, 1, &frame.inFlight), "vkResetFences failed");
    vkCheck(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");

    recordCommandBuffer(frame.commandBuffer, imageIndex, frameData, uiDrawData);

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

    vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, frame.inFlight), "vkQueueSubmit2 failed");

    const VkResult presented = swapchainResources_.present(
        presentQueue_, frame.renderFinished, imageIndex);
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

std::optional<GridPosition3> VulkanRenderer::pickIsoGridCell(const RenderFrameData& frameData, Vec2 pixelPosition) const
{
    if (frameData.viewMode != RenderViewMode::Isometric3D ||
        frameData.levelWidth == 0 ||
        frameData.levelHeight == 0 ||
        swapchainResources_.extent().width == 0 ||
        swapchainResources_.extent().height == 0) {
        return std::nullopt;
    }

    const IsoRenderLayout layout = calculateIsoRenderLayout(frameData);

    auto clipToPixel = [this](Vec3 clip) {
        return Vec2 {
            (clip.x + 1.0f) * 0.5f * static_cast<float>(swapchainResources_.extent().width),
            (1.0f - clip.y) * 0.5f * static_cast<float>(swapchainResources_.extent().height),
        };
    };

    auto faceVisible = [&](const std::array<Vec3, 4>& vertices, Vec3 normal) {
        const Vec3 center = multiply(add(add(vertices[0], vertices[1]), add(vertices[2], vertices[3])), 0.25f);
        return dot(normal, subtract(layout.cameraPosition, center)) > 0.0f;
    };

    std::optional<GridPosition3> picked;
    float pickedDepth = std::numeric_limits<float>::max();

    auto testFace = [&](const RenderFrameData::Tile& tile, const std::array<Vec3, 4>& vertices, Vec3 normal) {
        if (!faceVisible(vertices, normal)) {
            return;
        }

        std::array<Vec3, 4> clipVertices {
            projectIsoPoint(layout, vertices[0]),
            projectIsoPoint(layout, vertices[1]),
            projectIsoPoint(layout, vertices[2]),
            projectIsoPoint(layout, vertices[3]),
        };
        const std::array<Vec3, 4> pixelQuad {
            Vec3 { clipToPixel(clipVertices[0]).x, clipToPixel(clipVertices[0]).y, clipVertices[0].z },
            Vec3 { clipToPixel(clipVertices[1]).x, clipToPixel(clipVertices[1]).y, clipVertices[1].z },
            Vec3 { clipToPixel(clipVertices[2]).x, clipToPixel(clipVertices[2]).y, clipVertices[2].z },
            Vec3 { clipToPixel(clipVertices[3]).x, clipToPixel(clipVertices[3]).y, clipVertices[3].z },
        };
        const std::array<Vec2, 4> pixelQuad2D {
            Vec2 { pixelQuad[0].x, pixelQuad[0].y },
            Vec2 { pixelQuad[1].x, pixelQuad[1].y },
            Vec2 { pixelQuad[2].x, pixelQuad[2].y },
            Vec2 { pixelQuad[3].x, pixelQuad[3].y },
        };
        if (!pointInQuad(pixelPosition, pixelQuad2D)) {
            return;
        }

        const std::optional<float> depth = pointDepthInQuad(pixelPosition, pixelQuad);
        if (!depth || *depth >= pickedDepth) {
            return;
        }

        const int x = static_cast<int>(std::floor(tile.position.x + 0.0001f));
        const int y = static_cast<int>(std::floor(tile.position.y + 0.0001f));
        if (x < 0 || y < 0 || x >= static_cast<int>(frameData.levelWidth) || y >= static_cast<int>(frameData.levelHeight)) {
            return;
        }

        picked = tile.cell;
        pickedDepth = *depth;
    };

    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.isEditorPreview) {
            continue;
        }

        const float x = tile.position.x;
        const float y = tile.position.y;
        const float width = tile.size.x;
        const float depth = tile.size.y;
        const float base = tile.baseElevation;
        const float height = std::max(tile.height, 0.0f);
        const Vec3 a { x, y, base };
        const Vec3 b { x + width, y, base };
        const Vec3 c { x + width, y + depth, base };
        const Vec3 d { x, y + depth, base };

        if (height <= 0.0f) {
            testFace(tile, { a, b, c, d }, { 0.0f, 0.0f, 1.0f });
            continue;
        }

        const float top = base + height;
        const Vec3 e { x, y, top };
        const Vec3 f { x + width, y, top };
        const Vec3 g { x + width, y + depth, top };
        const Vec3 h { x, y + depth, top };
        testFace(tile, { a, b, f, e }, { 0.0f, -1.0f, 0.0f });
        testFace(tile, { b, c, g, f }, { 1.0f, 0.0f, 0.0f });
        testFace(tile, { c, d, h, g }, { 0.0f, 1.0f, 0.0f });
        testFace(tile, { d, a, e, h }, { -1.0f, 0.0f, 0.0f });
        testFace(tile, { e, f, g, h }, { 0.0f, 0.0f, 1.0f });
    }

    return picked;
}

void VulkanRenderer::waitIdle() const
{
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
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
    return physicalDeviceProperties_.deviceName;
}

const char* VulkanRenderer::physicalDeviceTypeName() const
{
    return vulkanDeviceTypeName(physicalDeviceProperties_.deviceType);
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
    return wideLinesSupported_;
}

float VulkanRenderer::wireframeLineWidth() const
{
    return wireframeLineWidth_;
}

std::array<float, 2> VulkanRenderer::wireframeLineWidthRange() const
{
    return wireframeLineWidthRange_;
}

void VulkanRenderer::setWireframeLineWidth(float lineWidth)
{
    const float maxLineWidth = wideLinesSupported_ ? wireframeLineWidthRange_[1] : 1.0f;
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

void VulkanRenderer::createInstance()
{
    VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Sokoban 3D",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "Sokoban Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }

    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

    auto layers = validationLayers();
    if (!layers.empty() && !supportsValidationLayer()) {
        layers.clear();
    }

    VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    vkCheck(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance failed");
}

void VulkanRenderer::createSurface()
{
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
}

void VulkanRenderer::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkCheck(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices failed");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan-capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkCheck(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices failed");

    int bestScore = std::numeric_limits<int>::min();
    for (VkPhysicalDevice device : devices) {
        if (!isDeviceSuitable(device)) {
            continue;
        }

        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(device, &properties);
        const int score = vulkanDevicePreferenceScore(properties);
        if (physicalDevice_ == VK_NULL_HANDLE || score > bestScore) {
            physicalDevice_ = device;
            bestScore = score;
        }
    }

    if (physicalDevice_ != VK_NULL_HANDLE) {
        queueFamilies_ = findQueueFamilies(physicalDevice_);
        vkGetPhysicalDeviceProperties(
            physicalDevice_,
            &physicalDeviceProperties_);
        log::info() << "Vulkan GPU: " << physicalDeviceProperties_.deviceName
            << " (" << vulkanDeviceTypeName(
                physicalDeviceProperties_.deviceType) << ")";
        return;
    }

    throw std::runtime_error("No GPU supports the required Vulkan 1.4 feature set");
}

void VulkanRenderer::createDevice()
{
    std::set<uint32_t> uniqueQueueFamilies { queueFamilies_.graphics, queueFamilies_.present };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    const float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        queueInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        });
    }

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .extendedDynamicState = VK_TRUE,
    };

    VkPhysicalDeviceVulkan13Features vulkan13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extendedDynamicState,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceFeatures supportedFeatures {};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supportedFeatures);
    wideLinesSupported_ = supportedFeatures.wideLines == VK_TRUE;

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    const float minLineWidth = std::max(properties.limits.lineWidthRange[0], 1.0f);
    const float maxPracticalLineWidth = std::max(minLineWidth, std::min(properties.limits.lineWidthRange[1], config::maxWireframeLineWidth));
    wireframeLineWidthRange_ = wideLinesSupported_
        ? std::array<float, 2> { minLineWidth, maxPracticalLineWidth }
        : std::array<float, 2> { 1.0f, 1.0f };
    wireframeLineWidth_ = std::clamp(wireframeLineWidth_, 1.0f, wireframeLineWidthRange_[1]);

    VkPhysicalDeviceFeatures enabledFeatures {
        .fillModeNonSolid = VK_TRUE,
        .wideLines = wideLinesSupported_ ? VK_TRUE : VK_FALSE,
    };

    VkDeviceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13,
        .queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size()),
        .pQueueCreateInfos = queueInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
        .pEnabledFeatures = &enabledFeatures,
    };

    vkCheck(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice failed");

    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
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

void VulkanRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilies_.graphics,
    };

    vkCheck(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_), "vkCreateCommandPool failed");
}

void VulkanRenderer::setAnimationPreview(const GltfAnimationClip* clip, float timeSeconds)
{
    modelResources_.setAnimationPreview(clip, timeSeconds);
}

void VulkanRenderer::createPipeline()
{
    pipelines_.create({
        .device = device_,
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
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };
    vkCheck(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers.data()), "vkAllocateCommandBuffers failed");

    VkSemaphoreCreateInfo semaphoreInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (size_t i = 0; i < frames_.size(); ++i) {
        frames_[i].commandBuffer = commandBuffers[i];
        vkCheck(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frames_[i].imageAvailable), "vkCreateSemaphore failed");
        vkCheck(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frames_[i].renderFinished), "vkCreateSemaphore failed");
        vkCheck(vkCreateFence(device_, &fenceInfo, nullptr, &frames_[i].inFlight), "vkCreateFence failed");
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
    initInfo.Instance = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device = device_;
    initInfo.QueueFamily = queueFamilies_.graphics;
    initInfo.Queue = graphicsQueue_;
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

void VulkanRenderer::renderDebugUi(VkCommandBuffer commandBuffer) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
#else
    (void)commandBuffer;
#endif
}

void VulkanRenderer::recreateSwapchain()
{
    if (!swapchainResources_.canRecreate()) {
        ++swapchainRecreationDeferrals_;
        return;
    }

    vkDeviceWaitIdle(device_);
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

void VulkanRenderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    const RenderFrameData& frameData,
    const UiDrawData& uiDrawData)
{
    const VkExtent2D extent = swapchainResources_.extent();
    const VkExtent2D renderExtent = swapchainResources_.renderExtent();
    pendingStats_ = {
        .frameIndex = nextStatsFrameIndex_++,
        .totalTiles = static_cast<uint32_t>(frameData.tiles.size()),
        .swapchainWidth = extent.width,
        .swapchainHeight = extent.height,
        .swapchainImages = swapchainResources_.imageCount(),
        .renderWidth = renderExtent.width,
        .renderHeight = renderExtent.height,
        .renderScalePercent = static_cast<uint32_t>(
            swapchainResources_.renderScalePercent()),
        .activeSamples = sampleCountValue(),
        .wireframeEnabled = wireframeEnabled_,
        .wireframeLineWidth = wireframeLineWidth_,
        .pipelineRebuilds = pipelineRebuilds_,
        .swapchainRecreations = swapchainRecreations_,
        .swapchainRecreationDeferrals = swapchainRecreationDeferrals_,
    };

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer failed");
    swapchainResources_.beginFrame(commandBuffer, imageIndex, pendingStats_);

    recordGameRendering(
        commandBuffer,
        swapchainResources_.renderColorView(),
        swapchainResources_.resolveColorView(),
        frameData);

    ssaoPass_.record(
        commandBuffer,
        swapchainResources_.resolvedColorView(),
        swapchainResources_.depthSourceImage(),
        frameData.lighting.ambientOcclusion,
        sceneDescriptors_.set(currentFrame_),
        pipelines_.layout(),
        {
            .occlusion = pipelines_.ssao(),
            .composite = pipelines_.ssaoComposite(),
            .visualize = pipelines_.ssaoVisualize(),
        },
        pendingStats_);

    swapchainResources_.upscaleSceneToSwapchain(
        commandBuffer,
        imageIndex,
        pendingStats_);

    recordOverlayRendering(
        commandBuffer,
        swapchainResources_.image(imageIndex),
        swapchainResources_.imageView(imageIndex),
        uiDrawData);
    swapchainResources_.endFrame(commandBuffer, imageIndex, pendingStats_);

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer failed");
    lastStats_ = pendingStats_;
}

void VulkanRenderer::recordShadowMapRendering(
    VkCommandBuffer commandBuffer,
    const RenderFrameData& frameData,
    const ShadowRenderLayout& layout)
{
    shadowPass_.begin(commandBuffer, pipelines_.shadow(), pendingStats_);

    auto drawTileFaces = [&](const RenderFrameData::Tile& tile) {
        const float x = tile.position.x;
        const float y = tile.position.y;
        const float width = tile.size.x;
        const float depth = tile.size.y;
        const float base = tile.baseElevation;
        const float height = std::max(tile.height, 0.0f);
        const Vec3 a { x, y, base };
        const Vec3 b { x + width, y, base };
        const Vec3 c { x + width, y + depth, base };
        const Vec3 d { x, y + depth, base };

        if (height <= 0.0f) {
            drawShadowFace(commandBuffer, {
                projectShadowPoint(layout, a),
                projectShadowPoint(layout, b),
                projectShadowPoint(layout, c),
                projectShadowPoint(layout, d),
            });
            return;
        }

        const float top = base + height;
        const Vec3 e { x, y, top };
        const Vec3 f { x + width, y, top };
        const Vec3 g { x + width, y + depth, top };
        const Vec3 h { x, y + depth, top };
        drawShadowFace(commandBuffer, { projectShadowPoint(layout, a), projectShadowPoint(layout, b), projectShadowPoint(layout, f), projectShadowPoint(layout, e) });
        drawShadowFace(commandBuffer, { projectShadowPoint(layout, b), projectShadowPoint(layout, c), projectShadowPoint(layout, g), projectShadowPoint(layout, f) });
        drawShadowFace(commandBuffer, { projectShadowPoint(layout, c), projectShadowPoint(layout, d), projectShadowPoint(layout, h), projectShadowPoint(layout, g) });
        drawShadowFace(commandBuffer, { projectShadowPoint(layout, d), projectShadowPoint(layout, a), projectShadowPoint(layout, e), projectShadowPoint(layout, h) });
        drawShadowFace(commandBuffer, { projectShadowPoint(layout, e), projectShadowPoint(layout, f), projectShadowPoint(layout, g), projectShadowPoint(layout, h) });
    };

    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.isEditorPreview || tile.pickOnly) {
            continue;
        }
        if (!tile.model.isCube()) {
            continue;
        }

        drawTileFaces(tile);
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        drawShadowFace(commandBuffer, {
            projectShadowPoint(layout, face.vertices[0]),
            projectShadowPoint(layout, face.vertices[1]),
            projectShadowPoint(layout, face.vertices[2]),
            projectShadowPoint(layout, face.vertices[3]),
        });
    }
    bool modelPipelineBound = false;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.isEditorPreview || tile.pickOnly || tile.model.isCube()) {
            continue;
        }
        if (!modelPipelineBound) {
            shadowPass_.bindModelPipeline(
                commandBuffer, pipelines_.modelShadow(), pendingStats_);
            modelPipelineBound = true;
        }
        drawModelShadow(commandBuffer, layout, tile);
    }
    shadowPass_.end(commandBuffer, pendingStats_);
}

void VulkanRenderer::recordGameRendering(
    VkCommandBuffer commandBuffer,
    VkImageView colorView,
    VkImageView resolveView,
    const RenderFrameData& frameData)
{
    ShadowRenderLayout shadowLayout {};
    if (shadowPass_.valid() && pipelines_.shadow()) {
        shadowLayout = calculateShadowRenderLayout(frameData);
        recordShadowMapRendering(commandBuffer, frameData, shadowLayout);
    }

    const bool hasBlurredTiles = frameData.viewMode == RenderViewMode::Isometric3D &&
        std::ranges::any_of(frameData.tiles, [](const RenderFrameData::Tile& tile) {
            return tile.blurBehind;
        });

    swapchainResources_.ensureSceneColorReadable(commandBuffer, pendingStats_);

    recordScenePass(
        commandBuffer,
        colorView,
        resolveView,
        frameData,
        shadowLayout,
        false,
        false,
        hasBlurredTiles || !resolveView,
        false,
        true);

    if (!hasBlurredTiles) {
        return;
    }

    swapchainResources_.copyResolvedSceneColor(commandBuffer, pendingStats_);
    recordScenePass(
        commandBuffer,
        colorView,
        resolveView,
        frameData,
        shadowLayout,
        true,
        true,
        !resolveView,
        true,
        false);
}

void VulkanRenderer::recordScenePass(
    VkCommandBuffer commandBuffer,
    VkImageView colorView,
    VkImageView resolveView,
    const RenderFrameData& frameData,
    const ShadowRenderLayout& shadowLayout,
    bool translucentPass,
    bool loadColor,
    bool storeColor,
    bool loadDepth,
    bool writeDepth)
{
    VkClearValue clearValue {
        .color = { { 0.03f, 0.04f, 0.06f, 1.0f } },
    };

    VkRenderingAttachmentInfo colorAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = resolveView ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
        .resolveImageView = resolveView,
        .resolveImageLayout = resolveView ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = loadColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = storeColor ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = clearValue,
    };

    VkClearValue depthClearValue {
        .depthStencil = { .depth = 1.0f, .stencil = 0 },
    };
    VkRenderingAttachmentInfo depthAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainResources_.depthView(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = swapchainResources_.resolveDepthView() ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT : VK_RESOLVE_MODE_NONE,
        .resolveImageView = swapchainResources_.resolveDepthView(),
        .resolveImageLayout = swapchainResources_.resolveDepthView()
            ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = loadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = depthClearValue,
    };

    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = swapchainResources_.renderExtent() },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = swapchainResources_.depthView() ? &depthAttachment : nullptr,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++pendingStats_.renderPasses;

    VkViewport viewport {
        .x = 0.0f,
        .y = static_cast<float>(swapchainResources_.renderExtent().height),
        .width = static_cast<float>(swapchainResources_.renderExtent().width),
        .height = -static_cast<float>(swapchainResources_.renderExtent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor {
        .offset = { 0, 0 },
        .extent = swapchainResources_.renderExtent(),
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.scene());
    ++pendingStats_.pipelineBinds;
    if (sceneDescriptors_.set(currentFrame_)) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.layout(), 0, 1, &sceneDescriptors_.set(currentFrame_), 0, nullptr);
    }
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, wireframeEnabled_ ? wireframeLineWidth_ : 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, swapchainResources_.depthView() ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(commandBuffer, swapchainResources_.depthView() && writeDepth ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);

    if (frameData.viewMode == RenderViewMode::Isometric3D) {
        const IsoRenderLayout isoLayout = calculateIsoRenderLayout(frameData);
        drawIsoFrame(commandBuffer, isoLayout, shadowLayout, frameData, frameData.lighting, translucentPass);
    } else {
        const TileRenderLayout tileLayout = calculateTileRenderLayout(frameData);
        for (const auto& tile : frameData.tiles) {
            drawTile(commandBuffer, tileLayout, tile, frameData.lighting);
        }
        if (!translucentPass) {
            vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
            drawTopDownGridOverlay(commandBuffer, tileLayout, frameData);
        }
    }

    vkCmdEndRendering(commandBuffer);
}

void VulkanRenderer::recordOverlayRendering(
    VkCommandBuffer commandBuffer,
    VkImage colorImage,
    VkImageView colorView,
    const UiDrawData& uiDrawData)
{
    const bool hasGameUi = !uiDrawData.commands.empty() &&
        uiDrawData.viewportSize.x > 0.0f &&
        uiDrawData.viewportSize.y > 0.0f;
#if !SOKOBAN_ENABLE_DEBUG_UI
    if (!hasGameUi) {
        return;
    }
#endif

    // Make prior scene/post-process writes visible to this attachment LOAD.
    VkImageMemoryBarrier2 overlayBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
    VkDependencyInfo overlayDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &overlayBarrier,
    };
    vkCmdPipelineBarrier2(commandBuffer, &overlayDependency);
    ++pendingStats_.imageBarriers;

    VkRenderingAttachmentInfo colorAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = swapchainResources_.extent() },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++pendingStats_.renderPasses;

    if (hasGameUi) {
        VkViewport viewport {
            .x = 0.0f,
            .y = static_cast<float>(swapchainResources_.extent().height),
            .width = static_cast<float>(swapchainResources_.extent().width),
            .height = -static_cast<float>(swapchainResources_.extent().height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        VkRect2D scissor {
            .offset = { 0, 0 },
            .extent = swapchainResources_.extent(),
        };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.ui());
        ++pendingStats_.pipelineBinds;
        if (sceneDescriptors_.set(currentFrame_)) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.layout(), 0, 1, &sceneDescriptors_.set(currentFrame_), 0, nullptr);
        }
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
        vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetLineWidth(commandBuffer, 1.0f);
        vkCmdSetDepthTestEnable(commandBuffer, VK_FALSE);
        vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
        vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_ALWAYS);

        const RenderFrameData::Lighting unlitLighting {};
        for (const UiDrawCommand& command : uiDrawData.commands) {
            drawUiRect(commandBuffer, command, uiDrawData.viewportSize, unlitLighting);
        }
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    renderDebugUi(commandBuffer);
#endif
    vkCmdEndRendering(commandBuffer);
}

VulkanRenderer::TileRenderLayout VulkanRenderer::calculateTileRenderLayout(const RenderFrameData& frameData) const
{
    const BoardPixelLayout pixelLayout = calculateBoardPixelLayout(
        { static_cast<float>(swapchainResources_.renderExtent().width), static_cast<float>(swapchainResources_.renderExtent().height) },
        frameData.levelWidth,
        frameData.levelHeight);
    const Vec2 tileSize = pixelSizeToClipSpace(pixelLayout.tileSize);
    const Vec2 boardBottomLeft {
        -1.0f + 2.0f * pixelLayout.bottomLeft.x / static_cast<float>(swapchainResources_.renderExtent().width),
        1.0f - 2.0f * pixelLayout.bottomLeft.y / static_cast<float>(swapchainResources_.renderExtent().height),
    };

    return {
        .boardBottomLeft = boardBottomLeft,
        .tileSize = tileSize,
    };
}

VulkanRenderer::IsoRenderLayout VulkanRenderer::calculateIsoRenderLayout(const RenderFrameData& frameData) const
{
    if (frameData.levelWidth == 0 || frameData.levelHeight == 0) {
        return {};
    }

    constexpr float radiansPerDegree = 3.14159265358979323846f / 180.0f;
    const float pitch = config::boardPitchDegrees * radiansPerDegree;
    const float cameraDistance = std::max(
        static_cast<float>(std::max(frameData.levelWidth, frameData.levelHeight)),
        1.0f) * config::perspectiveCameraDistanceScale;
    const Vec3 target {
        static_cast<float>(frameData.levelWidth) * 0.5f,
        static_cast<float>(frameData.levelHeight) * 0.5f,
        static_cast<float>(std::max(frameData.levelDepth, 1U) - 1U) * 0.5f,
    };
    const Vec3 cameraPosition {
        target.x,
        target.y + std::sin(pitch) * cameraDistance,
        target.z + std::cos(pitch) * cameraDistance,
    };
    const Vec3 cameraForward = normalize(subtract(target, cameraPosition));
    const Vec3 cameraRight = normalize(cross({ 0.0f, 0.0f, 1.0f }, cameraForward));
    const Vec3 cameraUp = normalize(cross(cameraForward, cameraRight));
    const float focalLength = 1.0f / std::tan(config::perspectiveFovDegrees * radiansPerDegree * 0.5f);

    IsoRenderLayout layout {
        .cameraPosition = cameraPosition,
        .cameraRight = cameraRight,
        .cameraUp = cameraUp,
        .cameraForward = cameraForward,
        .projectedCenter = {},
        .focalLength = focalLength,
        .fitScale = 1.0f,
    };

    Vec2 minPoint { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Vec2 maxPoint { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    float nearestDepth = std::numeric_limits<float>::max();
    float farthestDepth = std::numeric_limits<float>::lowest();

    auto includePoint = [&](Vec3 worldPoint) {
        const Vec3 projected = projectIsoPoint(layout, worldPoint);
        minPoint.x = std::min(minPoint.x, projected.x);
        minPoint.y = std::min(minPoint.y, projected.y);
        maxPoint.x = std::max(maxPoint.x, projected.x);
        maxPoint.y = std::max(maxPoint.y, projected.y);

        const float cameraDepth = dot(subtract(worldPoint, layout.cameraPosition), layout.cameraForward);
        nearestDepth = std::min(nearestDepth, cameraDepth);
        farthestDepth = std::max(farthestDepth, cameraDepth);
    };
    const float levelTop = static_cast<float>(std::max(frameData.levelDepth, 1U));
    includePoint({ 0.0f, 0.0f, 0.0f });
    includePoint({ static_cast<float>(frameData.levelWidth), 0.0f, 0.0f });
    includePoint({ static_cast<float>(frameData.levelWidth), static_cast<float>(frameData.levelHeight), 0.0f });
    includePoint({ 0.0f, static_cast<float>(frameData.levelHeight), 0.0f });
    includePoint({ 0.0f, 0.0f, levelTop });
    includePoint({ static_cast<float>(frameData.levelWidth), 0.0f, levelTop });
    includePoint({ static_cast<float>(frameData.levelWidth), static_cast<float>(frameData.levelHeight), levelTop });
    includePoint({ 0.0f, static_cast<float>(frameData.levelHeight), levelTop });
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.isEditorPreview) {
            continue;
        }

        const float x = tile.position.x;
        const float y = tile.position.y;
        const float width = tile.size.x;
        const float height = tile.size.y;
        const float base = tile.baseElevation;
        const float top = base + std::max(tile.height, 0.0f);
        includePoint({ x, y, base });
        includePoint({ x + width, y, base });
        includePoint({ x + width, y + height, base });
        includePoint({ x, y + height, base });
        includePoint({ x, y, top });
        includePoint({ x + width, y, top });
        includePoint({ x + width, y + height, top });
        includePoint({ x, y + height, top });
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        for (Vec3 vertex : face.vertices) {
            includePoint(vertex);
        }
    }

    const Vec2 sceneSize {
        std::max(maxPoint.x - minPoint.x, 0.001f),
        std::max(maxPoint.y - minPoint.y, 0.001f),
    };
    layout.projectedCenter = {
        (minPoint.x + maxPoint.x) * 0.5f,
        (minPoint.y + maxPoint.y) * 0.5f,
    };
    layout.fitScale = 1.82f * std::min(1.0f / sceneSize.x, 1.0f / sceneSize.y);
    layout.nearestDepth = nearestDepth;
    layout.farthestDepth = std::max(farthestDepth, nearestDepth + 0.001f);
    return layout;
}

VulkanRenderer::ShadowRenderLayout VulkanRenderer::calculateShadowRenderLayout(const RenderFrameData& frameData) const
{
    const Vec3 lightDirection = normalize(frameData.lighting.sun.direction);
    const Vec3 lightForward = normalize(multiply(lightDirection.x == 0.0f && lightDirection.y == 0.0f && lightDirection.z == 0.0f
            ? Vec3 { 0.0f, 0.0f, 1.0f }
            : lightDirection,
        -1.0f));
    const Vec3 referenceUp = std::abs(lightForward.z) > 0.9f
        ? Vec3 { 0.0f, 1.0f, 0.0f }
        : Vec3 { 0.0f, 0.0f, 1.0f };
    const Vec3 lightRight = normalize(cross(referenceUp, lightForward));
    const Vec3 lightUp = normalize(cross(lightForward, lightRight));

    Vec3 minPoint {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maxPoint {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    auto includePoint = [&](Vec3 worldPoint) {
        const Vec3 lightPoint {
            dot(worldPoint, lightRight),
            dot(worldPoint, lightUp),
            dot(worldPoint, lightForward),
        };
        minPoint.x = std::min(minPoint.x, lightPoint.x);
        minPoint.y = std::min(minPoint.y, lightPoint.y);
        minPoint.z = std::min(minPoint.z, lightPoint.z);
        maxPoint.x = std::max(maxPoint.x, lightPoint.x);
        maxPoint.y = std::max(maxPoint.y, lightPoint.y);
        maxPoint.z = std::max(maxPoint.z, lightPoint.z);
    };

    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.isEditorPreview) {
            continue;
        }

        const float x = tile.position.x;
        const float y = tile.position.y;
        const float width = tile.size.x;
        const float height = tile.size.y;
        const float base = tile.baseElevation;
        const float top = base + std::max(tile.height, 0.0f);
        includePoint({ x, y, base });
        includePoint({ x + width, y, base });
        includePoint({ x + width, y + height, base });
        includePoint({ x, y + height, base });
        includePoint({ x, y, top });
        includePoint({ x + width, y, top });
        includePoint({ x + width, y + height, top });
        includePoint({ x, y + height, top });
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        for (Vec3 vertex : face.vertices) {
            includePoint(vertex);
        }
    }

    if (frameData.tiles.empty() && frameData.isoFaces.empty()) {
        minPoint = { -1.0f, -1.0f, -1.0f };
        maxPoint = { 1.0f, 1.0f, 1.0f };
    }

    const float padding = config::shadowMapPadding;
    const float centerX = (minPoint.x + maxPoint.x) * 0.5f;
    const float centerY = (minPoint.y + maxPoint.y) * 0.5f;
    const float centerZ = (minPoint.z + maxPoint.z) * 0.5f;

    return {
        .lightRight = lightRight,
        .lightUp = lightUp,
        .lightForward = lightForward,
        .center = add(add(multiply(lightRight, centerX), multiply(lightUp, centerY)), multiply(lightForward, centerZ)),
        .halfWidth = std::max((maxPoint.x - minPoint.x) * 0.5f + padding, 0.5f),
        .halfHeight = std::max((maxPoint.y - minPoint.y) * 0.5f + padding, 0.5f),
        .nearestDepth = minPoint.z - padding,
        .farthestDepth = std::max(maxPoint.z + padding, minPoint.z + 0.001f),
    };
}

void VulkanRenderer::drawTile(
    VkCommandBuffer commandBuffer,
    const TileRenderLayout& layout,
    const RenderFrameData::Tile& tile,
    const RenderFrameData::Lighting& lighting) const
{
    const Vec2 origin {
        layout.boardBottomLeft.x + tile.position.x * layout.tileSize.x,
        layout.boardBottomLeft.y + tile.position.y * layout.tileSize.y,
    };
    const Vec2 size {
        layout.tileSize.x * tile.size.x,
        layout.tileSize.y * tile.size.y,
    };

    drawFace(commandBuffer, {
        Vec3 { origin.x, origin.y, 0.0f },
        Vec3 { origin.x + size.x, origin.y, 0.0f },
        Vec3 { origin.x + size.x, origin.y + size.y, 0.0f },
        Vec3 { origin.x, origin.y + size.y, 0.0f },
    }, {}, tile.color, {}, lighting);
}

void VulkanRenderer::drawIsoFrame(
    VkCommandBuffer commandBuffer,
    const IsoRenderLayout& layout,
    const ShadowRenderLayout& shadowLayout,
    const RenderFrameData& frameData,
    const RenderFrameData::Lighting& lighting,
    bool translucentPass) const
{
    // Reused across frames and passes to avoid a per-frame heap allocation
    // of what can be thousands of fat IsoFace structs. The renderer runs on a
    // single thread, so a function-local static is safe; clear() keeps the
    // grown capacity and reserve() only ever tops it up.
    static std::vector<IsoFace> faces;
    faces.clear();
    faces.reserve(frameData.tiles.size() * 5);

    auto faceDepth = [&](const std::array<Vec3, 4>& vertices) {
        float depth = 0.0f;
        for (Vec3 vertex : vertices) {
            depth += dot(subtract(vertex, layout.cameraPosition), layout.cameraForward);
        }
        return depth * 0.25f;
    };

    auto faceVisible = [&](const std::array<Vec3, 4>& vertices, Vec3 normal) {
        const Vec3 center = multiply(add(add(vertices[0], vertices[1]), add(vertices[2], vertices[3])), 0.25f);
        return dot(normal, subtract(layout.cameraPosition, center)) > 0.0f;
    };

    auto appendFace = [&](const std::array<Vec3, 4>& vertices, Vec3 normal, Vec4 color, bool blurBehind, bool showGrid, bool isEditorPreview, Vec2 gridSize) {
        if (!faceVisible(vertices, normal)) {
            return false;
        }

        faces.push_back({
            .vertices = {
                projectIsoPoint(layout, vertices[0]),
                projectIsoPoint(layout, vertices[1]),
                projectIsoPoint(layout, vertices[2]),
                projectIsoPoint(layout, vertices[3]),
            },
            .shadowVertices = {
                projectShadowPoint(shadowLayout, vertices[0]),
                projectShadowPoint(shadowLayout, vertices[1]),
                projectShadowPoint(shadowLayout, vertices[2]),
                projectShadowPoint(shadowLayout, vertices[3]),
            },
            .normal = normal,
            .color = color,
            .blurBehind = blurBehind,
            .showGrid = showGrid,
            .isEditorPreview = isEditorPreview,
            .gridSize = gridSize,
            .depth = faceDepth(vertices),
        });
        return true;
    };
    auto appendDoubleSidedFace = [&](const std::array<Vec3, 4>& vertices, Vec4 color) {
        const Vec3 normal = normalize(cross(subtract(vertices[1], vertices[0]), subtract(vertices[2], vertices[0])));
        faces.push_back({
            .vertices = {
                projectIsoPoint(layout, vertices[0]),
                projectIsoPoint(layout, vertices[1]),
                projectIsoPoint(layout, vertices[2]),
                projectIsoPoint(layout, vertices[3]),
            },
            .shadowVertices = {
                projectShadowPoint(shadowLayout, vertices[0]),
                projectShadowPoint(shadowLayout, vertices[1]),
                projectShadowPoint(shadowLayout, vertices[2]),
                projectShadowPoint(shadowLayout, vertices[3]),
            },
            .normal = normal,
            .color = color,
            .blurBehind = false,
            .showGrid = false,
            .isEditorPreview = false,
            .depth = faceDepth(vertices),
        });
    };
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.pickOnly) {
            continue;
        }
        if (tile.blurBehind != translucentPass) {
            continue;
        }
        if (!tile.model.isCube()) {
            continue;
        }

        const float x = tile.position.x;
        const float y = tile.position.y;
        const float width = tile.size.x;
        const float depth = tile.size.y;
        const float base = tile.baseElevation;
        const float height = std::max(tile.height, 0.0f);
        const Vec3 a { x, y, base };
        const Vec3 b { x + width, y, base };
        const Vec3 c { x + width, y + depth, base };
        const Vec3 d { x, y + depth, base };

        if (height <= 0.0f) {
            const std::array<Vec3, 4> face { a, b, c, d };
            appendFace(face, { 0.0f, 0.0f, 1.0f }, tile.color, tile.blurBehind, tile.showGrid, tile.isEditorPreview, { width, depth });
            continue;
        }

        const float top = base + height;
        const Vec3 e { x, y, top };
        const Vec3 f { x + width, y, top };
        const Vec3 g { x + width, y + depth, top };
        const Vec3 h { x, y + depth, top };
        const std::array<Vec3, 4> nearFace { a, b, f, e };
        const std::array<Vec3, 4> rightFace { b, c, g, f };
        const std::array<Vec3, 4> farFace { c, d, h, g };
        const std::array<Vec3, 4> leftFace { d, a, e, h };
        const std::array<Vec3, 4> topFace { e, f, g, h };
        appendFace(nearFace, { 0.0f, -1.0f, 0.0f }, tile.color, tile.blurBehind, tile.showGrid, tile.isEditorPreview, { width, height });
        appendFace(rightFace, { 1.0f, 0.0f, 0.0f }, tile.color, tile.blurBehind, tile.showGrid, tile.isEditorPreview, { depth, height });
        appendFace(farFace, { 0.0f, 1.0f, 0.0f }, tile.color, tile.blurBehind, tile.showGrid, tile.isEditorPreview, { width, height });
        appendFace(leftFace, { -1.0f, 0.0f, 0.0f }, tile.color, tile.blurBehind, tile.showGrid, tile.isEditorPreview, { depth, height });
        appendFace(topFace, { 0.0f, 0.0f, 1.0f }, tile.color, tile.blurBehind, tile.showGrid, tile.isEditorPreview, { width, depth });
    }
    if (!translucentPass) {
        for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
            appendDoubleSidedFace(face.vertices, face.color);
        }
    }

    std::ranges::sort(faces, [](const IsoFace& left, const IsoFace& right) {
        return left.depth > right.depth;
    });

    for (const IsoFace& face : faces) {
        drawFace(
            commandBuffer,
            face.vertices,
            face.shadowVertices,
            face.color,
            face.normal,
            lighting,
            face.blurBehind,
            face.showGrid ? frameData.gridOverlay.color : Vec4 {},
            face.gridSize,
            frameData.gridOverlay.width,
            face.isEditorPreview);
    }

    bool modelPipelineBound = false;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.pickOnly ||
            tile.blurBehind != translucentPass ||
            tile.model.isCube()) {
            continue;
        }
        if (!modelPipelineBound) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.model());
            ++pendingStats_.pipelineBinds;
            modelPipelineBound = true;
        }
        drawModel(commandBuffer, layout, shadowLayout, tile, lighting);
    }
}

void VulkanRenderer::drawTopDownGridOverlay(
    VkCommandBuffer commandBuffer,
    const TileRenderLayout& layout,
    const RenderFrameData& frameData) const
{
    if (frameData.levelWidth == 0 || frameData.levelHeight == 0 || frameData.gridOverlay.color.w <= 0.0f) {
        return;
    }

    const float tileWidthPixels = std::max(layout.tileSize.x * static_cast<float>(swapchainResources_.renderExtent().width) * 0.5f, 0.001f);
    const float lineWidth = std::clamp(frameData.gridOverlay.width / tileWidthPixels, 0.0f, 0.5f);
    if (lineWidth <= 0.0f) {
        return;
    }

    const RenderFrameData::Lighting unlitLighting {};
    const float gridDepth = 0.0f;
    const float levelWidth = static_cast<float>(frameData.levelWidth);
    const float levelHeight = static_cast<float>(frameData.levelHeight);
    const float halfLineWidth = lineWidth * 0.5f;

    auto drawRect = [&](float leftTile, float bottomTile, float rightTile, float topTile) {
        if (rightTile <= leftTile || topTile <= bottomTile) {
            return;
        }

        const Vec2 min {
            layout.boardBottomLeft.x + leftTile * layout.tileSize.x,
            layout.boardBottomLeft.y + bottomTile * layout.tileSize.y,
        };
        const Vec2 max {
            layout.boardBottomLeft.x + rightTile * layout.tileSize.x,
            layout.boardBottomLeft.y + topTile * layout.tileSize.y,
        };

        drawFace(commandBuffer, {
            Vec3 { min.x, min.y, gridDepth },
            Vec3 { max.x, min.y, gridDepth },
            Vec3 { max.x, max.y, gridDepth },
            Vec3 { min.x, max.y, gridDepth },
        }, {}, frameData.gridOverlay.color, {}, unlitLighting);
    };

    for (uint32_t x = 0; x <= frameData.levelWidth; ++x) {
        const float center = static_cast<float>(x);
        drawRect(
            std::clamp(center - halfLineWidth, 0.0f, levelWidth),
            0.0f,
            std::clamp(center + halfLineWidth, 0.0f, levelWidth),
            levelHeight);
    }
    for (uint32_t y = 0; y <= frameData.levelHeight; ++y) {
        const float center = static_cast<float>(y);
        drawRect(
            0.0f,
            std::clamp(center - halfLineWidth, 0.0f, levelHeight),
            levelWidth,
            std::clamp(center + halfLineWidth, 0.0f, levelHeight));
    }
}

void VulkanRenderer::drawFace(
    VkCommandBuffer commandBuffer,
    const std::array<Vec3, 4>& vertices,
    const std::array<Vec4, 4>& shadowVertices,
    Vec4 color,
    Vec3 normal,
    const RenderFrameData::Lighting& lighting,
    bool blurBehind,
    Vec4 gridColor,
    Vec2 gridSize,
    float gridLineWidth,
    bool isEditorPreview) const
{
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    ++pendingStats_.visibleFaces;
    ++pendingStats_.drawCalls;
    pendingStats_.vertices += 6;
    pendingStats_.triangles += 2;

    const Vec3 sunRadiance {
        lighting.sun.color.x * lighting.sun.intensity,
        lighting.sun.color.y * lighting.sun.intensity,
        lighting.sun.color.z * lighting.sun.intensity,
    };
    const Vec3 ambientRadiance {
        lighting.ambient.color.x * lighting.ambient.intensity,
        lighting.ambient.color.y * lighting.ambient.intensity,
        lighting.ambient.color.z * lighting.ambient.intensity,
    };

    const TilePushConstants pushConstants {
        .vertices = {
            Vec4 { vertices[0].x, vertices[0].y, vertices[0].z, 1.0f },
            Vec4 { vertices[1].x, vertices[1].y, vertices[1].z, 1.0f },
            Vec4 { vertices[2].x, vertices[2].y, vertices[2].z, 1.0f },
            Vec4 { vertices[3].x, vertices[3].y, vertices[3].z, 1.0f },
        },
        .shadowVertices = shadowVertices,
        .color = color,
        .normalAndAmbientRed = { normal.x, normal.y, normal.z, ambientRadiance.x },
        .sunDirectionAndAmbientGreen = { lighting.sun.direction.x, lighting.sun.direction.y, lighting.sun.direction.z, ambientRadiance.y },
        .sunRadianceAndAmbientBlue = { sunRadiance.x, sunRadiance.y, sunRadiance.z, ambientRadiance.z },
        .shadowOptions = {
            lighting.shadows.enabled ? 1.0f : 0.0f,
            std::clamp(lighting.shadows.opacity, 0.0f, 1.0f),
            std::max(lighting.shadows.bias, 0.0f),
            gridColor.w > 0.0f && gridLineWidth > 0.0f && gridSize.x > 0.0f && gridSize.y > 0.0f ? gridLineWidth : 0.0f,
        },
        .materialOptions = {
            blurBehind ? 1.0f : 0.0f,
            gridSize.x,
            gridSize.y,
            isEditorPreview ? -config::iceBlurRadiusPixels : config::iceBlurRadiusPixels,
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
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
}

void VulkanRenderer::drawShadowFace(VkCommandBuffer commandBuffer, const std::array<Vec4, 4>& shadowVertices) const
{
    const TilePushConstants pushConstants {
        .shadowVertices = shadowVertices,
    };

    vkCmdPushConstants(
        commandBuffer,
        pipelines_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
}

void VulkanRenderer::drawModel(
    VkCommandBuffer commandBuffer,
    const IsoRenderLayout& layout,
    const ShadowRenderLayout& shadowLayout,
    const RenderFrameData::Tile& tile,
    const RenderFrameData::Lighting& lighting) const
{
    const VulkanModelResources::MeshView mesh = modelResources_.meshForModel(tile.model);
    const VulkanModelResources::MaterialBinding material = modelResources_.materialForModel(tile.model);
    const ModelTransformPoints transform = modelTransformPoints(tile);
    auto isoPoint = [&](Vec3 point) {
        const Vec3 projected = projectIsoPoint(layout, point);
        return Vec4 { projected.x, projected.y, projected.z, 1.0f };
    };

    const Vec3 sunRadiance {
        lighting.sun.color.x * lighting.sun.intensity,
        lighting.sun.color.y * lighting.sun.intensity,
        lighting.sun.color.z * lighting.sun.intensity,
    };
    const Vec3 ambientRadiance {
        lighting.ambient.color.x * lighting.ambient.intensity,
        lighting.ambient.color.y * lighting.ambient.intensity,
        lighting.ambient.color.z * lighting.ambient.intensity,
    };
    const TilePushConstants pushConstants {
        .vertices = affineTransformColumns(
            isoPoint(transform.origin),
            isoPoint(transform.xPoint),
            isoPoint(transform.yPoint),
            isoPoint(transform.zPoint)),
        .shadowVertices = affineTransformColumns(
            projectShadowPoint(shadowLayout, transform.origin),
            projectShadowPoint(shadowLayout, transform.xPoint),
            projectShadowPoint(shadowLayout, transform.yPoint),
            projectShadowPoint(shadowLayout, transform.zPoint)),
        .color = tile.color,
        .normalAndAmbientRed = { 0.0f, 0.0f, 0.0f, ambientRadiance.x },
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
            std::clamp(lighting.shadows.opacity * std::clamp(lighting.modelShadowReceive, 0.0f, 1.0f), 0.0f, 1.0f),
            std::max(lighting.shadows.bias, 0.0f),
            0.0f,
        },
        .materialOptions = {
            tile.blurBehind ? 1.0f : 0.0f,
            tile.beltScrollOffset,
            static_cast<float>(material.textureIndex),
            tile.isEditorPreview ? -config::iceBlurRadiusPixels : config::iceBlurRadiusPixels,
        },
        .textureOptions = {
            shaderValue(material.mode),
            static_cast<float>(tile.modelRotationQuarterTurns % 4),
            std::max(lighting.specularStrength, 0.0f),
            std::max(lighting.specularPower, 1.0f),
        },
    };

    const VkBuffer vertexBuffer = mesh.vertexBuffer;
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(
        commandBuffer,
        pipelines_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);

    pendingStats_.visibleFaces += mesh.indexCount / 3;
    ++pendingStats_.drawCalls;
    pendingStats_.vertices += mesh.indexCount;
    pendingStats_.triangles += mesh.indexCount / 3;
}

void VulkanRenderer::drawModelShadow(
    VkCommandBuffer commandBuffer,
    const ShadowRenderLayout& layout,
    const RenderFrameData::Tile& tile) const
{
    const VulkanModelResources::MeshView mesh = modelResources_.meshForModel(tile.model);
    const ModelTransformPoints transform = modelTransformPoints(tile);
    const TilePushConstants pushConstants {
        .shadowVertices = affineTransformColumns(
            projectShadowPoint(layout, transform.origin),
            projectShadowPoint(layout, transform.xPoint),
            projectShadowPoint(layout, transform.yPoint),
            projectShadowPoint(layout, transform.zPoint)),
    };

    const VkBuffer vertexBuffer = mesh.vertexBuffer;
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(
        commandBuffer,
        pipelines_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
}

void VulkanRenderer::drawUiRect(
    VkCommandBuffer commandBuffer,
    const UiDrawCommand& command,
    Vec2 viewportSize,
    const RenderFrameData::Lighting& lighting) const
{
    const float left = -1.0f + 2.0f * command.rect.position.x / viewportSize.x;
    const float right = -1.0f + 2.0f * (command.rect.position.x + command.rect.size.x) / viewportSize.x;
    const float top = 1.0f - 2.0f * command.rect.position.y / viewportSize.y;
    const float bottom = 1.0f - 2.0f * (command.rect.position.y + command.rect.size.y) / viewportSize.y;

    const std::array vertices {
        Vec3 { left, top, 0.0f },
        Vec3 { right, top, 0.0f },
        Vec3 { right, bottom, 0.0f },
        Vec3 { left, bottom, 0.0f },
    };
    if (command.kind == UiDrawKind::Solid) {
        drawFace(commandBuffer, vertices, {}, command.color, {}, lighting);
        return;
    }

    ++pendingStats_.visibleFaces;
    ++pendingStats_.drawCalls;
    pendingStats_.vertices += 6;
    pendingStats_.triangles += 2;
    const float materialMode = command.kind == UiDrawKind::FontGlyph ? 3.0f : 4.0f;
    const TilePushConstants pushConstants {
        .vertices = {
            Vec4 { vertices[0].x, vertices[0].y, vertices[0].z, 1.0f },
            Vec4 { vertices[1].x, vertices[1].y, vertices[1].z, 1.0f },
            Vec4 { vertices[2].x, vertices[2].y, vertices[2].z, 1.0f },
            Vec4 { vertices[3].x, vertices[3].y, vertices[3].z, 1.0f },
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
        .textureOptions = { materialMode, 0.0f, 0.0f, 1.0f },
    };
    vkCmdPushConstants(
        commandBuffer,
        pipelines_.layout(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TilePushConstants),
        &pushConstants);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
}

Vec3 VulkanRenderer::projectIsoPoint(const IsoRenderLayout& layout, Vec3 point) const
{
    const Vec3 relative = subtract(point, layout.cameraPosition);
    const float cameraX = dot(relative, layout.cameraRight);
    const float cameraY = dot(relative, layout.cameraUp);
    const float cameraZ = std::max(dot(relative, layout.cameraForward), 0.001f);
    const float aspect = static_cast<float>(swapchainResources_.renderExtent().width) / static_cast<float>(swapchainResources_.renderExtent().height);
    const Vec2 projected {
        layout.focalLength * cameraX / (cameraZ * aspect),
        layout.focalLength * cameraY / cameraZ,
    };

    const float depthRange = std::max(layout.farthestDepth - layout.nearestDepth, 0.001f);
    const float normalizedDepth = std::clamp((cameraZ - layout.nearestDepth) / depthRange, 0.0f, 1.0f);

    return {
        (projected.x - layout.projectedCenter.x) * layout.fitScale,
        (projected.y - layout.projectedCenter.y) * layout.fitScale,
        normalizedDepth,
    };
}

Vec4 VulkanRenderer::projectShadowPoint(const ShadowRenderLayout& layout, Vec3 point) const
{
    const Vec3 relative = subtract(point, layout.center);
    const float x = dot(relative, layout.lightRight) / std::max(layout.halfWidth, 0.001f);
    const float y = dot(relative, layout.lightUp) / std::max(layout.halfHeight, 0.001f);
    const float depth = dot(point, layout.lightForward);
    const float depthRange = std::max(layout.farthestDepth - layout.nearestDepth, 0.001f);
    const float z = std::clamp((depth - layout.nearestDepth) / depthRange, 0.0f, 1.0f);
    return { x, y, z, 1.0f };
}

Vec2 VulkanRenderer::pixelSizeToClipSpace(float pixelSize) const
{
    // Scene pixel sizes are measured against the internal render target, while shader positions are
    // in clip space where the visible width and height each span -1 to +1.
    // Multiplying by 2 converts a screen fraction into that two-unit range.
    return {
        2.0f * pixelSize / static_cast<float>(swapchainResources_.renderExtent().width),
        2.0f * pixelSize / static_cast<float>(swapchainResources_.renderExtent().height),
    };
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport), "vkGetPhysicalDeviceSurfaceSupportKHR failed");
        if (presentSupport) {
            indices.present = i;
        }

        if (indices.complete()) {
            break;
        }
    }

    return indices;
}

bool VulkanRenderer::isDeviceSuitable(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
        (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 && VK_API_VERSION_MINOR(properties.apiVersion) < 4)) {
        return false;
    }
    if (properties.limits.maxPushConstantsSize < sizeof(TilePushConstants)) {
        return false;
    }

    const QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.complete()) {
        return false;
    }

    uint32_t extensionCount = 0;
    vkCheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr), "vkEnumerateDeviceExtensionProperties failed");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkCheck(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()), "vkEnumerateDeviceExtensionProperties failed");

    std::set<std::string> missing(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());
    for (const auto& extension : extensions) {
        missing.erase(extension.extensionName);
    }
    if (!missing.empty()) {
        return false;
    }

    uint32_t formatCount = 0;
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (formatCount == 0 || presentModeCount == 0) {
        return false;
    }

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
    };
    VkPhysicalDeviceVulkan13Features vulkan13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extendedDynamicState,
    };
    VkPhysicalDeviceFeatures2 features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13,
    };

    vkGetPhysicalDeviceFeatures2(device, &features);

    return vulkan13.dynamicRendering &&
        vulkan13.synchronization2 &&
        features.features.fillModeNonSolid &&
        extendedDynamicState.extendedDynamicState;
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

    if (requested == VK_SAMPLE_COUNT_1_BIT) {
        return requested;
    }

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    const VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts &
        properties.limits.framebufferDepthSampleCounts;
    if (supported & requested) {
        return requested;
    }
    if (requested >= VK_SAMPLE_COUNT_8_BIT && (supported & VK_SAMPLE_COUNT_4_BIT)) {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    if (requested >= VK_SAMPLE_COUNT_4_BIT && (supported & VK_SAMPLE_COUNT_2_BIT)) {
        return VK_SAMPLE_COUNT_2_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

bool VulkanRenderer::msaaEnabled() const
{
    return activeSampleCount_ != VK_SAMPLE_COUNT_1_BIT;
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
