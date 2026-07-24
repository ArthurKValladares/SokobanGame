#include "engine/render/VulkanDeviceContext.hpp"

#include "engine/Config.hpp"
#include "engine/Log.hpp"
#include "engine/render/VulkanDeviceSelection.hpp"
#include "engine/render/VulkanRenderConstants.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

namespace sokoban {
namespace {

constexpr std::array<const char*, 2> requiredDeviceExtensions {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
};

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(message) + " (VkResult " +
            std::to_string(result) + ")");
    }
}

std::vector<const char*> validationLayers()
{
#if SOKOBAN_ENABLE_VALIDATION
    return { "VK_LAYER_KHRONOS_validation" };
#else
    return {};
#endif
}

bool supportsValidationLayer()
{
    uint32_t layerCount = 0;
    vkCheck(
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr),
        "vkEnumerateInstanceLayerProperties failed");

    std::vector<VkLayerProperties> layers(layerCount);
    vkCheck(
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data()),
        "vkEnumerateInstanceLayerProperties failed");

    return std::ranges::any_of(
        layers,
        [](const VkLayerProperties& layer) {
            return std::string_view(layer.layerName) ==
                "VK_LAYER_KHRONOS_validation";
        });
}

} // namespace

VulkanDeviceContext::VulkanDeviceContext(SDL_Window* window)
    : window_(window)
{
    try {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createDevice();
        createCommandPool();
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanDeviceContext::~VulkanDeviceContext()
{
    waitIdle();
    destroy();
}

VkInstance VulkanDeviceContext::instance() const
{
    return instance_;
}

VkSurfaceKHR VulkanDeviceContext::surface() const
{
    return surface_;
}

VkPhysicalDevice VulkanDeviceContext::physicalDevice() const
{
    return physicalDevice_;
}

const VkPhysicalDeviceProperties&
VulkanDeviceContext::physicalDeviceProperties() const
{
    return physicalDeviceProperties_;
}

VkDevice VulkanDeviceContext::device() const
{
    return device_;
}

const VulkanQueueFamilyIndices& VulkanDeviceContext::queueFamilies() const
{
    return queueFamilies_;
}

VkQueue VulkanDeviceContext::graphicsQueue() const
{
    return graphicsQueue_;
}

VkQueue VulkanDeviceContext::presentQueue() const
{
    return presentQueue_;
}

VkCommandPool VulkanDeviceContext::commandPool() const
{
    return commandPool_;
}

bool VulkanDeviceContext::wideLinesSupported() const
{
    return wideLinesSupported_;
}

std::array<float, 2>
VulkanDeviceContext::wireframeLineWidthRange() const
{
    return wireframeLineWidthRange_;
}

VkSampleCountFlagBits VulkanDeviceContext::supportedSampleCount(
    VkSampleCountFlagBits requested) const
{
    if (requested == VK_SAMPLE_COUNT_1_BIT) {
        return requested;
    }

    const VkSampleCountFlags supported =
        physicalDeviceProperties_.limits.framebufferColorSampleCounts &
        physicalDeviceProperties_.limits.framebufferDepthSampleCounts;
    if (supported & requested) {
        return requested;
    }
    if (requested >= VK_SAMPLE_COUNT_8_BIT &&
        (supported & VK_SAMPLE_COUNT_4_BIT)) {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    if (requested >= VK_SAMPLE_COUNT_4_BIT &&
        (supported & VK_SAMPLE_COUNT_2_BIT)) {
        return VK_SAMPLE_COUNT_2_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

void VulkanDeviceContext::waitIdle() const
{
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
}

void VulkanDeviceContext::createInstance()
{
    const VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Sokoban 3D",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "Sokoban Engine",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_4,
    };

    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions =
        SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions) {
        throw std::runtime_error(
            std::string("SDL_Vulkan_GetInstanceExtensions failed: ") +
            SDL_GetError());
    }
    std::vector<const char*> extensions(
        sdlExtensions, sdlExtensions + sdlExtensionCount);

    std::vector<const char*> layers = validationLayers();
    if (!layers.empty() && !supportsValidationLayer()) {
        layers.clear();
    }

    const VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    vkCheck(
        vkCreateInstance(&createInfo, nullptr, &instance_),
        "vkCreateInstance failed");
}

void VulkanDeviceContext::createSurface()
{
    if (!SDL_Vulkan_CreateSurface(
            window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(
            std::string("SDL_Vulkan_CreateSurface failed: ") +
            SDL_GetError());
    }
}

void VulkanDeviceContext::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkCheck(
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
        "vkEnumeratePhysicalDevices failed");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan-capable GPU found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkCheck(
        vkEnumeratePhysicalDevices(
            instance_, &deviceCount, devices.data()),
        "vkEnumeratePhysicalDevices failed");

    int bestScore = std::numeric_limits<int>::min();
    for (VkPhysicalDevice device : devices) {
        if (!isDeviceSuitable(device)) {
            continue;
        }
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(device, &properties);
        const int score = vulkanDevicePreferenceScore(properties);
        if (!physicalDevice_ || score > bestScore) {
            physicalDevice_ = device;
            bestScore = score;
        }
    }

    if (!physicalDevice_) {
        throw std::runtime_error(
            "No GPU supports the required Vulkan 1.4 feature set");
    }

    queueFamilies_ = findQueueFamilies(physicalDevice_);
    vkGetPhysicalDeviceProperties(
        physicalDevice_, &physicalDeviceProperties_);
    log::info(log::Category::Rendering) << "Vulkan GPU: "
        << physicalDeviceProperties_.deviceName << " ("
        << vulkanDeviceTypeName(physicalDeviceProperties_.deviceType)
        << ")";
}

void VulkanDeviceContext::createDevice()
{
    const std::set<uint32_t> uniqueQueueFamilies {
        queueFamilies_.graphics,
        queueFamilies_.present,
    };
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
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
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
    const float minLineWidth = std::max(
        physicalDeviceProperties_.limits.lineWidthRange[0], 1.0f);
    const float maxPracticalLineWidth = std::max(
        minLineWidth,
        std::min(
            physicalDeviceProperties_.limits.lineWidthRange[1],
            config::maxWireframeLineWidth));
    wireframeLineWidthRange_ = wideLinesSupported_
        ? std::array<float, 2> {
              minLineWidth, maxPracticalLineWidth }
        : std::array<float, 2> { 1.0f, 1.0f };

    const VkPhysicalDeviceFeatures enabledFeatures {
        .fillModeNonSolid = VK_TRUE,
        .wideLines = wideLinesSupported_ ? VK_TRUE : VK_FALSE,
    };
    const VkDeviceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13,
        .queueCreateInfoCount =
            static_cast<uint32_t>(queueInfos.size()),
        .pQueueCreateInfos = queueInfos.data(),
        .enabledExtensionCount =
            static_cast<uint32_t>(requiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = requiredDeviceExtensions.data(),
        .pEnabledFeatures = &enabledFeatures,
    };
    vkCheck(
        vkCreateDevice(
            physicalDevice_, &createInfo, nullptr, &device_),
        "vkCreateDevice failed");

    vkGetDeviceQueue(
        device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(
        device_, queueFamilies_.present, 0, &presentQueue_);
}

void VulkanDeviceContext::createCommandPool()
{
    const VkCommandPoolCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilies_.graphics,
    };
    vkCheck(
        vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_),
        "vkCreateCommandPool failed");
}

void VulkanDeviceContext::destroy() noexcept
{
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    if (device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

VulkanQueueFamilyIndices VulkanDeviceContext::findQueueFamilies(
    VkPhysicalDevice device) const
{
    VulkanQueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkCheck(
            vkGetPhysicalDeviceSurfaceSupportKHR(
                device, i, surface_, &presentSupport),
            "vkGetPhysicalDeviceSurfaceSupportKHR failed");
        if (presentSupport) {
            indices.present = i;
        }
        if (indices.complete()) {
            break;
        }
    }
    return indices;
}

bool VulkanDeviceContext::isDeviceSuitable(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
        (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 &&
         VK_API_VERSION_MINOR(properties.apiVersion) < 4) ||
        properties.limits.maxPushConstantsSize <
            sizeof(TilePushConstants)) {
        return false;
    }

    const VulkanQueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.complete()) {
        return false;
    }

    uint32_t extensionCount = 0;
    vkCheck(
        vkEnumerateDeviceExtensionProperties(
            device, nullptr, &extensionCount, nullptr),
        "vkEnumerateDeviceExtensionProperties failed");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkCheck(
        vkEnumerateDeviceExtensionProperties(
            device, nullptr, &extensionCount, extensions.data()),
        "vkEnumerateDeviceExtensionProperties failed");
    std::set<std::string> missing(
        requiredDeviceExtensions.begin(),
        requiredDeviceExtensions.end());
    for (const VkExtensionProperties& extension : extensions) {
        missing.erase(extension.extensionName);
    }
    if (!missing.empty()) {
        return false;
    }

    uint32_t formatCount = 0;
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, surface_, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface_, &presentModeCount, nullptr);
    if (formatCount == 0 || presentModeCount == 0) {
        return false;
    }

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
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

} // namespace sokoban
