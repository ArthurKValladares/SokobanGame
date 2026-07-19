#include "engine/render/VulkanDeviceSelection.hpp"
#include "engine/render/RenderResolution.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

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
        sokoban::scaledRenderExtent({ 1, 1 }, 25) ==
            sokoban::PixelExtent { 1, 1 },
        "non-empty output never produces a zero-sized render target");
    check(
        sokoban::scaledRenderExtent({ 1920, 1080 }, 42) ==
            sokoban::PixelExtent { 1920, 1080 },
        "unsupported scales normalize to native resolution");

    std::cout << "Vulkan device selection tests passed\n";
    return 0;
}
