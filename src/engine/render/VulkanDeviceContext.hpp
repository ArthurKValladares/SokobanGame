#pragma once

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
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
    explicit VulkanDeviceContext(SDL_Window* window);
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

    [[nodiscard]] bool wideLinesSupported() const;
    [[nodiscard]] std::array<float, 2> wireframeLineWidthRange() const;
    [[nodiscard]] VkSampleCountFlagBits supportedSampleCount(
        VkSampleCountFlagBits requested) const;
    void waitIdle() const;

private:
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createCommandPool();
    void destroy() noexcept;

    [[nodiscard]] VulkanQueueFamilyIndices findQueueFamilies(
        VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;

    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties_ {};
    VkDevice device_ = VK_NULL_HANDLE;
    VulkanQueueFamilyIndices queueFamilies_ {};
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    bool wideLinesSupported_ = false;
    std::array<float, 2> wireframeLineWidthRange_ { 1.0f, 1.0f };
};

} // namespace sokoban
