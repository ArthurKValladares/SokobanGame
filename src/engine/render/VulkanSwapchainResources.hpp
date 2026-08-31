#pragma once

#include "engine/PresentationPolicy.hpp"
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
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkSurfaceKHR surface,
        SDL_Window* window,
        QueueFamilies queueFamilies,
        VkSampleCountFlagBits sampleCount,
        int renderScalePercent,
        VkFormat depthFormat,
        PresentationPolicy presentationPolicy,
        VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
    void recreate();
    void recreateAttachments(
        VkSampleCountFlagBits sampleCount,
        int renderScalePercent);
    void destroy();

    [[nodiscard]] bool canRecreate() const;
    [[nodiscard]] VkResult acquire(VkSemaphore available, uint32_t& imageIndex) const;
    [[nodiscard]] VkResult present(
        VkQueue presentQueue,
        VkSemaphore finished,
        uint32_t imageIndex) const;

    void beginFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderStats& stats);
    void endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderStats& stats) const;
    void prepareSwapchainForUi(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        RenderStats& stats) const;
    void ensureSceneColorReadable(VkCommandBuffer commandBuffer, RenderStats& stats);
    void prepareSceneColorAttachment(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void publishSceneColor(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    // Hands the scene target to the tonemap pass and its display image to the
    // rasterizer. Everything that used to read the scene target directly -
    // the upscale blit, the developer workspace's game viewport, the frame
    // capture - reads the display image now, which is what lets the two
    // carry different formats.
    void beginTonemap(VkCommandBuffer commandBuffer, RenderStats& stats);
    // Only the developer workspace needs this: ImGui samples the display
    // image for its Game Viewport instead of presenting it.
    void publishDisplayColor(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void copyResolvedSceneColor(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    // Publishes the single-sample depth resolve directly for shader reads;
    // prepareSceneDepthAttachment restores it before another scene render.
    void publishSceneDepth(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void prepareSceneDepthAttachment(
        VkCommandBuffer commandBuffer,
        RenderStats& stats);
    void upscaleSceneToSwapchain(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        RenderStats& stats);

    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }
    // The swapchain's own format, and the format of the display image the
    // tonemap pass writes. Both are what the surface wants presented.
    [[nodiscard]] VkFormat colorFormat() const { return colorFormat_; }
    // What the scene renders into. Decoupled from colorFormat() so that the
    // scene can carry range the surface cannot.
    [[nodiscard]] VkFormat sceneColorFormat() const { return sceneColorFormat_; }
    [[nodiscard]] VkFormat depthFormat() const { return depthFormat_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    [[nodiscard]] VkExtent2D renderExtent() const { return renderExtent_; }
    [[nodiscard]] int renderScalePercent() const { return renderScalePercent_; }
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
    [[nodiscard]] VkImage resolvedColorImage() const { return resolvedColorImage_.image; }
    [[nodiscard]] VkImageView resolvedColorView() const { return resolvedColorImage_.view; }
    [[nodiscard]] VkImage displayColorImage() const { return displayColorImage_.image; }
    [[nodiscard]] VkImageView displayColorView() const { return displayColorImage_.view; }
    [[nodiscard]] VkImageView renderColorView(
        bool resolveIntoSampledScene = false) const;
    [[nodiscard]] VkImageView resolveColorView(
        bool resolveIntoSampledScene = false) const;

private:
    struct SwapchainImage {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    void createSwapchain(VkSwapchainKHR oldSwapchain);
    void createAttachments();
    void createResolvedColor();
    void createMsaaColor();
    void createDepth();
    void createSceneColor();
    void createDisplayColor();
    void destroyAttachments();
    [[nodiscard]] VkFormat chooseSceneColorFormat() const;
    [[nodiscard]] vulkanResources::ImageState displayColorSourceState() const;
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const;
    [[nodiscard]] VkExtent2D chooseExtent(
        const VkSurfaceCapabilitiesKHR& capabilities) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    SDL_Window* window_ = nullptr;
    QueueFamilies queueFamilies_ {};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat sceneColorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkExtent2D extent_ {};
    VkExtent2D renderExtent_ {};
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    int renderScalePercent_ = 100;
    PresentationPolicy presentationPolicy_ {};
    std::vector<SwapchainImage> images_;
    vulkanResources::OwnedImage resolvedColorImage_ {};
    vulkanResources::OwnedImage msaaColorImage_ {};
    vulkanResources::OwnedImage depthImage_ {};
    vulkanResources::OwnedImage resolveDepthImage_ {};
    vulkanResources::OwnedImage sceneColorImage_ {};
    vulkanResources::OwnedImage displayColorImage_ {};
    VkSampler sceneColorSampler_ = VK_NULL_HANDLE;
    VkImageLayout sceneColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    // There is one display image and more than one frame in flight, and in
    // the developer workspace ImGui samples it at the end of every frame.
    // The next frame's tonemap has to name that read as the thing it is
    // waiting on, or its write races a sample still in progress.
    VkImageLayout displayColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    bool depthLayoutInitialized_ = false;
};

} // namespace sokoban
