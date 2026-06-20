#include "engine/render/VulkanRenderer.hpp"

#include "engine/BoardLayout.hpp"
#include "engine/Config.hpp"

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
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

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
    std::array<Vec4, 6> vertices;
    Vec4 color;
};

struct IsoFace {
    std::array<Vec3, 4> vertices {};
    Vec4 color {};
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

Vec4 shadeColor(Vec4 color, float multiplier)
{
    return {
        color.x * multiplier,
        color.y * multiplier,
        color.z * multiplier,
        color.w,
    };
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
    createCommandPool();
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
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }

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
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
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

void VulkanRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilies_.graphics,
    };

    vkCheck(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_), "vkCreateCommandPool failed");
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
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    vkCheck(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout failed");

    VkShaderModule vertexShader = createShaderModule(assetRoot_ / "shaders/triangle.vert.glsl.spv");
    VkShaderModule fragmentShader = createShaderModule(assetRoot_ / "shaders/triangle.frag.glsl.spv");

    pipelineLibraries_ = createGraphicsPipelineLibraries(vertexShader, fragmentShader);

    vkDestroyShaderModule(device_, fragmentShader, nullptr);
    vkDestroyShaderModule(device_, vertexShader, nullptr);

    VkPipelineLibraryCreateInfoKHR libraryInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR,
        .libraryCount = static_cast<uint32_t>(pipelineLibraries_.size()),
        .pLibraries = pipelineLibraries_.data(),
    };

    VkGraphicsPipelineCreateInfo linkedPipeline {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &libraryInfo,
        .flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT,
        .layout = pipelineLayout_,
    };

    vkCheck(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &linkedPipeline, nullptr, &pipeline_), "vkCreateGraphicsPipelines linked pipeline failed");
    ++pipelineRebuilds_;
}

void VulkanRenderer::destroyPipeline()
{
    if (pipeline_) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
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
    ++swapchainRecreations_;
}

void VulkanRenderer::cleanupSwapchain()
{
    cleanupDepthResources();
    cleanupMsaaColorResources();

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

void VulkanRenderer::recordGameRendering(
    VkCommandBuffer commandBuffer,
    VkImageView colorView,
    VkImageView resolveView,
    const RenderFrameData& frameData)
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
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = resolveView ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearValue,
    };

    VkClearValue depthClearValue {
        .depthStencil = { .depth = 1.0f, .stencil = 0 },
    };
    VkRenderingAttachmentInfo depthAttachment {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImage_.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
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
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, wireframeEnabled_ ? wireframeLineWidth_ : 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, depthImage_.view ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(commandBuffer, depthImage_.view ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);

    if (frameData.viewMode == RenderViewMode::Isometric3D) {
        drawIsoFrame(commandBuffer, calculateIsoRenderLayout(frameData), frameData);
    } else {
        const TileRenderLayout tileLayout = calculateTileRenderLayout(frameData);
        for (const auto& tile : frameData.tiles) {
            drawTile(commandBuffer, tileLayout, tile);
        }
    }

    vkCmdEndRendering(commandBuffer);
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
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetLineWidth(commandBuffer, 1.0f);
    vkCmdSetDepthTestEnable(commandBuffer, VK_FALSE);
    vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_ALWAYS);

    for (const UiDrawCommand& command : uiDrawData.commands) {
        drawUiRect(commandBuffer, command, uiDrawData.viewportSize);
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
        0.0f,
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
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
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

void VulkanRenderer::drawTile(VkCommandBuffer commandBuffer, const TileRenderLayout& layout, const RenderFrameData::Tile& tile) const
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
    }, tile.color);
}

