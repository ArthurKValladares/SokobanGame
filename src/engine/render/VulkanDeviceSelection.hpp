#pragma once

#include <vulkan/vulkan.h>

namespace sokoban {

// Scores devices only after the renderer has established that they support
// its required queues, extensions, surface formats, and Vulkan features.
[[nodiscard]] int vulkanDevicePreferenceScore(
    const VkPhysicalDeviceProperties& properties);
[[nodiscard]] const char* vulkanDeviceTypeName(VkPhysicalDeviceType type);

// Chooses values that are present in the surface capability masks for
// VkSwapchainCreateInfoKHR. Throws when the surface advertises no legal mode.
[[nodiscard]] VkSurfaceTransformFlagBitsKHR chooseSurfaceTransform(
    const VkSurfaceCapabilitiesKHR& capabilities);
[[nodiscard]] VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
    const VkSurfaceCapabilitiesKHR& capabilities);

} // namespace sokoban
