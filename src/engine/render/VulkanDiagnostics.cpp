#include "engine/render/VulkanDiagnostics.hpp"

#include "engine/Log.hpp"

#include <SDL3/SDL.h>

namespace sokoban {

std::optional<VulkanFailure> vulkanFailureForResult(VkResult result)
{
    switch (result) {
    case VK_ERROR_DEVICE_LOST:
        return VulkanFailure::DeviceLost;
    case VK_ERROR_SURFACE_LOST_KHR:
        return VulkanFailure::SurfaceLost;
    default:
        return std::nullopt;
    }
}

std::string_view vulkanFailureTitle(VulkanFailure failure)
{
    switch (failure) {
    case VulkanFailure::DeviceLost:
        return "Graphics device lost";
    case VulkanFailure::SurfaceLost:
        return "Graphics display connection lost";
    case VulkanFailure::UnsupportedHardware:
        return "Unsupported graphics hardware";
    }
    return "Graphics error";
}

std::string_view vulkanFailureMessage(VulkanFailure failure)
{
    switch (failure) {
    case VulkanFailure::DeviceLost:
        return "The graphics device stopped responding. Sokoban 3D will close "
               "safely; please restart the game.";
    case VulkanFailure::SurfaceLost:
        return "The display connection used by Vulkan was lost. Sokoban 3D will "
               "close safely; please restart the game.";
    case VulkanFailure::UnsupportedHardware:
        return "Sokoban 3D requires a Vulkan 1.3 GPU with dynamic rendering, "
               "synchronization2, cube-map arrays, extended dynamic state, and "
               "the required descriptor capacity. Update the graphics driver or "
               "use supported graphics hardware.";
    }
    return "An unrecoverable graphics error occurred.";
}

void showVulkanFailureDialog(SDL_Window* window, VulkanFailure failure) noexcept
{
    const std::string_view title = vulkanFailureTitle(failure);
    const std::string_view message = vulkanFailureMessage(failure);
    if (!SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            title.data(),
            message.data(),
            window)) {
        log::warning(log::Category::Rendering)
            << "Could not show graphics failure dialog: " << SDL_GetError();
    }
}

} // namespace sokoban
