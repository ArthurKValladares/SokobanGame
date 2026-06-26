#include "engine/render/VulkanRenderer.hpp"

#include "engine/BoardLayout.hpp"
#include "engine/Config.hpp"
#include "engine/render/ImageData.hpp"

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
#include <cstddef>
#include <cstring>
#include <fstream>
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

constexpr std::array<const char*, 4> requiredDeviceExtensions {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
};

struct TilePushConstants {
    std::array<Vec4, 4> vertices;
    std::array<Vec4, 4> shadowVertices;
    Vec4 color;
    Vec4 normalAndAmbientRed;
    Vec4 sunDirectionAndAmbientGreen;
    Vec4 sunRadianceAndAmbientBlue;
    Vec4 shadowOptions;
    Vec4 materialOptions;
    Vec4 gridColor;
    Vec4 textureOptions;
};

static_assert(sizeof(TilePushConstants) == 256);

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

    const uint32_t quarterTurns = tile.model == RenderModel::Rogue
        ? tile.modelRotationQuarterTurns % 4
        : 0;
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

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    const auto size = static_cast<size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    return data;
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

VulkanRenderer::VulkanRenderer(SDL_Window* window, std::filesystem::path assetRoot)
    : window_(window)
    , assetRoot_(std::move(assetRoot))
{
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createDevice();
    createSwapchain();
    createImageViews();
    createMsaaColorResources();
    createDepthResources();
    createShadowResources();
    createSceneColorResources();
    createCommandPool();
    createModelResources();
    createModelTextureResources();
    createDescriptorResources();
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
    destroyDescriptorResources();
    destroyModelTextureResources();
    destroyModelResources();
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }

    cleanupShadowResources();
    cleanupSwapchain();

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
    auto& frame = frames_[currentFrame_];
    vkCheck(vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences failed");

    uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquired, "vkAcquireNextImageKHR failed");
    }

    vkCheck(vkResetFences(device_, 1, &frame.inFlight), "vkResetFences failed");
    vkCheck(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer failed");

#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Render();
#endif

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

    VkPresentInfoKHR present {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &imageIndex,
    };

    const VkResult presented = vkQueuePresentKHR(presentQueue_, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        vkCheck(presented, "vkQueuePresentKHR failed");
    }

    currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight_;
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
        swapchainExtent_.width == 0 ||
        swapchainExtent_.height == 0) {
        return std::nullopt;
    }

    const IsoRenderLayout layout = calculateIsoRenderLayout(frameData);

    auto clipToPixel = [this](Vec3 clip) {
        return Vec2 {
            (clip.x + 1.0f) * 0.5f * static_cast<float>(swapchainExtent_.width),
            (1.0f - clip.y) * 0.5f * static_cast<float>(swapchainExtent_.height),
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
        const std::array<Vec2, 4> pixelQuad {
            clipToPixel(clipVertices[0]),
            clipToPixel(clipVertices[1]),
            clipToPixel(clipVertices[2]),
            clipToPixel(clipVertices[3]),
        };
        if (!pointInQuad(pixelPosition, pixelQuad)) {
            return;
        }

        const float depth = (clipVertices[0].z + clipVertices[1].z + clipVertices[2].z + clipVertices[3].z) * 0.25f;
        if (depth >= pickedDepth) {
            return;
        }

        const int x = static_cast<int>(std::floor(tile.position.x + 0.0001f));
        const int y = static_cast<int>(std::floor(tile.position.y + 0.0001f));
        if (x < 0 || y < 0 || x >= static_cast<int>(frameData.levelWidth) || y >= static_cast<int>(frameData.levelHeight)) {
            return;
        }

        picked = tile.cell;
        pickedDepth = depth;
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
    cleanupMsaaColorResources();
    cleanupDepthResources();
    createMsaaColorResources();
    createDepthResources();
    createPipeline();
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

    for (VkPhysicalDevice device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            queueFamilies_ = findQueueFamilies(device);
            return;
        }
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

    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphicsPipelineLibrary {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT,
        .graphicsPipelineLibrary = VK_TRUE,
    };

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &graphicsPipelineLibrary,
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

void VulkanRenderer::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities {};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");

    uint32_t formatCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR failed");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR failed");

    uint32_t presentModeCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR failed");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR failed");

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    const VkExtent2D extent = chooseSwapchainExtent(capabilities);
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        throw std::runtime_error("Swapchain images do not support transfer source usage required for ice blur");
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    std::array queueFamilyIndices { queueFamilies_.graphics, queueFamilies_.present };
    const bool sharedQueues = queueFamilies_.graphics != queueFamilies_.present;

    VkSwapchainCreateInfoKHR createInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = sharedQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = sharedQueues ? static_cast<uint32_t>(queueFamilyIndices.size()) : 0U,
        .pQueueFamilyIndices = sharedQueues ? queueFamilyIndices.data() : nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain_,
    };

    VkSwapchainKHR oldSwapchain = swapchain_;
    vkCheck(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR failed");

    if (oldSwapchain) {
        vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    }

    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;

    uint32_t swapchainImageCount = 0;
    vkCheck(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, nullptr), "vkGetSwapchainImagesKHR failed");
    std::vector<VkImage> images(swapchainImageCount);
    vkCheck(vkGetSwapchainImagesKHR(device_, swapchain_, &swapchainImageCount, images.data()), "vkGetSwapchainImagesKHR failed");

    swapchainImages_.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        swapchainImages_[i].image = images[i];
    }
}

