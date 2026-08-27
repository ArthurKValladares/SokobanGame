#include "engine/render/VulkanDeviceSelection.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanDiagnostics.hpp"
#include "engine/render/VulkanGpuProfiler.hpp"
#include "engine/render/VulkanPipelineCache.hpp"
#include "engine/render/VulkanResourceUtils.hpp"
#include "engine/render/RenderResolution.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

VkPhysicalDeviceProperties properties(
    VkPhysicalDeviceType type,
    uint32_t maxImageDimension2D)
{
    VkPhysicalDeviceProperties result {};
    result.deviceType = type;
    result.limits.maxImageDimension2D = maxImageDimension2D;
    return result;
}

bool throwsForNoSurfaceMode(const VkSurfaceCapabilitiesKHR& capabilities)
{
    try {
        (void)sokoban::chooseCompositeAlpha(capabilities);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

sokoban::VulkanDeviceFeatureSupport releaseFeatureSupport()
{
    return {
        .apiVersion = VK_API_VERSION_1_3,
        .maxPushConstantsSize = 128,
        .maxPerStageDescriptorSampledImages = 32,
        .maxDescriptorSetSampledImages = 32,
        .dynamicRendering = true,
        .synchronization2 = true,
        .imageCubeArray = true,
        .extendedDynamicState = true,
        .runtimeDescriptorArray = true,
        .descriptorBindingVariableDescriptorCount = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
    };
}

sokoban::VulkanPipelineCacheIdentity pipelineCacheIdentity()
{
    sokoban::VulkanPipelineCacheIdentity identity {
        .vendorId = 0x1234,
        .deviceId = 0x5678,
    };
    for (std::size_t index = 0; index < identity.uuid.size(); ++index) {
        identity.uuid[index] = static_cast<uint8_t>(index);
    }
    return identity;
}

} // namespace

int main()
{
    const auto discrete = properties(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 8192);
    const auto integrated = properties(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 16384);
    const auto virtualGpu = properties(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU, 32768);
    const auto cpu = properties(VK_PHYSICAL_DEVICE_TYPE_CPU, 32768);

    check(
        sokoban::vulkanDevicePreferenceScore(discrete) >
            sokoban::vulkanDevicePreferenceScore(integrated),
        "a discrete GPU outranks an integrated GPU regardless of enumeration order");
    check(
        sokoban::vulkanDevicePreferenceScore(integrated) >
            sokoban::vulkanDevicePreferenceScore(virtualGpu),
        "an integrated GPU outranks a virtual GPU");
    check(
        sokoban::vulkanDevicePreferenceScore(virtualGpu) >
            sokoban::vulkanDevicePreferenceScore(cpu),
        "a virtual GPU outranks a CPU device");
    check(
        sokoban::vulkanDevicePreferenceScore(
            properties(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 16384)) >
            sokoban::vulkanDevicePreferenceScore(discrete),
        "image limits break ties within a device class");
    check(
        std::string_view(sokoban::vulkanDeviceTypeName(
            VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) == "discrete",
        "device type diagnostics are readable");
    check(
        sokoban::scaledRenderExtent({ 3840, 2160 }, 50) ==
            sokoban::PixelExtent { 1920, 1080 },
        "50 percent maps 4K output to 1080p rendering");
    check(
        sokoban::scaledRenderExtent({ 3840, 2160 }, 67) ==
            sokoban::PixelExtent { 2560, 1440 },
        "67 percent maps 4K output to 1440p rendering");
    check(
        sokoban::scaledRenderExtent({ 3840, 2160 }, 75) ==
            sokoban::PixelExtent { 2880, 1620 },
        "75 percent preset scales both dimensions");
    check(
        sokoban::scaledRenderExtent({ 1920, 1080 }, 63) ==
            sokoban::PixelExtent { 1210, 680 },
        "custom percentages are rounded to the nearest pixel");
    check(
        sokoban::scaledRenderExtent({ 1, 1 }, 25) ==
            sokoban::PixelExtent { 1, 1 },
        "non-empty output never produces a zero-sized render target");
    check(sokoban::normalizedRenderScalePercent(10) == 25,
        "custom scales clamp to the minimum");
    check(sokoban::normalizedRenderScalePercent(120) == 100,
        "custom scales clamp to the maximum");
    check(sokoban::normalizedRenderScalePresetPercent(42) == 100,
        "unsupported presets normalize to native resolution");

    VkSurfaceCapabilitiesKHR currentTransformSupported {};
    currentTransformSupported.supportedTransforms =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
        VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    currentTransformSupported.currentTransform =
        VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    check(
        sokoban::chooseSurfaceTransform(currentTransformSupported) ==
            VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
        "surface uses its supported current transform");

    VkSurfaceCapabilitiesKHR fallbackTransform {};
    fallbackTransform.supportedTransforms =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
        VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR;
    fallbackTransform.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    check(
        sokoban::chooseSurfaceTransform(fallbackTransform) ==
            VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        "unsupported current transform falls back to supported identity");

    VkSurfaceCapabilitiesKHR rotationOnlyTransform {};
    rotationOnlyTransform.supportedTransforms =
        VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    check(
        sokoban::chooseSurfaceTransform(rotationOnlyTransform) ==
            VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR,
        "surface without identity uses an advertised transform");

    VkSurfaceCapabilitiesKHR opaqueAlpha {};
    opaqueAlpha.supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    check(
        sokoban::chooseCompositeAlpha(opaqueAlpha) ==
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        "opaque alpha is preferred when supported");

    VkSurfaceCapabilitiesKHR fallbackAlpha {};
    fallbackAlpha.supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    check(
        sokoban::chooseCompositeAlpha(fallbackAlpha) ==
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        "composite alpha falls back to an advertised mode");

    check(throwsForNoSurfaceMode({}),
        "surface with no composite alpha support is rejected");

    constexpr uint32_t requiredPushConstants = 128;
    constexpr uint32_t requiredSampledImages = 32;
    const auto releaseOnlyTier = sokoban::chooseVulkanFeatureTier(
        releaseFeatureSupport(), requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(releaseOnlyTier.releaseCompatible,
        "the Vulkan 1.3 release feature tier accepts its documented baseline");
    check(!releaseOnlyTier.wireframeSupported && !releaseOnlyTier.wideLinesSupported,
        "debug rasterization features do not gate the release tier");
    check(!releaseOnlyTier.partiallyBoundDescriptorsSupported,
        "fully populated descriptor heaps do not require partially-bound support");

    auto debugFeatureSupport = releaseFeatureSupport();
    debugFeatureSupport.fillModeNonSolid = true;
    debugFeatureSupport.wideLines = true;
    const auto debugTier = sokoban::chooseVulkanFeatureTier(
        debugFeatureSupport, requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(debugTier.releaseCompatible && debugTier.wireframeSupported &&
            debugTier.wideLinesSupported,
        "supported debug features augment but do not replace the release tier");

    auto oldApiSupport = releaseFeatureSupport();
    oldApiSupport.apiVersion = VK_API_VERSION_1_2;
    check(!sokoban::chooseVulkanFeatureTier(
               oldApiSupport, requiredPushConstants,
               requiredSampledImages, requiredSampledImages)
               .releaseCompatible,
        "Vulkan 1.2 is below the renderer's release contract");

    auto missingBaselineFeature = releaseFeatureSupport();
    missingBaselineFeature.extendedDynamicState = false;
    check(!sokoban::chooseVulkanFeatureTier(
               missingBaselineFeature,
               requiredPushConstants,
               requiredSampledImages,
               requiredSampledImages).releaseCompatible,
        "ordinary draw-path features remain required for release");

    auto insufficientLimits = releaseFeatureSupport();
    insufficientLimits.maxPerStageDescriptorSampledImages =
        requiredSampledImages - 1;
    check(!sokoban::chooseVulkanFeatureTier(
               insufficientLimits,
               requiredPushConstants,
               requiredSampledImages,
               requiredSampledImages).releaseCompatible,
        "descriptor capacity remains part of the release contract");

    auto insufficientSetLimits = releaseFeatureSupport();
    insufficientSetLimits.maxDescriptorSetSampledImages =
        requiredSampledImages - 1;
    const auto insufficientSetTier = sokoban::chooseVulkanFeatureTier(
        insufficientSetLimits, requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(!insufficientSetTier.releaseCompatible &&
            insufficientSetTier.rejection ==
                sokoban::VulkanFeatureTierRejection::DescriptorSetSampledImageCapacity,
        "descriptor-set sampled-image capacity is checked independently");

    auto noRuntimeArrays = releaseFeatureSupport();
    noRuntimeArrays.runtimeDescriptorArray = false;
    const auto noRuntimeArrayTier = sokoban::chooseVulkanFeatureTier(
        noRuntimeArrays, requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(!noRuntimeArrayTier.releaseCompatible &&
            sokoban::vulkanFeatureTierRejectionMessage(
                noRuntimeArrayTier.rejection).find("runtimeDescriptorArray") !=
                std::string_view::npos,
        "missing runtime descriptor arrays produce an actionable rejection");

    auto noVariableCount = releaseFeatureSupport();
    noVariableCount.descriptorBindingVariableDescriptorCount = false;
    const auto noVariableCountTier = sokoban::chooseVulkanFeatureTier(
        noVariableCount, requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(!noVariableCountTier.releaseCompatible &&
            noVariableCountTier.rejection ==
                sokoban::VulkanFeatureTierRejection::VariableDescriptorCount,
        "variable descriptor counts are required by the runtime heap tier");

    auto noNonUniformIndexing = releaseFeatureSupport();
    noNonUniformIndexing.shaderSampledImageArrayNonUniformIndexing = false;
    const auto noNonUniformTier = sokoban::chooseVulkanFeatureTier(
        noNonUniformIndexing, requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(!noNonUniformTier.releaseCompatible &&
            noNonUniformTier.rejection ==
                sokoban::VulkanFeatureTierRejection::SampledImageArrayNonUniformIndexing,
        "non-uniform sampled-image indexing is required by material handles");

    auto partiallyBoundSupport = releaseFeatureSupport();
    partiallyBoundSupport.descriptorBindingPartiallyBound = true;
    const auto partiallyBoundTier = sokoban::chooseVulkanFeatureTier(
        partiallyBoundSupport, requiredPushConstants,
        requiredSampledImages, requiredSampledImages);
    check(partiallyBoundTier.releaseCompatible &&
            partiallyBoundTier.partiallyBoundDescriptorsSupported,
        "partially-bound support is reported without becoming a requirement");

    auto heapSupport = releaseFeatureSupport();
    heapSupport.maxPerStageDescriptorSampledImages = 160;
    heapSupport.maxDescriptorSetSampledImages = 128;
    const auto boundedHeap = sokoban::chooseVulkanTextureHeapCapacity(
        heapSupport, 70, 8, 16, 256, 8);
    check(boundedHeap.supported && boundedHeap.capacity == 128,
        "texture heap capacity is bounded by the tightest device limit");

    const auto lowLimitHeap = sokoban::chooseVulkanTextureHeapCapacity(
        heapSupport, 110, 8, 16, 256, 8);
    const std::string lowLimitMessage =
        sokoban::vulkanTextureHeapCapacityFailureMessage(lowLimitHeap);
    check(!lowLimitHeap.supported &&
            lowLimitMessage.find("110 content") != std::string::npos &&
            lowLimitMessage.find("24 reserved") != std::string::npos &&
            lowLimitMessage.find("128") != std::string::npos,
        "low-limit heap rejection reports required, reserved and available counts");

    check(
        sokoban::vulkanDebug::validationMessageLogLevel(
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ==
            sokoban::log::Level::Error,
        "validation errors are reported at error severity");
    check(
        sokoban::vulkanDebug::validationMessageLogLevel(
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ==
            sokoban::log::Level::Warning,
        "validation warnings are reported at warning severity");
    check(
        sokoban::vulkanDebug::validationMessageLogLevel(
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) ==
            sokoban::log::Level::Debug,
        "verbose validation messages stay at debug severity");

    check(
        sokoban::vulkanFailureForResult(VK_ERROR_DEVICE_LOST) ==
            sokoban::VulkanFailure::DeviceLost,
        "device loss is classified for a user-facing shutdown");
    check(
        sokoban::vulkanFailureForResult(VK_ERROR_SURFACE_LOST_KHR) ==
            sokoban::VulkanFailure::SurfaceLost,
        "surface loss is classified for a user-facing shutdown");
    check(!sokoban::vulkanFailureForResult(VK_ERROR_OUT_OF_DATE_KHR),
        "ordinary swapchain recreation does not become a fatal graphics error");
    check(
        sokoban::vulkanFailureMessage(
            sokoban::VulkanFailure::UnsupportedHardware).find("Vulkan 1.3") !=
            std::string_view::npos,
        "unsupported hardware diagnostics state the minimum API contract");
    bool preservedDeviceLossResult = false;
    try {
        sokoban::vkCheck(VK_ERROR_DEVICE_LOST, "test device loss");
    } catch (const sokoban::VulkanError& error) {
        preservedDeviceLossResult = error.result() == VK_ERROR_DEVICE_LOST;
    }
    check(preservedDeviceLossResult,
        "Vulkan errors preserve their result for renderer recovery diagnostics");

    check(
        sokoban::vulkanTimestampDeltaMilliseconds(100, 350, 2.0f, 64) ==
            0.0005,
        "timestamp deltas convert device nanoseconds to milliseconds");
    check(
        sokoban::vulkanTimestampDeltaMilliseconds(250, 5, 1.0f, 8) ==
            0.000011,
        "timestamp deltas account for a valid-bit counter wrap");
    check(
        sokoban::vulkanTimestampDeltaMilliseconds(1, 2, 0.0f, 64) == 0.0,
        "unavailable timestamps report no duration");

    const auto cacheIdentity = pipelineCacheIdentity();
    const std::array<std::byte, 4> cachePayload {
        std::byte { 0x01 }, std::byte { 0x00 },
        std::byte { 0x7f }, std::byte { 0xff },
    };
    const std::vector<std::byte> cacheFile =
        sokoban::encodeVulkanPipelineCacheFile(cacheIdentity, cachePayload);
    const auto decodedPayload = sokoban::decodeVulkanPipelineCacheFile(
        cacheFile, cacheIdentity);
    check(decodedPayload && *decodedPayload ==
            std::vector<std::byte>(cachePayload.begin(), cachePayload.end()),
        "a versioned pipeline cache envelope round-trips binary driver data");

    auto wrongDevice = cacheIdentity;
    ++wrongDevice.deviceId;
    check(!sokoban::decodeVulkanPipelineCacheFile(cacheFile, wrongDevice),
        "pipeline cache data from another device is ignored");

    auto corruptCacheFile = cacheFile;
    corruptCacheFile.pop_back();
    check(!sokoban::decodeVulkanPipelineCacheFile(corruptCacheFile, cacheIdentity),
        "truncated pipeline cache data is ignored");

    auto tamperedCacheFile = cacheFile;
    tamperedCacheFile.back() = std::byte { 0x00 };
    check(!sokoban::decodeVulkanPipelineCacheFile(tamperedCacheFile, cacheIdentity),
        "tampered pipeline cache data is ignored");

    auto oldFormatCacheFile = cacheFile;
    oldFormatCacheFile[8] = std::byte { 0x00 };
    check(!sokoban::decodeVulkanPipelineCacheFile(oldFormatCacheFile, cacheIdentity),
        "pipeline cache data from an unsupported envelope version is ignored");

    std::cout << "Vulkan device selection tests passed\n";
    return 0;
}
