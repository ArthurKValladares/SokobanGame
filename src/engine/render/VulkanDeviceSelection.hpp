#pragma once

#include <vulkan/vulkan.h>

namespace sokoban {

// Scores devices only after the renderer has established that they support
// its required queues, extensions, surface formats, and Vulkan features.
[[nodiscard]] int vulkanDevicePreferenceScore(
    const VkPhysicalDeviceProperties& properties);
[[nodiscard]] const char* vulkanDeviceTypeName(VkPhysicalDeviceType type);

} // namespace sokoban