void VulkanRenderer::drawIsoFrame(VkCommandBuffer commandBuffer, const IsoRenderLayout& layout, const RenderFrameData& frameData) const
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

    auto appendFace = [&](const std::array<Vec3, 4>& vertices, Vec3 normal, Vec4 color) {
        if (!faceVisible(vertices, normal)) {
            return;
        }

        faces.push_back({
            .vertices = {
                projectIsoPoint(layout, vertices[0]),
                projectIsoPoint(layout, vertices[1]),
                projectIsoPoint(layout, vertices[2]),
                projectIsoPoint(layout, vertices[3]),
            },
            .color = color,
            .depth = faceDepth(vertices),
        });
    };
    auto appendDoubleSidedFace = [&](const std::array<Vec3, 4>& vertices, Vec4 color) {
        faces.push_back({
            .vertices = {
                projectIsoPoint(layout, vertices[0]),
                projectIsoPoint(layout, vertices[1]),
                projectIsoPoint(layout, vertices[2]),
                projectIsoPoint(layout, vertices[3]),
            },
            .color = color,
            .depth = faceDepth(vertices),
        });
    };
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
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
            appendFace({ a, b, c, d }, { 0.0f, 0.0f, 1.0f }, tile.color);
            continue;
        }

        const float top = base + height;
        const Vec3 e { x, y, top };
        const Vec3 f { x + width, y, top };
        const Vec3 g { x + width, y + depth, top };
        const Vec3 h { x, y + depth, top };
        appendFace({ a, b, f, e }, { 0.0f, -1.0f, 0.0f }, shadeColor(tile.color, 0.72f));
        appendFace({ b, c, g, f }, { 1.0f, 0.0f, 0.0f }, shadeColor(tile.color, 0.82f));
        appendFace({ c, d, h, g }, { 0.0f, 1.0f, 0.0f }, shadeColor(tile.color, 0.62f));
        appendFace({ d, a, e, h }, { -1.0f, 0.0f, 0.0f }, shadeColor(tile.color, 0.82f));
        appendFace({ e, f, g, h }, { 0.0f, 0.0f, 1.0f }, tile.color);
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        appendDoubleSidedFace(face.vertices, face.color);
    }

    std::ranges::sort(faces, [](const IsoFace& left, const IsoFace& right) {
        return left.depth > right.depth;
    });

    for (const IsoFace& face : faces) {
        drawFace(commandBuffer, face.vertices, face.color);
    }
}

void VulkanRenderer::drawIsoTile(VkCommandBuffer commandBuffer, const IsoRenderLayout& layout, const RenderFrameData::Tile& tile) const
{
    const float x = tile.position.x;
    const float y = tile.position.y;
    const float base = tile.baseElevation;
    const float height = std::max(tile.height, 0.0f);

    if (height <= 0.0f) {
        drawFace(commandBuffer, {
            projectIsoPoint(layout, { x, y, base }),
            projectIsoPoint(layout, { x + 1.0f, y, base }),
            projectIsoPoint(layout, { x + 1.0f, y + 1.0f, base }),
            projectIsoPoint(layout, { x, y + 1.0f, base }),
        }, tile.color);
        return;
    }

    const float top = base + height;
    const Vec3 a = projectIsoPoint(layout, { x, y, base });
    const Vec3 b = projectIsoPoint(layout, { x + 1.0f, y, base });
    const Vec3 c = projectIsoPoint(layout, { x + 1.0f, y + 1.0f, base });
    const Vec3 d = projectIsoPoint(layout, { x, y + 1.0f, base });
    const Vec3 e = projectIsoPoint(layout, { x, y, top });
    const Vec3 f = projectIsoPoint(layout, { x + 1.0f, y, top });
    const Vec3 g = projectIsoPoint(layout, { x + 1.0f, y + 1.0f, top });
    const Vec3 h = projectIsoPoint(layout, { x, y + 1.0f, top });

    drawFace(commandBuffer, { d, c, g, h }, shadeColor(tile.color, 0.62f));
    drawFace(commandBuffer, { b, c, g, f }, shadeColor(tile.color, 0.78f));
    drawFace(commandBuffer, { e, f, g, h }, tile.color);
    (void)a;
}

void VulkanRenderer::drawFace(VkCommandBuffer commandBuffer, const std::array<Vec3, 4>& vertices, Vec4 color) const
{
    ++pendingStats_.visibleFaces;
    ++pendingStats_.drawCalls;
    pendingStats_.vertices += 6;
    pendingStats_.triangles += 2;

    const TilePushConstants pushConstants {
        .vertices = {
            Vec4 { vertices[0].x, vertices[0].y, vertices[0].z, 1.0f },
            Vec4 { vertices[1].x, vertices[1].y, vertices[1].z, 1.0f },
            Vec4 { vertices[2].x, vertices[2].y, vertices[2].z, 1.0f },
            Vec4 { vertices[0].x, vertices[0].y, vertices[0].z, 1.0f },
            Vec4 { vertices[2].x, vertices[2].y, vertices[2].z, 1.0f },
            Vec4 { vertices[3].x, vertices[3].y, vertices[3].z, 1.0f },
        },
        .color = color,
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

void VulkanRenderer::drawUiRect(VkCommandBuffer commandBuffer, const UiDrawCommand& command, Vec2 viewportSize) const
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
    }, command.color);
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
