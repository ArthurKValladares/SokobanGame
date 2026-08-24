#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace sokoban {

// The release renderer requires Vulkan 1.3's dynamic-rendering and
// synchronization model, cube-map arrays for point shadows, and the dynamic
// states used by its ordinary draw paths. Wireframe and wide lines are
// developer conveniences: a release-capable device need not support either.
struct VulkanDeviceFeatureSupport {
    uint32_t apiVersion = 0;
    uint32_t maxPushConstantsSize = 0;
    uint32_t maxPerStageDescriptorSampledImages = 0;
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool imageCubeArray = false;
    bool extendedDynamicState = false;
    bool fillModeNonSolid = false;
    bool wideLines = false;
};

struct VulkanFeatureTier {
    bool releaseCompatible = false;
    bool wireframeSupported = false;
    bool wideLinesSupported = false;
};

[[nodiscard]] VulkanFeatureTier chooseVulkanFeatureTier(
    const VulkanDeviceFeatureSupport& support,
    uint32_t requiredPushConstantsSize,
    uint32_t requiredSampledImages);

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
