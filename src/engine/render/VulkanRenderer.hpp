#pragma once

#include "engine/Math.hpp"
#include "engine/ui/Ui.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sokoban {

enum class RenderViewMode {
    TopDown2D,
    Isometric3D,
};

enum class AntiAliasingMode {
    None,
    Msaa2x,
    Msaa4x,
    Msaa8x,
};

struct RenderFrameData {
    struct Tile {
        Vec2 position {};
        Vec2 size { 1.0f, 1.0f };
        Vec4 color {};
        float baseElevation = 0.0f;
        float height = 0.0f;
    };

    struct IsoFace {
        std::array<Vec3, 4> vertices {};
        Vec4 color {};
    };

    RenderViewMode viewMode = RenderViewMode::TopDown2D;
    uint32_t levelWidth = 0;
    uint32_t levelHeight = 0;
    Vec2 playerPosition {};
    std::vector<Tile> tiles;
    std::vector<IsoFace> isoFaces;
};

struct RenderStats {
    uint64_t frameIndex = 0;
    uint32_t totalTiles = 0;
    uint32_t visibleFaces = 0;
    uint32_t drawCalls = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t pipelineBinds = 0;
    uint32_t renderPasses = 0;
    uint32_t imageBarriers = 0;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;
    uint32_t swapchainImages = 0;
    uint32_t activeSamples = 1;
    bool wireframeEnabled = false;
    float wireframeLineWidth = 1.0f;
    uint64_t pipelineRebuilds = 0;
    uint64_t swapchainRecreations = 0;
};

class VulkanRenderer {
public:
    VulkanRenderer(SDL_Window* window, std::filesystem::path assetRoot);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(const RenderFrameData& frameData, const UiDrawData& uiDrawData);
    void handleEvent(const SDL_Event& event);
    void beginDebugUiFrame();
    [[nodiscard]] bool wantsKeyboardCapture() const;
    [[nodiscard]] bool wantsMouseCapture() const;
    void waitIdle() const;
    [[nodiscard]] AntiAliasingMode antiAliasingMode() const;
    [[nodiscard]] VkSampleCountFlagBits activeSampleCount() const;
    [[nodiscard]] RenderStats renderStats() const;
    void setAntiAliasingMode(AntiAliasingMode mode);
    [[nodiscard]] bool wireframeEnabled() const;
    void setWireframeEnabled(bool enabled);
    [[nodiscard]] bool wideLinesSupported() const;
    [[nodiscard]] float wireframeLineWidth() const;
    [[nodiscard]] std::array<float, 2> wireframeLineWidthRange() const;
    void setWireframeLineWidth(float lineWidth);

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

    struct OwnedImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    struct TileRenderLayout {
        Vec2 boardBottomLeft {};
        Vec2 tileSize {};
    };

    struct IsoRenderLayout {
        Vec3 cameraPosition {};
        Vec3 cameraRight {};
        Vec3 cameraUp {};
        Vec3 cameraForward {};
        Vec2 projectedCenter {};
        float focalLength = 1.0f;
        float fitScale = 1.0f;
        float nearestDepth = 0.0f;
        float farthestDepth = 1.0f;
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createImageViews();
    void createMsaaColorResources();
    void createDepthResources();
    void createCommandPool();
    void createPipeline();
    void destroyPipeline();
    void createFrameResources();
    void initializeDebugUi();
    void shutdownDebugUi();
    void renderDebugUi(VkCommandBuffer commandBuffer) const;
    void recreateSwapchain();
    void cleanupSwapchain();
    void cleanupMsaaColorResources();
    void cleanupDepthResources();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderFrameData& frameData, const UiDrawData& uiDrawData);
    void recordGameRendering(VkCommandBuffer commandBuffer, VkImageView colorView, VkImageView resolveView, const RenderFrameData& frameData);
    void recordUiRendering(VkCommandBuffer commandBuffer, VkImageView colorView, const UiDrawData& uiDrawData);
    void recordDebugUiRendering(VkCommandBuffer commandBuffer, VkImageView colorView) const;
    [[nodiscard]] TileRenderLayout calculateTileRenderLayout(const RenderFrameData& frameData) const;
    [[nodiscard]] IsoRenderLayout calculateIsoRenderLayout(const RenderFrameData& frameData) const;
    void drawTile(VkCommandBuffer commandBuffer, const TileRenderLayout& layout, const RenderFrameData::Tile& tile) const;
    void drawIsoFrame(VkCommandBuffer commandBuffer, const IsoRenderLayout& layout, const RenderFrameData& frameData) const;
    void drawIsoTile(VkCommandBuffer commandBuffer, const IsoRenderLayout& layout, const RenderFrameData::Tile& tile) const;
    void drawFace(VkCommandBuffer commandBuffer, const std::array<Vec3, 4>& vertices, Vec4 color) const;
    void drawUiRect(VkCommandBuffer commandBuffer, const UiDrawCommand& command, Vec2 viewportSize) const;
    [[nodiscard]] Vec3 projectIsoPoint(const IsoRenderLayout& layout, Vec3 point) const;
    [[nodiscard]] Vec2 pixelSizeToClipSpace(float pixelSize) const;

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    [[nodiscard]] VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    [[nodiscard]] VkShaderModule createShaderModule(const std::filesystem::path& path) const;
    [[nodiscard]] std::array<VkPipeline, 2> createGraphicsPipelineLibraries(VkShaderModule vertexShader, VkShaderModule fragmentShader) const;
    [[nodiscard]] VkSampleCountFlagBits sampleCountForMode(AntiAliasingMode mode) const;
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectMask) const;
    [[nodiscard]] bool msaaEnabled() const;
    [[nodiscard]] uint32_t sampleCountValue() const;

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
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkExtent2D swapchainExtent_ {};
    std::vector<SwapchainImage> swapchainImages_;
    OwnedImage msaaColorImage_ {};
    OwnedImage depthImage_ {};

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> pipelineLibraries_ {};
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    static constexpr uint32_t maxFramesInFlight_ = 2;
    std::array<FrameResources, maxFramesInFlight_> frames_ {};
    uint32_t currentFrame_ = 0;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::None;
    VkSampleCountFlagBits activeSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    bool wireframeEnabled_ = false;
    bool wideLinesSupported_ = false;
    bool depthLayoutInitialized_ = false;
    float wireframeLineWidth_ = 1.0f;
    std::array<float, 2> wireframeLineWidthRange_ { 1.0f, 1.0f };
    mutable RenderStats pendingStats_ {};
    RenderStats lastStats_ {};
    uint64_t nextStatsFrameIndex_ = 1;
    uint64_t pipelineRebuilds_ = 0;
    uint64_t swapchainRecreations_ = 0;
};

} // namespace sokoban
