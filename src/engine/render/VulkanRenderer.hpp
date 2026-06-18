#pragma once

#include "engine/Math.hpp"

#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sokoban {

struct RenderFrameData {
    struct Tile {
        Vec2 position {};
        Vec4 color {};
    };

    uint32_t levelWidth = 0;
    uint32_t levelHeight = 0;
    Vec2 playerPosition {};
    std::vector<Tile> tiles;
};

class VulkanRenderer {
public:
    VulkanRenderer(SDL_Window* window, std::filesystem::path assetRoot);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(const RenderFrameData& frameData);
    void waitIdle() const;

private:
    struct QueueFamilyIndices {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;

        [[nodiscard]] bool complete() const
        {
            return graphics != UINT32_MAX && present != UINT32_MAX;
        }
    };

    struct SwapchainImage {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    struct TileRenderLayout {
        Vec2 boardTopLeft {};
        Vec2 tileSize {};
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createImageViews();
    void createCommandPool();
    void createPipeline();
    void createFrameResources();
    void recreateSwapchain();
    void cleanupSwapchain();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderFrameData& frameData);
    [[nodiscard]] TileRenderLayout calculateTileRenderLayout(const RenderFrameData& frameData) const;
    void drawTile(VkCommandBuffer commandBuffer, const TileRenderLayout& layout, const RenderFrameData::Tile& tile) const;
    [[nodiscard]] Vec2 pixelSizeToClipSpace(float pixelSize) const;

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    [[nodiscard]] VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    [[nodiscard]] VkShaderModule createShaderModule(const std::filesystem::path& path) const;
    [[nodiscard]] std::array<VkPipeline, 2> createGraphicsPipelineLibraries(VkShaderModule vertexShader, VkShaderModule fragmentShader) const;

    SDL_Window* window_ = nullptr;
    std::filesystem::path assetRoot_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    QueueFamilyIndices queueFamilies_ {};
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_ {};
    std::vector<SwapchainImage> swapchainImages_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> pipelineLibraries_ {};
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    static constexpr uint32_t maxFramesInFlight_ = 2;
    std::array<FrameResources, maxFramesInFlight_> frames_ {};
    uint32_t currentFrame_ = 0;
};

} // namespace sokoban
