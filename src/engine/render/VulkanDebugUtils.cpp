#include "engine/render/VulkanDebugUtils.hpp"

#include "engine/Log.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban::vulkanDebug {
namespace {

// Validation errors are counted, not just logged, so a headless run can turn
// them into a process exit code. Relaxed ordering is enough: nothing is
// published through this counter, and the only reader runs after the frames
// it is reporting on have finished.
std::atomic<std::uint64_t> validationErrors { 0 };
std::atomic_bool validationMessengerActive { false };

std::atomic_bool objectNamingEnabled { false };
std::atomic<PFN_vkSetDebugUtilsObjectNameEXT> setObjectNameFunction { nullptr };
std::atomic<PFN_vkCmdBeginDebugUtilsLabelEXT> beginLabelFunction { nullptr };
std::atomic<PFN_vkCmdEndDebugUtilsLabelEXT> endLabelFunction { nullptr };

VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*)
{
    if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT &&
        (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0) {
        // Before the logging below, which is allowed to fail: the count is the
        // part a CI gate depends on, so it must not be lost to a formatting or
        // allocation failure inside a C callback.
        validationErrors.fetch_add(1, std::memory_order_relaxed);
    }
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
    if (!createMessenger) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    const VkResult result =
        createMessenger(instance, &createInfo, nullptr, &messenger);
    if (result == VK_SUCCESS) {
        validationMessengerActive.store(true, std::memory_order_relaxed);
    }
    return result;
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
    validationMessengerActive.store(false, std::memory_order_relaxed);
}

void initializeObjectNaming(VkInstance instance)
{
    setObjectNameFunction.store(
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT")),
        std::memory_order_release);
    beginLabelFunction.store(
        reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT")),
        std::memory_order_release);
    endLabelFunction.store(
        reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT")),
        std::memory_order_release);
    objectNamingEnabled.store(instance != VK_NULL_HANDLE, std::memory_order_release);
}

void shutdownObjectNaming() noexcept
{
    objectNamingEnabled.store(false, std::memory_order_release);
    setObjectNameFunction.store(nullptr, std::memory_order_release);
    beginLabelFunction.store(nullptr, std::memory_order_release);
    endLabelFunction.store(nullptr, std::memory_order_release);
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
    const PFN_vkSetDebugUtilsObjectNameEXT setName =
        setObjectNameFunction.load(std::memory_order_acquire);
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

void beginLabel(
    VkDevice device,
    VkCommandBuffer commandBuffer,
    std::string_view name,
    std::array<float, 4> color) noexcept
{
    if (!objectNamingEnabled.load(std::memory_order_acquire) ||
        !device || !commandBuffer || name.empty()) {
        return;
    }
    const PFN_vkCmdBeginDebugUtilsLabelEXT begin =
        beginLabelFunction.load(std::memory_order_acquire);
    if (!begin) {
        return;
    }
    try {
        const std::string terminatedName(name);
        const VkDebugUtilsLabelEXT label {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = terminatedName.c_str(),
            .color = { color[0], color[1], color[2], color[3] },
        };
        begin(commandBuffer, &label);
    } catch (...) {
        // Debug labels are optional diagnostics, never renderer work.
    }
}

void endLabel(VkDevice device, VkCommandBuffer commandBuffer) noexcept
{
    if (!objectNamingEnabled.load(std::memory_order_acquire) ||
        !device || !commandBuffer) {
        return;
    }
    const PFN_vkCmdEndDebugUtilsLabelEXT end =
        endLabelFunction.load(std::memory_order_acquire);
    if (end) {
        end(commandBuffer);
    }
}

bool validationActive() noexcept
{
    return validationMessengerActive.load(std::memory_order_relaxed);
}

std::uint64_t validationErrorCount() noexcept
{
    return validationErrors.load(std::memory_order_relaxed);
}

void resetValidationErrorCount() noexcept
{
    validationErrors.store(0, std::memory_order_relaxed);
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