void VulkanRenderer::createImageViews()
{
    for (auto& image : swapchainImages_) {
        image.view = createImageView(image.image, swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanRenderer::createMsaaColorResources()
{
    if (!msaaEnabled() || swapchainExtent_.width == 0 || swapchainExtent_.height == 0) {
        return;
    }

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchainFormat_,
        .extent = {
            .width = swapchainExtent_.width,
            .height = swapchainExtent_.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = activeSampleCount_,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &msaaColorImage_.image), "vkCreateImage MSAA color failed");

    VkMemoryRequirements memoryRequirements {};
    vkGetImageMemoryRequirements(device_, msaaColorImage_.image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkCheck(vkAllocateMemory(device_, &allocateInfo, nullptr, &msaaColorImage_.memory), "vkAllocateMemory MSAA color failed");
    vkCheck(vkBindImageMemory(device_, msaaColorImage_.image, msaaColorImage_.memory, 0), "vkBindImageMemory MSAA color failed");
    msaaColorImage_.view = createImageView(msaaColorImage_.image, swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanRenderer::createDepthResources()
{
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) {
        return;
    }

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat_,
        .extent = {
            .width = swapchainExtent_.width,
            .height = swapchainExtent_.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = activeSampleCount_,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &depthImage_.image), "vkCreateImage depth failed");

    VkMemoryRequirements memoryRequirements {};
    vkGetImageMemoryRequirements(device_, depthImage_.image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkCheck(vkAllocateMemory(device_, &allocateInfo, nullptr, &depthImage_.memory), "vkAllocateMemory depth failed");
    vkCheck(vkBindImageMemory(device_, depthImage_.image, depthImage_.memory, 0), "vkBindImageMemory depth failed");
    depthImage_.view = createImageView(depthImage_.image, depthFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanRenderer::createShadowResources()
{
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = shadowFormat_,
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

    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &shadowImage_.image), "vkCreateImage shadow map failed");

    VkMemoryRequirements memoryRequirements {};
    vkGetImageMemoryRequirements(device_, shadowImage_.image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkCheck(vkAllocateMemory(device_, &allocateInfo, nullptr, &shadowImage_.memory), "vkAllocateMemory shadow map failed");
    vkCheck(vkBindImageMemory(device_, shadowImage_.image, shadowImage_.memory, 0), "vkBindImageMemory shadow map failed");
    shadowImage_.view = createImageView(shadowImage_.image, shadowFormat_, VK_IMAGE_ASPECT_DEPTH_BIT);
    shadowImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

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
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &shadowSampler_), "vkCreateSampler shadow map failed");
}

void VulkanRenderer::createSceneColorResources()
{
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) {
        return;
    }

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchainFormat_,
        .extent = {
            .width = swapchainExtent_.width,
            .height = swapchainExtent_.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &sceneColorImage_.image), "vkCreateImage scene color failed");

    VkMemoryRequirements memoryRequirements {};
    vkGetImageMemoryRequirements(device_, sceneColorImage_.image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    vkCheck(vkAllocateMemory(device_, &allocateInfo, nullptr, &sceneColorImage_.memory), "vkAllocateMemory scene color failed");
    vkCheck(vkBindImageMemory(device_, sceneColorImage_.image, sceneColorImage_.memory, 0), "vkBindImageMemory scene color failed");
    sceneColorImage_.view = createImageView(sceneColorImage_.image, swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    sceneColorImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sceneColorSampler_), "vkCreateSampler scene color failed");
}

void VulkanRenderer::createDescriptorResources()
{
    std::array<VkDescriptorSetLayoutBinding, 3> bindings {
        VkDescriptorSetLayoutBinding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        VkDescriptorSetLayoutBinding {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        VkDescriptorSetLayoutBinding {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    vkCheck(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_), "vkCreateDescriptorSetLayout failed");

    std::array<VkDescriptorPoolSize, 1> poolSizes {
        VkDescriptorPoolSize {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 3,
        },
    };
    VkDescriptorPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    vkCheck(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_), "vkCreateDescriptorPool failed");

    VkDescriptorSetAllocateInfo allocateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout_,
    };
    vkCheck(vkAllocateDescriptorSets(device_, &allocateInfo, &descriptorSet_), "vkAllocateDescriptorSets failed");
    updateDescriptorSet();
}

void VulkanRenderer::updateDescriptorSet()
{
    if (!descriptorSet_ ||
        !shadowSampler_ ||
        !shadowImage_.view ||
        !sceneColorSampler_ ||
        !sceneColorImage_.view ||
        !rogueTextureSampler_ ||
        !rogueTextureImage_.view) {
        return;
    }

    VkDescriptorImageInfo imageInfo {
        .sampler = shadowSampler_,
        .imageView = shadowImage_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo sceneImageInfo {
        .sampler = sceneColorSampler_,
        .imageView = sceneColorImage_.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo modelImageInfo {
        .sampler = rogueTextureSampler_,
        .imageView = rogueTextureImage_.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    std::array<VkWriteDescriptorSet, 3> writes {
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet_,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet_,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &sceneImageInfo,
        },
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet_,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &modelImageInfo,
        },
    };
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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

void VulkanRenderer::createModelResources()
{
    bricksAMesh_ = uploadMesh(loadGltfMesh(assetRoot_ / "models/bricks_A.gltf"));
    stoneMesh_ = uploadMesh(loadGltfMesh(assetRoot_ / "models/stone.gltf"));
    waterMesh_ = uploadMesh(loadGltfMesh(assetRoot_ / "models/water.gltf"));
    glassMesh_ = uploadMesh(loadGltfMesh(assetRoot_ / "models/glass.gltf"));
    rogueMesh_ = uploadMesh(loadGltfMesh(
        assetRoot_ / "models/Rogue.glb",
        {
            .preserveAspectRatio = true,
            .rotateHalfTurn = true,
        }));
}

VulkanRenderer::GpuMesh VulkanRenderer::uploadMesh(const MeshData& mesh) const
{
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::runtime_error("glTF mesh contains no geometry");
    }

    const VkDeviceSize vertexBytes = sizeof(MeshVertex) * mesh.vertices.size();
    const VkDeviceSize indexBytes = sizeof(uint32_t) * mesh.indices.size();
    GpuMesh result;
    result.vertexBuffer = createBuffer(
        vertexBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    result.indexBuffer = createBuffer(
        indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, result.vertexBuffer.memory, 0, vertexBytes, 0, &mapped), "vkMapMemory model vertex buffer failed");
    std::memcpy(mapped, mesh.vertices.data(), static_cast<size_t>(vertexBytes));
    vkUnmapMemory(device_, result.vertexBuffer.memory);

    mapped = nullptr;
    vkCheck(vkMapMemory(device_, result.indexBuffer.memory, 0, indexBytes, 0, &mapped), "vkMapMemory model index buffer failed");
    std::memcpy(mapped, mesh.indices.data(), static_cast<size_t>(indexBytes));
    vkUnmapMemory(device_, result.indexBuffer.memory);

    result.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return result;
}

void VulkanRenderer::destroyModelResources()
{
    destroyMesh(rogueMesh_);
    destroyMesh(glassMesh_);
    destroyMesh(waterMesh_);
    destroyMesh(stoneMesh_);
    destroyMesh(bricksAMesh_);
}

void VulkanRenderer::createModelTextureResources()
{
    const ImageData image = loadRgbaImage(assetRoot_ / "models/rogue_texture.png");
    const VkDeviceSize imageBytes = image.rgba.size();
    OwnedBuffer staging = createBuffer(
        imageBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, staging.memory, 0, imageBytes, 0, &mapped), "vkMapMemory texture staging failed");
    std::memcpy(mapped, image.rgba.data(), image.rgba.size());
    vkUnmapMemory(device_, staging.memory);

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = {
            .width = image.width,
            .height = image.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &rogueTextureImage_.image), "vkCreateImage Rogue texture failed");

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device_, rogueTextureImage_.image, &requirements);
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkCheck(vkAllocateMemory(device_, &allocationInfo, nullptr, &rogueTextureImage_.memory), "vkAllocateMemory Rogue texture failed");
    vkCheck(vkBindImageMemory(device_, rogueTextureImage_.image, rogueTextureImage_.memory, 0), "vkBindImageMemory Rogue texture failed");

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandBufferInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkCheck(vkAllocateCommandBuffers(device_, &commandBufferInfo, &commandBuffer), "vkAllocateCommandBuffers texture upload failed");
    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer texture upload failed");

    VkImageMemoryBarrier2 toTransfer {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = rogueTextureImage_.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo toTransferDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toTransfer,
    };
    vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);

    VkBufferImageCopy copyRegion {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = image.width,
            .height = image.height,
            .depth = 1,
        },
    };
    vkCmdCopyBufferToImage(
        commandBuffer,
        staging.buffer,
        rogueTextureImage_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    VkImageMemoryBarrier2 toRead {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = rogueTextureImage_.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo toReadDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toRead,
    };
    vkCmdPipelineBarrier2(commandBuffer, &toReadDependency);
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer texture upload failed");

    VkCommandBufferSubmitInfo commandBufferSubmit {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commandBuffer,
    };
    VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmit,
    };
    vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2 texture upload failed");
    vkCheck(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle texture upload failed");
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);

    vkDestroyBuffer(device_, staging.buffer, nullptr);
    vkFreeMemory(device_, staging.memory, nullptr);
    rogueTextureImage_.view = createImageView(
        rogueTextureImage_.image,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &rogueTextureSampler_), "vkCreateSampler Rogue texture failed");
}

