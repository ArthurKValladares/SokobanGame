#include "engine/render/VulkanDeviceContext.hpp"

#include "engine/render/RendererConfig.hpp"
#include "engine/Log.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanDiagnostics.hpp"
#include "engine/render/VulkanDeviceSelection.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

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
// Deliberately fatal rather than defaulting to 0. This flag decides whether
// Application and DebugUi declare some of their members, so a translation unit
// that quietly assumed a value would disagree with the rest of the program
// about those class layouts - and link anyway. CMake defines it PUBLIC on
// sokoban_core, so anything linking a Sokoban library already has it.
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

namespace sokoban {
namespace {

constexpr std::array<const char*, 1> requiredDeviceExtensions {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

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

VulkanDeviceContext::VulkanDeviceContext(
    SDL_Window* window,
    std::size_t requiredTextureDescriptors)
    : window_(window)
{
    if (requiredTextureDescriptors > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(
            "Texture descriptor requirement is outside the supported range");
    }
    requiredTextureDescriptors_ = std::max(
        static_cast<uint32_t>(requiredTextureDescriptors), 1U);
    try {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createDevice();
        memoryAllocator_.create(instance_, physicalDevice_, device_);
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

VulkanMemoryAllocator& VulkanDeviceContext::memoryAllocator()
{
    return memoryAllocator_;
}

const VulkanMemoryAllocator& VulkanDeviceContext::memoryAllocator() const
{
    return memoryAllocator_;
}

bool VulkanDeviceContext::wireframeSupported() const
{
    return featureTier_.wireframeSupported;
}

bool VulkanDeviceContext::wideLinesSupported() const
{
    return wideLinesSupported_;
}

float VulkanDeviceContext::maxSamplerAnisotropy() const
{
    // 1.0 is the "disabled" value every sampler accepts, so callers can pass
    // this straight through without branching on support.
    return featureTier_.anisotropicFilteringSupported
        ? std::max(physicalDeviceProperties_.limits.maxSamplerAnisotropy, 1.0f)
        : 1.0f;
}

uint32_t VulkanDeviceContext::textureDescriptorCapacity() const
{
    return textureDescriptorCapacity_;
}

bool VulkanDeviceContext::graphicsTimestampsSupported() const
{
    return graphicsTimestampValidBits_ != 0 &&
        physicalDeviceProperties_.limits.timestampComputeAndGraphics == VK_TRUE;
}

float VulkanDeviceContext::timestampPeriodNanoseconds() const
{
    return physicalDeviceProperties_.limits.timestampPeriod;
}

uint32_t VulkanDeviceContext::graphicsTimestampValidBits() const
{
    return graphicsTimestampValidBits_;
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
        // Discarded, not checked, and that is forced: ~VulkanRenderer calls
        // this, so a throw here would leave a destructor by exception and
        // terminate. A lost device has already made every later call
        // meaningless anyway - the failure is reported by whatever submits
        // next, which can throw. The explicit cast says the result was
        // considered rather than forgotten, which is the convention the two
        // waits in VulkanModelResources' catch blocks already follow.
        (void)vkDeviceWaitIdle(device_);
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
        .apiVersion = VK_API_VERSION_1_3,
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
        log::warning(log::Category::Rendering)
            << "VK_LAYER_KHRONOS_validation is unavailable; Vulkan validation is disabled";
        layers.clear();
    }
    const bool enableDebugUtils =
        !layers.empty() && vulkanDebug::debugUtilsExtensionAvailable();
    if (!layers.empty() && !enableDebugUtils) {
        log::warning(log::Category::Rendering)
            << "VK_EXT_debug_utils is unavailable; Vulkan validation messages will not be captured";
    }
    if (enableDebugUtils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    const VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo =
        vulkanDebug::validationMessengerCreateInfo();

    const VkInstanceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = enableDebugUtils ? &debugCreateInfo : nullptr,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    vkCheck(
        vkCreateInstance(&createInfo, nullptr, &instance_),
        "vkCreateInstance failed");
    if (enableDebugUtils) {
        createValidationMessenger();
    }
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

void VulkanDeviceContext::createValidationMessenger()
{
    const VkDebugUtilsMessengerCreateInfoEXT createInfo =
        vulkanDebug::validationMessengerCreateInfo();
    vkCheck(
        vulkanDebug::createValidationMessenger(
            instance_, createInfo, validationMessenger_),
        "vkCreateDebugUtilsMessengerEXT failed");
    vulkanDebug::initializeObjectNaming(instance_);
}

void VulkanDeviceContext::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkCheck(
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
        "vkEnumeratePhysicalDevices failed");
    if (deviceCount == 0) {
        showVulkanFailureDialog(window_, VulkanFailure::UnsupportedHardware);
        throw std::runtime_error(std::string(vulkanFailureMessage(
            VulkanFailure::UnsupportedHardware)));
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
        showVulkanFailureDialog(window_, VulkanFailure::UnsupportedHardware);
        throw std::runtime_error(std::string(vulkanFailureMessage(
            VulkanFailure::UnsupportedHardware)));
    }

    queueFamilies_ = findQueueFamilies(physicalDevice_);
    vkGetPhysicalDeviceProperties(
        physicalDevice_, &physicalDeviceProperties_);
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice_, &queueFamilyCount, queueFamilyProperties.data());
    graphicsTimestampValidBits_ =
        queueFamilyProperties[queueFamilies_.graphics].timestampValidBits;
    const VulkanDeviceFeatureSupport support =
        queryFeatureSupport(physicalDevice_);
    const VulkanTextureHeapCapacity textureHeap =
        chooseVulkanTextureHeapCapacity(
            support,
            requiredTextureDescriptors_,
            config::editorTextureDescriptorReserve,
            config::importedTextureDescriptorReserve,
            config::textureDescriptorCapacityCeiling,
            sceneSingleImageBindings);
    textureDescriptorCapacity_ = textureHeap.capacity;
    featureTier_ = chooseVulkanFeatureTier(
        support,
        sizeof(GpuDrawInstance),
        textureDescriptorCapacity_ + sceneSingleImageBindings,
        textureDescriptorCapacity_);
    log::info(log::Category::Rendering) << "Vulkan GPU: "
        << physicalDeviceProperties_.deviceName << " ("
        << vulkanDeviceTypeName(physicalDeviceProperties_.deviceType)
        << ")" << (featureTier_.wireframeSupported
            ? " with debug wireframe support"
            : " without debug wireframe support")
        << "; texture descriptor capacity " << textureDescriptorCapacity_;
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
    VkPhysicalDeviceVulkan12Features vulkan12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan13,
    };
    vulkan12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vulkan12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vulkan12.runtimeDescriptorArray = VK_TRUE;

    wideLinesSupported_ = featureTier_.wideLinesSupported;
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
        .imageCubeArray = VK_TRUE,
        .fillModeNonSolid = featureTier_.wireframeSupported ? VK_TRUE : VK_FALSE,
        .wideLines = wideLinesSupported_ ? VK_TRUE : VK_FALSE,
        .samplerAnisotropy =
            featureTier_.anisotropicFilteringSupported ? VK_TRUE : VK_FALSE,
    };
    const VkDeviceCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan12,
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
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_DEVICE, device_, "Sokoban logical device");

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
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_COMMAND_POOL, commandPool_,
        "Sokoban graphics command pool");
}

