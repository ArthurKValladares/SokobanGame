#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace sokoban {

// The release renderer requires Vulkan 1.3's dynamic-rendering and
// synchronization model, cube-map arrays for point shadows, and the dynamic
// states used by its ordinary draw paths. Wireframe and wide lines are
// developer conveniences: a release-capable device need not support either.
struct VulkanDeviceFeatureSupport {
    uint32_t apiVersion = 0;
    uint32_t maxPushConstantsSize = 0;
    uint32_t maxPerStageDescriptorSampledImages = 0;
    uint32_t maxDescriptorSetSampledImages = 0;
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool imageCubeArray = false;
    bool extendedDynamicState = false;
    bool runtimeDescriptorArray = false;
    bool descriptorBindingPartiallyBound = false;
    bool descriptorBindingVariableDescriptorCount = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    bool fillModeNonSolid = false;
    bool wideLines = false;
    bool samplerAnisotropy = false;
};

enum class VulkanFeatureTierRejection {
    None,
    Vulkan13,
    PushConstantCapacity,
    PerStageSampledImageCapacity,
    DescriptorSetSampledImageCapacity,
    DynamicRendering,
    Synchronization2,
    ImageCubeArray,
    ExtendedDynamicState,
    RuntimeDescriptorArray,
    VariableDescriptorCount,
    SampledImageArrayNonUniformIndexing,
};

struct VulkanFeatureTier {
    bool releaseCompatible = false;
    bool wireframeSupported = false;
    bool wideLinesSupported = false;
    // Optional quality feature, like wireframe: a device without it renders
    // correctly, just with more blurring on obliquely viewed surfaces.
    bool anisotropicFilteringSupported = false;
    // The planned heap fills every allocated slot with a fallback descriptor,
    // so partially-bound access is useful capability information but is not a
    // release requirement and is not enabled by the current device chain.
    bool partiallyBoundDescriptorsSupported = false;
    VulkanFeatureTierRejection rejection = VulkanFeatureTierRejection::None;
};

struct VulkanTextureHeapCapacity {
    uint32_t required = 0;
    uint32_t editorReserve = 0;
    uint32_t importedReserve = 0;
    uint32_t available = 0;
    uint32_t capacity = 0;
    bool supported = false;
};

[[nodiscard]] VulkanFeatureTier chooseVulkanFeatureTier(
    const VulkanDeviceFeatureSupport& support,
    uint32_t requiredPushConstantsSize,
    uint32_t requiredPerStageSampledImages,
    uint32_t requiredDescriptorSetSampledImages);
[[nodiscard]] std::string_view vulkanFeatureTierRejectionMessage(
    VulkanFeatureTierRejection rejection);
[[nodiscard]] VulkanTextureHeapCapacity chooseVulkanTextureHeapCapacity(
    const VulkanDeviceFeatureSupport& support,
    uint32_t required,
    uint32_t editorReserve,
    uint32_t importedReserve,
    uint32_t configuredCeiling,
    uint32_t otherPerStageSampledImages);
[[nodiscard]] std::string vulkanTextureHeapCapacityFailureMessage(
    const VulkanTextureHeapCapacity& capacity);

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