void VulkanRenderer::destroyModelTextureResources()
{
    if (rogueTextureSampler_) {
        vkDestroySampler(device_, rogueTextureSampler_, nullptr);
        rogueTextureSampler_ = VK_NULL_HANDLE;
    }
    if (rogueTextureImage_.view) {
        vkDestroyImageView(device_, rogueTextureImage_.view, nullptr);
        rogueTextureImage_.view = VK_NULL_HANDLE;
    }
    if (rogueTextureImage_.image) {
        vkDestroyImage(device_, rogueTextureImage_.image, nullptr);
        rogueTextureImage_.image = VK_NULL_HANDLE;
    }
    if (rogueTextureImage_.memory) {
        vkFreeMemory(device_, rogueTextureImage_.memory, nullptr);
        rogueTextureImage_.memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroyMesh(GpuMesh& mesh) const
{
    if (mesh.indexBuffer.buffer) {
        vkDestroyBuffer(device_, mesh.indexBuffer.buffer, nullptr);
        mesh.indexBuffer.buffer = VK_NULL_HANDLE;
    }
    if (mesh.indexBuffer.memory) {
        vkFreeMemory(device_, mesh.indexBuffer.memory, nullptr);
        mesh.indexBuffer.memory = VK_NULL_HANDLE;
    }
    if (mesh.vertexBuffer.buffer) {
        vkDestroyBuffer(device_, mesh.vertexBuffer.buffer, nullptr);
        mesh.vertexBuffer.buffer = VK_NULL_HANDLE;
    }
    if (mesh.vertexBuffer.memory) {
        vkFreeMemory(device_, mesh.vertexBuffer.memory, nullptr);
        mesh.vertexBuffer.memory = VK_NULL_HANDLE;
    }
    mesh.indexCount = 0;
}

const VulkanRenderer::GpuMesh& VulkanRenderer::meshForModel(RenderModel model) const
{
    switch (model) {
    case RenderModel::BricksA:
        return bricksAMesh_;
    case RenderModel::Stone:
        return stoneMesh_;
    case RenderModel::Water:
        return waterMesh_;
    case RenderModel::Glass:
        return glassMesh_;
    case RenderModel::Rogue:
        return rogueMesh_;
    case RenderModel::Cube:
        break;
    }
    throw std::runtime_error("Cube tiles do not have a GPU model mesh");
}

void VulkanRenderer::createPipeline()
{
    VkPushConstantRange pushConstantRange {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(TilePushConstants),
    };

    VkPipelineLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = descriptorSetLayout_ ? 1U : 0U,
        .pSetLayouts = descriptorSetLayout_ ? &descriptorSetLayout_ : nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout failed");

    VkShaderModule vertexShader = createShaderModule(assetRoot_ / "shaders/triangle.vert.glsl.spv");
    VkShaderModule fragmentShader = createShaderModule(assetRoot_ / "shaders/triangle.frag.glsl.spv");
    VkShaderModule shadowVertexShader = createShaderModule(assetRoot_ / "shaders/shadow.vert.glsl.spv");
    VkShaderModule modelVertexShader = createShaderModule(assetRoot_ / "shaders/model.vert.glsl.spv");
    VkShaderModule modelShadowVertexShader = createShaderModule(assetRoot_ / "shaders/model_shadow.vert.glsl.spv");

    pipeline_ = createGraphicsPipeline(vertexShader, fragmentShader);
    modelPipeline_ = createModelGraphicsPipeline(modelVertexShader, fragmentShader);
    createShadowPipeline(shadowVertexShader);
    createModelShadowPipeline(modelShadowVertexShader);

    vkDestroyShaderModule(device_, modelShadowVertexShader, nullptr);
    vkDestroyShaderModule(device_, modelVertexShader, nullptr);
    vkDestroyShaderModule(device_, shadowVertexShader, nullptr);
    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, vertexShader, nullptr);

    ++pipelineRebuilds_;
}

void VulkanRenderer::createShadowPipeline(VkShaderModule shadowVertexShader)
{
    VkPipelineShaderStageCreateInfo vertexStage {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shadowVertexShader,
        .pName = "main",
    };

    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkDynamicState dynamicStates[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .depthAttachmentFormat = shadowFormat_,
    };
    VkGraphicsPipelineCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 1,
        .pStages = &vertexStage,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout_,
    };
    vkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &shadowPipeline_), "vkCreateGraphicsPipelines shadow pipeline failed");
}

