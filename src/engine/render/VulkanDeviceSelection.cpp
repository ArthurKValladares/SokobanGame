#include "engine/render/VulkanDeviceSelection.hpp"

#include <algorithm>

namespace sokoban {

int vulkanDevicePreferenceScore(const VkPhysicalDeviceProperties& properties)
{
    int score = 0;
    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score = 4000;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score = 3000;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score = 2000;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score = 1000;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default:
        break;
    }

    // Break ties between devices of the same class without allowing a large
    // integrated-GPU limit to outrank a discrete GPU.
    score += static_cast<int>(std::min(
        properties.limits.maxImageDimension2D / 1024U,
        100U));
    return score;
}

const char* vulkanDeviceTypeName(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default: return "other";
    }
}

} // namespace sokoban
