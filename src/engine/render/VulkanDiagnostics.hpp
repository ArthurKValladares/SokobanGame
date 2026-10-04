#pragma once

#include <vulkan/vulkan.h>

#include <optional>
#include <string_view>
#include <cstdint>

struct SDL_Window;

namespace sokoban {

enum class VulkanFailure : uint8_t {
    DeviceLost,
    SurfaceLost,
    UnsupportedHardware,
};

[[nodiscard]] std::optional<VulkanFailure> vulkanFailureForResult(
    VkResult result);
[[nodiscard]] std::string_view vulkanFailureTitle(VulkanFailure failure);
[[nodiscard]] std::string_view vulkanFailureMessage(VulkanFailure failure);

// Deliberately independent of the renderer UI: neither a lost device nor a
// lost surface can be relied on to present another in-game frame.
void showVulkanFailureDialog(SDL_Window* window, VulkanFailure failure) noexcept;

} // namespace sokoban