void VulkanRenderer::createModelShadowPipeline(VkShaderModule shadowVertexShader)
{
    VkPipelineShaderStageCreateInfo vertexStage {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shadowVertexShader,
        .pName = "main",
    };
    const VkVertexInputBindingDescription binding {
        .binding = 0,
        .stride = sizeof(MeshVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attribute {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(MeshVertex, position),
    };
    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attribute,
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkDynamicState dynamicStates[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .depthAttachmentFormat = shadowFormat_,
    };
    VkGraphicsPipelineCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 1,
        .pStages = &vertexStage,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout_,
    };
    vkCheck(
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &modelShadowPipeline_),
        "vkCreateGraphicsPipelines model shadow pipeline failed");
}

void VulkanRenderer::destroyPipeline()
{
    if (modelShadowPipeline_) {
        vkDestroyPipeline(device_, modelShadowPipeline_, nullptr);
        modelShadowPipeline_ = VK_NULL_HANDLE;
    }
    if (shadowPipeline_) {
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (modelPipeline_) {
        vkDestroyPipeline(device_, modelPipeline_, nullptr);
        modelPipeline_ = VK_NULL_HANDLE;
    }
    for (VkPipeline& library : pipelineLibraries_) {
        if (library) {
            vkDestroyPipeline(device_, library, nullptr);
            library = VK_NULL_HANDLE;
        }
    }
    if (pipelineLayout_) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
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

    const VkFormat colorAttachmentFormat = swapchainFormat_;
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
    initInfo.ImageCount = std::max(2U, static_cast<uint32_t>(swapchainImages_.size()));
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
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width == 0 || height == 0) {
        return;
    }

    vkDeviceWaitIdle(device_);
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createMsaaColorResources();
    createDepthResources();
    createSceneColorResources();
    updateDescriptorSet();
    ++swapchainRecreations_;
}

