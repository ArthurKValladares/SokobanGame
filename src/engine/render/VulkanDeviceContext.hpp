#pragma once

#include "engine/render/VulkanDeviceSelection.hpp"

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sokoban {

struct VulkanQueueFamilyIndices {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;

    [[nodiscard]] bool complete() const
    {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }
};

// Owns the Vulkan objects whose lifetime defines a logical device context.
// Resources created from these handles must be destroyed before this object.
class VulkanDeviceContext {
public:
    VulkanDeviceContext(
        SDL_Window* window,
        std::size_t requiredTextureDescriptors = 1);
    ~VulkanDeviceContext();

    VulkanDeviceContext(const VulkanDeviceContext&) = delete;
    VulkanDeviceContext& operator=(const VulkanDeviceContext&) = delete;

    [[nodiscard]] VkInstance instance() const;
    [[nodiscard]] VkSurfaceKHR surface() const;
    [[nodiscard]] VkPhysicalDevice physicalDevice() const;
    [[nodiscard]] const VkPhysicalDeviceProperties& physicalDeviceProperties() const;
    [[nodiscard]] VkDevice device() const;
    [[nodiscard]] const VulkanQueueFamilyIndices& queueFamilies() const;
    [[nodiscard]] VkQueue graphicsQueue() const;
    [[nodiscard]] VkQueue presentQueue() const;
    [[nodiscard]] VkCommandPool commandPool() const;

    [[nodiscard]] bool wireframeSupported() const;
    [[nodiscard]] bool wideLinesSupported() const;
    // Highest anisotropy the device allows, or 1.0 when unsupported - which is
    // the value a sampler uses to mean "off", so callers need no branch.
    [[nodiscard]] float maxSamplerAnisotropy() const;
    [[nodiscard]] uint32_t textureDescriptorCapacity() const;
    [[nodiscard]] bool graphicsTimestampsSupported() const;
    [[nodiscard]] float timestampPeriodNanoseconds() const;
    [[nodiscard]] uint32_t graphicsTimestampValidBits() const;
    [[nodiscard]] std::array<float, 2> wireframeLineWidthRange() const;
    [[nodiscard]] VkSampleCountFlagBits supportedSampleCount(
        VkSampleCountFlagBits requested) const;
    void waitIdle() const;

private:
    void createInstance();
    void createSurface();
    void createValidationMessenger();
    void pickPhysicalDevice();
    void createDevice();
    void createCommandPool();
    void destroy() noexcept;

    [[nodiscard]] VulkanQueueFamilyIndices findQueueFamilies(
        VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    [[nodiscard]] VulkanDeviceFeatureSupport queryFeatureSupport(
        VkPhysicalDevice device) const;

    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT validationMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties_ {};
    VkDevice device_ = VK_NULL_HANDLE;
    VulkanQueueFamilyIndices queueFamilies_ {};
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VulkanFeatureTier featureTier_ {};
    uint32_t requiredTextureDescriptors_ = 1;
    uint32_t textureDescriptorCapacity_ = 0;
    bool wideLinesSupported_ = false;
    uint32_t graphicsTimestampValidBits_ = 0;
    std::array<float, 2> wireframeLineWidthRange_ { 1.0f, 1.0f };
};

} // namespace sokoban
