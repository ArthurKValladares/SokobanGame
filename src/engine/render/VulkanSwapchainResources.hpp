#pragma once

#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace sokoban {

class VulkanSwapchainResources {
public:
    struct QueueFamilies {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;
    };

    VulkanSwapchainResources() = default;
    ~VulkanSwapchainResources();

    VulkanSwapchainResources(const VulkanSwapchainResources&) = delete;
    VulkanSwapchainResources& operator=(const VulkanSwapchainResources&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkSurfaceKHR surface,
        SDL_Window* window,
        QueueFamilies queueFamilies,
        VkSampleCountFlagBits sampleCount,
        VkFormat depthFormat,
        bool vsync);
    void recreate();
    void recreateAttachments(VkSampleCountFlagBits sampleCount);
    void destroy();

    [[nodiscard]] bool canRecreate() const;
    [[nodiscard]] VkResult acquire(VkSemaphore available, uint32_t& imageIndex) const;
    [[nodiscard]] VkResult present(
        VkQueue presentQueue,
        VkSemaphore finished,
        uint32_t imageIndex) const;

    void beginFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderStats& stats);
    void endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderStats& stats) const;
    void ensureSceneColorReadable(VkCommandBuffer commandBuffer, RenderStats& stats);
    void copyResolvedSceneColor(
        VkCommandBuffer commandBuffer,
        VkImage resolvedColorImage,
        RenderStats& stats);

    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }
    [[nodiscard]] VkFormat colorFormat() const { return colorFormat_; }
    [[nodiscard]] VkFormat depthFormat() const { return depthFormat_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    [[nodiscard]] VkPresentModeKHR presentMode() const { return presentMode_; }
    [[nodiscard]] uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    [[nodiscard]] VkSampleCountFlagBits sampleCount() const { return sampleCount_; }
    [[nodiscard]] bool msaaEnabled() const { return sampleCount_ != VK_SAMPLE_COUNT_1_BIT; }
    [[nodiscard]] VkImage image(uint32_t index) const;
    [[nodiscard]] VkImageView imageView(uint32_t index) const;
    [[nodiscard]] VkImageView sceneColorView() const { return sceneColorImage_.view; }
    [[nodiscard]] VkSampler sceneColorSampler() const { return sceneColorSampler_; }
    [[nodiscard]] VkImageView depthView() const { return depthImage_.view; }
    [[nodiscard]] VkImageView sampledDepthView() const;
    [[nodiscard]] VkImage depthSourceImage() const;
    [[nodiscard]] VkImageView resolveDepthView() const { return resolveDepthImage_.view; }
    [[nodiscard]] VkImageView renderColorView(uint32_t imageIndex) const;
    [[nodiscard]] VkImageView resolveColorView(uint32_t imageIndex) const;

private:
    struct SwapchainImage {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    void createSwapchain();
    void createAttachments();
    void createMsaaColor();
    void createDepth();
    void createSceneColor();
    void destroyAttachments();
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const;
    [[nodiscard]] VkExtent2D chooseExtent(
        const VkSurfaceCapabilitiesKHR& capabilities) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    SDL_Window* window_ = nullptr;
    QueueFamilies queueFamilies_ {};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkExtent2D extent_ {};
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    bool vsync_ = false;
    std::vector<SwapchainImage> images_;
    vulkanResources::OwnedImage msaaColorImage_ {};
    vulkanResources::OwnedImage depthImage_ {};
    vulkanResources::OwnedImage resolveDepthImage_ {};
    vulkanResources::OwnedImage sceneColorImage_ {};
    VkSampler sceneColorSampler_ = VK_NULL_HANDLE;
    VkImageLayout sceneColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    bool depthLayoutInitialized_ = false;
};

} // namespace sokoban
