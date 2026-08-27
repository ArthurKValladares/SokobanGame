#include "engine/render/VulkanDeviceSelection.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace sokoban {
namespace {

VulkanFeatureTierRejection featureTierRejection(
    const VulkanDeviceFeatureSupport& support,
    uint32_t requiredPushConstantsSize,
    uint32_t requiredPerStageSampledImages,
    uint32_t requiredDescriptorSetSampledImages)
{
    if (support.apiVersion < VK_API_VERSION_1_3) {
        return VulkanFeatureTierRejection::Vulkan13;
    }
    if (support.maxPushConstantsSize < requiredPushConstantsSize) {
        return VulkanFeatureTierRejection::PushConstantCapacity;
    }
    if (support.maxPerStageDescriptorSampledImages <
        requiredPerStageSampledImages) {
        return VulkanFeatureTierRejection::PerStageSampledImageCapacity;
    }
    if (support.maxDescriptorSetSampledImages <
        requiredDescriptorSetSampledImages) {
        return VulkanFeatureTierRejection::DescriptorSetSampledImageCapacity;
    }
    if (!support.dynamicRendering) {
        return VulkanFeatureTierRejection::DynamicRendering;
    }
    if (!support.synchronization2) {
        return VulkanFeatureTierRejection::Synchronization2;
    }
    if (!support.imageCubeArray) {
        return VulkanFeatureTierRejection::ImageCubeArray;
    }
    if (!support.extendedDynamicState) {
        return VulkanFeatureTierRejection::ExtendedDynamicState;
    }
    if (!support.runtimeDescriptorArray) {
        return VulkanFeatureTierRejection::RuntimeDescriptorArray;
    }
    if (!support.descriptorBindingVariableDescriptorCount) {
        return VulkanFeatureTierRejection::VariableDescriptorCount;
    }
    if (!support.shaderSampledImageArrayNonUniformIndexing) {
        return VulkanFeatureTierRejection::SampledImageArrayNonUniformIndexing;
    }
    return VulkanFeatureTierRejection::None;
}

} // namespace

VulkanFeatureTier chooseVulkanFeatureTier(
    const VulkanDeviceFeatureSupport& support,
    uint32_t requiredPushConstantsSize,
    uint32_t requiredPerStageSampledImages,
    uint32_t requiredDescriptorSetSampledImages)
{
    const VulkanFeatureTierRejection rejection = featureTierRejection(
        support,
        requiredPushConstantsSize,
        requiredPerStageSampledImages,
        requiredDescriptorSetSampledImages);
    const bool releaseCompatible =
        rejection == VulkanFeatureTierRejection::None;
    return {
        .releaseCompatible = releaseCompatible,
        .wireframeSupported = releaseCompatible && support.fillModeNonSolid,
        .wideLinesSupported =
            releaseCompatible && support.fillModeNonSolid && support.wideLines,
        .anisotropicFilteringSupported =
            releaseCompatible && support.samplerAnisotropy,
        .partiallyBoundDescriptorsSupported =
            support.descriptorBindingPartiallyBound,
        .rejection = rejection,
    };
}

VulkanTextureHeapCapacity chooseVulkanTextureHeapCapacity(
    const VulkanDeviceFeatureSupport& support,
    uint32_t required,
    uint32_t editorReserve,
    uint32_t importedReserve,
    uint32_t configuredCeiling,
    uint32_t otherPerStageSampledImages)
{
    const uint32_t perStageAvailable =
        support.maxPerStageDescriptorSampledImages >
            otherPerStageSampledImages
        ? support.maxPerStageDescriptorSampledImages -
            otherPerStageSampledImages
        : 0;
    const uint32_t available = std::min({
        configuredCeiling,
        perStageAvailable,
        support.maxDescriptorSetSampledImages,
    });
    const uint64_t requested = static_cast<uint64_t>(required) +
        editorReserve + importedReserve;
    return {
        .required = required,
        .editorReserve = editorReserve,
        .importedReserve = importedReserve,
        .available = available,
        .capacity = available,
        .supported = required > 0 && requested <= available,
    };
}

std::string vulkanTextureHeapCapacityFailureMessage(
    const VulkanTextureHeapCapacity& capacity)
{
    return "texture descriptor heap requires " +
        std::to_string(capacity.required) + " content descriptors plus " +
        std::to_string(capacity.editorReserve + capacity.importedReserve) +
        " reserved (" + std::to_string(capacity.editorReserve) +
        " editor, " + std::to_string(capacity.importedReserve) +
        " imported), but only " + std::to_string(capacity.available) +
        " are available";
}

std::string_view vulkanFeatureTierRejectionMessage(
    VulkanFeatureTierRejection rejection)
{
    switch (rejection) {
    case VulkanFeatureTierRejection::None:
        return "device satisfies the Vulkan renderer feature tier";
    case VulkanFeatureTierRejection::Vulkan13:
        return "requires Vulkan 1.3";
    case VulkanFeatureTierRejection::PushConstantCapacity:
        return "insufficient push-constant capacity";
    case VulkanFeatureTierRejection::PerStageSampledImageCapacity:
        return "insufficient per-stage sampled-image descriptor capacity";
    case VulkanFeatureTierRejection::DescriptorSetSampledImageCapacity:
        return "insufficient descriptor-set sampled-image capacity";
    case VulkanFeatureTierRejection::DynamicRendering:
        return "requires dynamicRendering";
    case VulkanFeatureTierRejection::Synchronization2:
        return "requires synchronization2";
    case VulkanFeatureTierRejection::ImageCubeArray:
        return "requires imageCubeArray";
    case VulkanFeatureTierRejection::ExtendedDynamicState:
        return "requires extendedDynamicState";
    case VulkanFeatureTierRejection::RuntimeDescriptorArray:
        return "requires runtimeDescriptorArray";
    case VulkanFeatureTierRejection::VariableDescriptorCount:
        return "requires descriptorBindingVariableDescriptorCount";
    case VulkanFeatureTierRejection::SampledImageArrayNonUniformIndexing:
        return "requires shaderSampledImageArrayNonUniformIndexing";
    }
    return "does not satisfy the Vulkan renderer feature tier";
}

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