void VulkanRenderer::cleanupSwapchain()
{
    cleanupDepthResources();
    cleanupMsaaColorResources();
    cleanupSceneColorResources();

    for (auto& image : swapchainImages_) {
        if (image.view) {
            vkDestroyImageView(device_, image.view, nullptr);
            image.view = VK_NULL_HANDLE;
        }
    }
    swapchainImages_.clear();

    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::cleanupMsaaColorResources()
{
    if (msaaColorImage_.view) {
        vkDestroyImageView(device_, msaaColorImage_.view, nullptr);
        msaaColorImage_.view = VK_NULL_HANDLE;
    }
    if (msaaColorImage_.image) {
        vkDestroyImage(device_, msaaColorImage_.image, nullptr);
        msaaColorImage_.image = VK_NULL_HANDLE;
    }
    if (msaaColorImage_.memory) {
        vkFreeMemory(device_, msaaColorImage_.memory, nullptr);
        msaaColorImage_.memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::cleanupDepthResources()
{
    depthLayoutInitialized_ = false;
    if (depthImage_.view) {
        vkDestroyImageView(device_, depthImage_.view, nullptr);
        depthImage_.view = VK_NULL_HANDLE;
    }
    if (depthImage_.image) {
        vkDestroyImage(device_, depthImage_.image, nullptr);
        depthImage_.image = VK_NULL_HANDLE;
    }
    if (depthImage_.memory) {
        vkFreeMemory(device_, depthImage_.memory, nullptr);
        depthImage_.memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroyDescriptorResources()
{
    if (descriptorPool_) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::cleanupShadowResources()
{
    shadowImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (shadowSampler_) {
        vkDestroySampler(device_, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
    }
    if (shadowImage_.view) {
        vkDestroyImageView(device_, shadowImage_.view, nullptr);
        shadowImage_.view = VK_NULL_HANDLE;
    }
    if (shadowImage_.image) {
        vkDestroyImage(device_, shadowImage_.image, nullptr);
        shadowImage_.image = VK_NULL_HANDLE;
    }
    if (shadowImage_.memory) {
        vkFreeMemory(device_, shadowImage_.memory, nullptr);
        shadowImage_.memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::cleanupSceneColorResources()
{
    sceneColorImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    if (sceneColorSampler_) {
        vkDestroySampler(device_, sceneColorSampler_, nullptr);
        sceneColorSampler_ = VK_NULL_HANDLE;
    }
    if (sceneColorImage_.view) {
        vkDestroyImageView(device_, sceneColorImage_.view, nullptr);
        sceneColorImage_.view = VK_NULL_HANDLE;
    }
    if (sceneColorImage_.image) {
        vkDestroyImage(device_, sceneColorImage_.image, nullptr);
        sceneColorImage_.image = VK_NULL_HANDLE;
    }
    if (sceneColorImage_.memory) {
        vkFreeMemory(device_, sceneColorImage_.memory, nullptr);
        sceneColorImage_.memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderFrameData& frameData, const UiDrawData& uiDrawData)
{
    pendingStats_ = {
        .frameIndex = nextStatsFrameIndex_++,
        .totalTiles = static_cast<uint32_t>(frameData.tiles.size()),
        .swapchainWidth = swapchainExtent_.width,
        .swapchainHeight = swapchainExtent_.height,
        .swapchainImages = static_cast<uint32_t>(swapchainImages_.size()),
        .activeSamples = sampleCountValue(),
        .wireframeEnabled = wireframeEnabled_,
        .wireframeLineWidth = wireframeLineWidth_,
        .pipelineRebuilds = pipelineRebuilds_,
        .swapchainRecreations = swapchainRecreations_,
    };

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer failed");

    VkImageMemoryBarrier2 swapchainToColorAttachment {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImages_[imageIndex].image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo swapchainToColorDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &swapchainToColorAttachment,
    };
    vkCmdPipelineBarrier2(commandBuffer, &swapchainToColorDependency);
    ++pendingStats_.imageBarriers;

    if (msaaEnabled()) {
        VkImageMemoryBarrier2 msaaToColorAttachment {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = msaaColorImage_.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkDependencyInfo msaaToColorDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &msaaToColorAttachment,
        };
        vkCmdPipelineBarrier2(commandBuffer, &msaaToColorDependency);
        ++pendingStats_.imageBarriers;
    }

    if (depthImage_.image) {
        VkImageMemoryBarrier2 depthToAttachment {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = depthLayoutInitialized_
                ? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                : VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = depthLayoutInitialized_
                ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                : VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = depthLayoutInitialized_
                ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depthImage_.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkDependencyInfo depthToAttachmentDependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &depthToAttachment,
        };
        vkCmdPipelineBarrier2(commandBuffer, &depthToAttachmentDependency);
        depthLayoutInitialized_ = true;
        ++pendingStats_.imageBarriers;
    }

    recordGameRendering(
        commandBuffer,
        msaaEnabled() ? msaaColorImage_.view : swapchainImages_[imageIndex].view,
        msaaEnabled() ? swapchainImages_[imageIndex].view : VK_NULL_HANDLE,
        swapchainImages_[imageIndex].image,
        frameData);

    recordUiRendering(commandBuffer, swapchainImages_[imageIndex].view, uiDrawData);
    recordDebugUiRendering(commandBuffer, swapchainImages_[imageIndex].view);

    VkImageMemoryBarrier2 toPresent {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImages_[imageIndex].image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo toPresentDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toPresent,
    };
    vkCmdPipelineBarrier2(commandBuffer, &toPresentDependency);
    ++pendingStats_.imageBarriers;

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer failed");
    lastStats_ = pendingStats_;
}

void VulkanRenderer::recordShadowMapRendering(VkCommandBuffer commandBuffer, const RenderFrameData& frameData, const ShadowRenderLayout& layout)
{
    VkImageMemoryBarrier2 shadowToAttachment {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = shadowImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = shadowImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = shadowImageLayout_,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = shadowImage_.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo shadowToAttachmentDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &shadowToAttachment,
    };
    vkCmdPipelineBarrier2(commandBuffer, &shadowToAttachmentDependency);
    shadowImageLayout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    ++pendingStats_.imageBarriers;

    VkClearValue shadowClear {
        .depthStencil = { .depth = 1.0f, .stencil = 0 },
    };
    VkRenderingAttachmentInfo depthAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = shadowImage_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = shadowClear,
    };
    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = { config::shadowMapSize, config::shadowMapSize } },
        .layerCount = 1,
        .pDepthAttachment = &depthAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++pendingStats_.renderPasses;

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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    ++pendingStats_.pipelineBinds;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthWriteEnable(commandBuffer, VK_TRUE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);

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
        if (tile.model != RenderModel::Cube) {
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
        if (tile.isEditorPreview || tile.pickOnly || tile.model == RenderModel::Cube) {
            continue;
        }
        if (!modelPipelineBound) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelShadowPipeline_);
            ++pendingStats_.pipelineBinds;
            modelPipelineBound = true;
        }
        drawModelShadow(commandBuffer, layout, tile);
    }

    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 shadowToRead {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = shadowImage_.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkDependencyInfo shadowToReadDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &shadowToRead,
    };
    vkCmdPipelineBarrier2(commandBuffer, &shadowToReadDependency);
    shadowImageLayout_ = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    ++pendingStats_.imageBarriers;
}

void VulkanRenderer::recordGameRendering(
    VkCommandBuffer commandBuffer,
    VkImageView colorView,
    VkImageView resolveView,
    VkImage resolvedColorImage,
    const RenderFrameData& frameData)
{
    ShadowRenderLayout shadowLayout {};
    if (shadowImage_.view && shadowPipeline_) {
        shadowLayout = calculateShadowRenderLayout(frameData);
        recordShadowMapRendering(commandBuffer, frameData, shadowLayout);
    }

    const bool hasBlurredTiles = frameData.viewMode == RenderViewMode::Isometric3D &&
        std::ranges::any_of(frameData.tiles, [](const RenderFrameData::Tile& tile) {
            return tile.blurBehind;
        });

    if (sceneColorImage_.image && sceneColorImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED) {
        VkImageMemoryBarrier2 sceneColorToRead {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sceneColorImage_.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &sceneColorToRead,
        };
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
        sceneColorImageLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ++pendingStats_.imageBarriers;
    }

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

    copyResolvedSceneColor(commandBuffer, resolvedColorImage);
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
        .imageView = depthImage_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = loadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = depthClearValue,
    };

    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = swapchainExtent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = depthImage_.view ? &depthAttachment : nullptr,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++pendingStats_.renderPasses;

    VkViewport viewport {
        .x = 0.0f,
        .y = static_cast<float>(swapchainExtent_.height),
        .width = static_cast<float>(swapchainExtent_.width),
        .height = -static_cast<float>(swapchainExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor {
        .offset = { 0, 0 },
        .extent = swapchainExtent_,
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    ++pendingStats_.pipelineBinds;
    if (descriptorSet_) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    }
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, wireframeEnabled_ ? wireframeLineWidth_ : 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, depthImage_.view ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(commandBuffer, depthImage_.view && writeDepth ? VK_TRUE : VK_FALSE);
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

void VulkanRenderer::copyResolvedSceneColor(VkCommandBuffer commandBuffer, VkImage resolvedColorImage)
{
    VkImageMemoryBarrier2 colorToTransfer {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = resolvedColorImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkImageMemoryBarrier2 sceneColorToTransfer {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = sceneColorImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = sceneColorImageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = sceneColorImageLayout_,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sceneColorImage_.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    std::array<VkImageMemoryBarrier2, 2> toTransferBarriers { colorToTransfer, sceneColorToTransfer };
    VkDependencyInfo toTransferDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(toTransferBarriers.size()),
        .pImageMemoryBarriers = toTransferBarriers.data(),
    };
    vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);
    ++pendingStats_.imageBarriers;

    VkImageCopy copyRegion {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .extent = {
            .width = swapchainExtent_.width,
            .height = swapchainExtent_.height,
            .depth = 1,
        },
    };
    vkCmdCopyImage(
        commandBuffer,
        resolvedColorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        sceneColorImage_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    VkImageMemoryBarrier2 colorToAttachment {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = resolvedColorImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    VkImageMemoryBarrier2 sceneColorToRead {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = sceneColorImage_.image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    std::array<VkImageMemoryBarrier2, 2> fromTransferBarriers { colorToAttachment, sceneColorToRead };
    VkDependencyInfo fromTransferDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(fromTransferBarriers.size()),
        .pImageMemoryBarriers = fromTransferBarriers.data(),
    };
    vkCmdPipelineBarrier2(commandBuffer, &fromTransferDependency);
    sceneColorImageLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ++pendingStats_.imageBarriers;
}

void VulkanRenderer::recordUiRendering(VkCommandBuffer commandBuffer, VkImageView colorView, const UiDrawData& uiDrawData)
{
    if (uiDrawData.commands.empty() || uiDrawData.viewportSize.x <= 0.0f || uiDrawData.viewportSize.y <= 0.0f) {
        return;
    }

    VkRenderingAttachmentInfo colorAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = swapchainExtent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++pendingStats_.renderPasses;

    VkViewport viewport {
        .x = 0.0f,
        .y = static_cast<float>(swapchainExtent_.height),
        .width = static_cast<float>(swapchainExtent_.width),
        .height = -static_cast<float>(swapchainExtent_.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor {
        .offset = { 0, 0 },
        .extent = swapchainExtent_,
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    ++pendingStats_.pipelineBinds;
    if (descriptorSet_) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
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

    vkCmdEndRendering(commandBuffer);
}

void VulkanRenderer::recordDebugUiRendering(VkCommandBuffer commandBuffer, VkImageView colorView) const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    VkRenderingAttachmentInfo colorAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };

    VkRenderingInfo renderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = swapchainExtent_ },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ++pendingStats_.renderPasses;
    renderDebugUi(commandBuffer);
    vkCmdEndRendering(commandBuffer);
#else
    (void)commandBuffer;
    (void)colorView;
#endif
}

VulkanRenderer::TileRenderLayout VulkanRenderer::calculateTileRenderLayout(const RenderFrameData& frameData) const
{
    const BoardPixelLayout pixelLayout = calculateBoardPixelLayout(
        { static_cast<float>(swapchainExtent_.width), static_cast<float>(swapchainExtent_.height) },
        frameData.levelWidth,
        frameData.levelHeight);
    const Vec2 tileSize = pixelSizeToClipSpace(pixelLayout.tileSize);
    const Vec2 boardBottomLeft {
        -1.0f + 2.0f * pixelLayout.bottomLeft.x / static_cast<float>(swapchainExtent_.width),
        1.0f - 2.0f * pixelLayout.bottomLeft.y / static_cast<float>(swapchainExtent_.height),
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
    std::vector<IsoFace> faces;
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
        if (tile.model != RenderModel::Cube) {
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
            tile.model == RenderModel::Cube) {
            continue;
        }
        if (!modelPipelineBound) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline_);
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

    const float tileWidthPixels = std::max(layout.tileSize.x * static_cast<float>(swapchainExtent_.width) * 0.5f, 0.001f);
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
        pipelineLayout_,
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
        pipelineLayout_,
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
    const GpuMesh& mesh = meshForModel(tile.model);
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
            0.0f,
            0.0f,
            tile.isEditorPreview ? -config::iceBlurRadiusPixels : config::iceBlurRadiusPixels,
        },
        .textureOptions = {
            tile.model == RenderModel::Rogue ? 1.0f : 0.0f,
            tile.model == RenderModel::Rogue
                ? static_cast<float>(tile.modelRotationQuarterTurns % 4)
                : 0.0f,
            std::max(lighting.specularStrength, 0.0f),
            std::max(lighting.specularPower, 1.0f),
        },
    };

    const VkBuffer vertexBuffer = mesh.vertexBuffer.buffer;
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout_,
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
    const GpuMesh& mesh = meshForModel(tile.model);
    const ModelTransformPoints transform = modelTransformPoints(tile);
    const TilePushConstants pushConstants {
        .shadowVertices = affineTransformColumns(
            projectShadowPoint(layout, transform.origin),
            projectShadowPoint(layout, transform.xPoint),
            projectShadowPoint(layout, transform.yPoint),
            projectShadowPoint(layout, transform.zPoint)),
    };

    const VkBuffer vertexBuffer = mesh.vertexBuffer.buffer;
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout_,
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

    drawFace(commandBuffer, {
        Vec3 { left, top, 0.0f },
        Vec3 { right, top, 0.0f },
        Vec3 { right, bottom, 0.0f },
        Vec3 { left, bottom, 0.0f },
    }, {}, command.color, {}, lighting);
}

Vec3 VulkanRenderer::projectIsoPoint(const IsoRenderLayout& layout, Vec3 point) const
{
    const Vec3 relative = subtract(point, layout.cameraPosition);
    const float cameraX = dot(relative, layout.cameraRight);
    const float cameraY = dot(relative, layout.cameraUp);
    const float cameraZ = std::max(dot(relative, layout.cameraForward), 0.001f);
    const float aspect = static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height);
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
    // Pixel sizes are measured against the swapchain, but shader positions are
    // in clip space where the visible width and height each span -1 to +1.
    // Multiplying by 2 converts a screen fraction into that two-unit range.
    return {
        2.0f * pixelSize / static_cast<float>(swapchainExtent_.width),
        2.0f * pixelSize / static_cast<float>(swapchainExtent_.height),
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

    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT graphicsPipelineLibrary {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT,
    };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &graphicsPipelineLibrary,
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
        extendedDynamicState.extendedDynamicState &&
        graphicsPipelineLibrary.graphicsPipelineLibrary;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
    const auto preferred = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });

    return preferred != formats.end() ? *preferred : formats.front();
}

VkPresentModeKHR VulkanRenderer::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const
{
    return std::ranges::find(modes, VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()
        ? VK_PRESENT_MODE_MAILBOX_KHR
        : VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);

    VkExtent2D extent {
        .width = static_cast<uint32_t>(std::max(width, 1)),
        .height = static_cast<uint32_t>(std::max(height, 1)),
    };

    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::filesystem::path& path) const
{
    const std::vector<char> code = readFile(path);

    VkShaderModuleCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule), "vkCreateShaderModule failed");
    return shaderModule;
}

VkPipeline VulkanRenderer::createGraphicsPipeline(VkShaderModule vertexShader, VkShaderModule fragmentShader) const
{
    std::array<VkPipelineShaderStageCreateInfo, 2> stages {
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = activeSampleCount_,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachment {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };
    VkDynamicState dynamicStates[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainFormat_,
        .depthAttachmentFormat = depthFormat_,
    };
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout_,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateGraphicsPipelines main pipeline failed");
    return pipeline;
}

VkPipeline VulkanRenderer::createModelGraphicsPipeline(VkShaderModule vertexShader, VkShaderModule fragmentShader) const
{
    std::array<VkPipelineShaderStageCreateInfo, 2> stages {
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
        },
    };
    const VkVertexInputBindingDescription binding {
        .binding = 0,
        .stride = sizeof(MeshVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const std::array<VkVertexInputAttributeDescription, 3> attributes {
        VkVertexInputAttributeDescription {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(MeshVertex, position),
        },
        VkVertexInputAttributeDescription {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(MeshVertex, normal),
        },
        VkVertexInputAttributeDescription {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(MeshVertex, uv),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = activeSampleCount_,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachment {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };
    VkDynamicState dynamicStates[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates)),
        .pDynamicStates = dynamicStates,
    };
    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainFormat_,
        .depthAttachmentFormat = depthFormat_,
    };
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout_,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCheck(
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
        "vkCreateGraphicsPipelines model pipeline failed");
    return pipeline;
}

std::array<VkPipeline, 2> VulkanRenderer::createGraphicsPipelineLibraries(VkShaderModule vertexShader, VkShaderModule fragmentShader) const
{
    VkPipelineShaderStageCreateInfo vertexStage {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertexShader,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo fragmentStage {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragmentShader,
        .pName = "main",
    };

    VkPipelineVertexInputStateCreateInfo vertexInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = activeSampleCount_,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    VkDynamicState preRasterDynamicStates[] {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_LINE_WIDTH,
    };

    VkPipelineDynamicStateCreateInfo preRasterDynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(preRasterDynamicStates)),
        .pDynamicStates = preRasterDynamicStates,
    };

    VkDynamicState fragmentDynamicStates[] {
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };

    VkPipelineDynamicStateCreateInfo fragmentDynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(std::size(fragmentDynamicStates)),
        .pDynamicStates = fragmentDynamicStates,
    };

    VkPipelineRenderingCreateInfo rendering {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainFormat_,
        .depthAttachmentFormat = depthFormat_,
    };

    VkGraphicsPipelineLibraryCreateInfoEXT vertexPreRasterLibraryInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
    };

    VkGraphicsPipelineCreateInfo vertexPreRasterPipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &vertexPreRasterLibraryInfo,
        .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR |
            VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
        .stageCount = 1,
        .pStages = &vertexStage,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pDynamicState = &preRasterDynamicState,
        .layout = pipelineLayout_,
    };

    VkGraphicsPipelineLibraryCreateInfoEXT fragmentLibraryInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .pNext = &rendering,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
            VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT,
    };

    VkGraphicsPipelineCreateInfo fragmentPipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &fragmentLibraryInfo,
        .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR |
            VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
        .stageCount = 1,
        .pStages = &fragmentStage,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &fragmentDynamicState,
        .layout = pipelineLayout_,
    };

    std::array<VkGraphicsPipelineCreateInfo, 2> createInfos {
        vertexPreRasterPipelineInfo,
        fragmentPipelineInfo,
    };
    std::array<VkPipeline, 2> libraries {};
    vkCheck(
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, static_cast<uint32_t>(createInfos.size()), createInfos.data(), nullptr, libraries.data()),
        "vkCreateGraphicsPipelines libraries failed");
    return libraries;
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
    const VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts;
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

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("No suitable Vulkan memory type found");
}

VulkanRenderer::OwnedBuffer VulkanRenderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) const
{
    OwnedBuffer result;
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &result.buffer), "vkCreateBuffer failed");

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties),
    };
    const VkResult allocationResult = vkAllocateMemory(device_, &allocationInfo, nullptr, &result.memory);
    if (allocationResult != VK_SUCCESS) {
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(allocationResult, "vkAllocateMemory buffer failed");
    }
    const VkResult bindResult = vkBindBufferMemory(device_, result.buffer, result.memory, 0);
    if (bindResult != VK_SUCCESS) {
        vkFreeMemory(device_, result.memory, nullptr);
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(bindResult, "vkBindBufferMemory failed");
    }
    return result;
}

VkImageView VulkanRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask) const
{
    VkImageViewCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkImageView imageView = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(device_, &createInfo, nullptr, &imageView), "vkCreateImageView failed");
    return imageView;
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
