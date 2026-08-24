#include "engine/render/VulkanDebugUtils.hpp"

#include "engine/Log.hpp"

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban::vulkanDebug {
namespace {

std::atomic_bool objectNamingEnabled { false };

VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*)
{
    try {
        const char* const message = callbackData && callbackData->pMessage
            ? callbackData->pMessage
            : "validation layer did not provide a message";
        const char* const id = callbackData && callbackData->pMessageIdName
            ? callbackData->pMessageIdName
            : "unknown";
        const std::string decorated =
            "Vulkan validation [" + std::string(id) + "]: " + message;
        switch (validationMessageLogLevel(severity)) {
        case log::Level::Debug:
            log::debug(log::Category::Rendering) << decorated;
            break;
        case log::Level::Info:
            log::info(log::Category::Rendering) << decorated;
            break;
        case log::Level::Warning:
            log::warning(log::Category::Rendering) << decorated;
            break;
        case log::Level::Error:
            log::error(log::Category::Rendering) << decorated;
            break;
        }
    } catch (...) {
        // Vulkan callbacks are C entry points. Logging must never let an
        // allocation or formatting failure cross this boundary.
    }
    return VK_FALSE;
}

} // namespace

bool debugUtilsExtensionAvailable()
{
    uint32_t extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &extensionCount, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::string_view(extension.extensionName) ==
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME) {
            return true;
        }
    }
    return false;
}

VkDebugUtilsMessengerCreateInfoEXT validationMessengerCreateInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validationCallback,
    };
}

VkResult createValidationMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT& createInfo,
    VkDebugUtilsMessengerEXT& messenger)
{
    const auto createMessenger =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    return createMessenger
        ? createMessenger(instance, &createInfo, nullptr, &messenger)
        : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyValidationMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger) noexcept
{
    if (!instance || !messenger) {
        return;
    }
    const auto destroyMessenger =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroyMessenger) {
        destroyMessenger(instance, messenger, nullptr);
    }
}

void initializeObjectNaming(VkInstance instance)
{
    objectNamingEnabled.store(instance != VK_NULL_HANDLE, std::memory_order_release);
}

void shutdownObjectNaming() noexcept
{
    objectNamingEnabled.store(false, std::memory_order_release);
}

void setObjectName(
    VkDevice device,
    VkObjectType objectType,
    uint64_t objectHandle,
    std::string_view name) noexcept
{
    if (!objectNamingEnabled.load(std::memory_order_acquire) ||
        !device || objectHandle == 0 || name.empty()) {
        return;
    }
    const auto setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    if (!setName) {
        return;
    }
    try {
        const std::string terminatedName(name);
        const VkDebugUtilsObjectNameInfoEXT nameInfo {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = objectType,
            .objectHandle = objectHandle,
            .pObjectName = terminatedName.c_str(),
        };
        (void)setName(device, &nameInfo);
    } catch (...) {
        // Debug labels must never make renderer resource creation fail.
    }
}

log::Level validationMessageLogLevel(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity)
{
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return log::Level::Error;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return log::Level::Warning;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return log::Level::Info;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
    default:
        return log::Level::Debug;
    }
}

} // namespace sokoban::vulkanDebug
