#include "engine/render/VulkanDeviceSelection.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

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

VkSurfaceTransformFlagBitsKHR chooseSurfaceTransform(
    const VkSurfaceCapabilitiesKHR& capabilities)
{
    const auto supported = capabilities.supportedTransforms;
    const VkSurfaceTransformFlagsKHR currentTransform =
        static_cast<VkSurfaceTransformFlagsKHR>(capabilities.currentTransform);
    if (currentTransform != 0 &&
        (supported & currentTransform) == currentTransform) {
        return capabilities.currentTransform;
    }

    constexpr std::array<VkSurfaceTransformFlagBitsKHR, 9> preferred {
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
        VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR,
        VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR,
        VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR,
        VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR,
        VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR,
        VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR,
        VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR,
    };
    for (const VkSurfaceTransformFlagBitsKHR transform : preferred) {
        if ((supported & transform) != 0) {
            return transform;
        }
    }
    throw std::runtime_error("surface advertises no supported pre-transform");
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
    const VkSurfaceCapabilitiesKHR& capabilities)
{
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preferred {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (const VkCompositeAlphaFlagBitsKHR alpha : preferred) {
        if ((capabilities.supportedCompositeAlpha & alpha) != 0) {
            return alpha;
        }
    }
    throw std::runtime_error("surface advertises no supported composite alpha mode");
}

} // namespace sokoban