void VulkanDeviceContext::destroy() noexcept
{
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    memoryAllocator_.destroy();
    if (device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vulkanDebug::shutdownObjectNaming();
        vulkanDebug::destroyValidationMessenger(
            instance_, validationMessenger_);
        validationMessenger_ = VK_NULL_HANDLE;
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
    const VulkanDeviceFeatureSupport support = queryFeatureSupport(device);
    const VulkanTextureHeapCapacity textureHeap =
        chooseVulkanTextureHeapCapacity(
            support,
            requiredTextureDescriptors_,
            config::editorTextureDescriptorReserve,
            config::importedTextureDescriptorReserve,
            config::textureDescriptorCapacityCeiling,
            sceneSingleImageBindings);
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (!textureHeap.supported) {
        log::warning(log::Category::Rendering)
            << "Rejecting Vulkan GPU " << properties.deviceName << ": "
            << vulkanTextureHeapCapacityFailureMessage(textureHeap);
        return false;
    }
    const VulkanFeatureTier tier = chooseVulkanFeatureTier(
        support,
        sizeof(GpuDrawInstance),
        textureHeap.capacity + sceneSingleImageBindings,
        textureHeap.capacity);
    if (!tier.releaseCompatible) {
        log::warning(log::Category::Rendering)
            << "Rejecting Vulkan GPU " << properties.deviceName << ": "
            << vulkanFeatureTierRejectionMessage(tier.rejection);
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

    return true;
}

VulkanDeviceFeatureSupport VulkanDeviceContext::queryFeatureSupport(
    VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(device, &properties);
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
    };
    VkPhysicalDeviceVulkan13Features vulkan13 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &extendedDynamicState,
    };
    VkPhysicalDeviceVulkan12Features vulkan12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan13,
    };
    VkPhysicalDeviceFeatures2 features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan12,
    };
    vkGetPhysicalDeviceFeatures2(device, &features);

    return {
        .apiVersion = properties.apiVersion,
        .maxPushConstantsSize = properties.limits.maxPushConstantsSize,
        .maxPerStageDescriptorSampledImages =
            properties.limits.maxPerStageDescriptorSampledImages,
        .maxDescriptorSetSampledImages =
            properties.limits.maxDescriptorSetSampledImages,
        .dynamicRendering = vulkan13.dynamicRendering == VK_TRUE,
        .synchronization2 = vulkan13.synchronization2 == VK_TRUE,
        .imageCubeArray = features.features.imageCubeArray == VK_TRUE,
        .extendedDynamicState =
            extendedDynamicState.extendedDynamicState == VK_TRUE,
        .runtimeDescriptorArray = vulkan12.runtimeDescriptorArray == VK_TRUE,
        .descriptorBindingPartiallyBound =
            vulkan12.descriptorBindingPartiallyBound == VK_TRUE,
        .descriptorBindingVariableDescriptorCount =
            vulkan12.descriptorBindingVariableDescriptorCount == VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing =
            vulkan12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE,
        .fillModeNonSolid = features.features.fillModeNonSolid == VK_TRUE,
        .wideLines = features.features.wideLines == VK_TRUE,
        .samplerAnisotropy = features.features.samplerAnisotropy == VK_TRUE,
    };
}

} // namespace sokoban
