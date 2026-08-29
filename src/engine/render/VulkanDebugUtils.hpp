#pragma once

#include "engine/Log.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <array>
#include <string_view>
#include <type_traits>

namespace sokoban::vulkanDebug {

[[nodiscard]] bool debugUtilsExtensionAvailable();
[[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT validationMessengerCreateInfo();
[[nodiscard]] VkResult createValidationMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT& createInfo,
    VkDebugUtilsMessengerEXT& messenger);
void destroyValidationMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger) noexcept;

// Object naming is initialized only after VK_EXT_debug_utils is enabled for
// this engine instance. Calls are intentionally harmless in release builds or
// on systems where the optional extension is unavailable.
void initializeObjectNaming(VkInstance instance);
void shutdownObjectNaming() noexcept;
void setObjectName(
    VkDevice device,
    VkObjectType objectType,
    uint64_t objectHandle,
    std::string_view name) noexcept;

void beginLabel(
    VkDevice device,
    VkCommandBuffer commandBuffer,
    std::string_view name,
    std::array<float, 4> color = { 0.3f, 0.7f, 1.0f, 1.0f }) noexcept;
void endLabel(VkDevice device, VkCommandBuffer commandBuffer) noexcept;

// Whether the debug-utils messenger is currently installed - that is, whether
// the validation layer actually loaded.
//
// This is what keeps a validation gate from passing vacuously. A missing
// layer, a stale VK_LAYER_PATH or a Release build all produce zero errors for
// the same uninteresting reason: nothing was watching.
[[nodiscard]] bool validationActive() noexcept;

// Validation-type errors reported since process start, or since the last
// reset. General loader/driver discovery messages remain logged but do not
// make the application-validation gate fail.
//
// The messenger already logs every message; this exists so a run can *fail*
// on one. Validation output is easy to lose in a wall of stderr, and a
// headless CI job needs a signal it can gate on. Zero when the validation
// layer is unavailable, which is the case in shipping builds - callers should
// treat "no errors" as meaningful only when validation was actually enabled.
[[nodiscard]] std::uint64_t validationErrorCount() noexcept;
void resetValidationErrorCount() noexcept;

[[nodiscard]] log::Level validationMessageLogLevel(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity);

template <typename Handle>
[[nodiscard]] uint64_t objectHandleValue(Handle handle)
{
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<uint64_t>(handle);
    } else {
        return static_cast<uint64_t>(handle);
    }
}

template <typename Handle>
void setObjectName(
    VkDevice device,
    VkObjectType objectType,
    Handle handle,
    std::string_view name) noexcept
{
    if (handle) {
        setObjectName(device, objectType, objectHandleValue(handle), name);
    }
}

} // namespace sokoban::vulkanDebug
