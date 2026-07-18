#pragma once

#include "engine/Config.hpp"
#include "engine/Math.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanPipelineFactory.hpp"
#include "engine/render/VulkanSceneDescriptors.hpp"
#include "engine/render/VulkanShadowPass.hpp"
#include "engine/render/VulkanSsaoPass.hpp"
#include "engine/render/VulkanSwapchainResources.hpp"
#include "engine/ui/Ui.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sokoban {

class AssetManifest;

enum class AntiAliasingMode {
    None,
    Msaa2x,
    Msaa4x,
    Msaa8x,
};

class VulkanRenderer {
public:
    // assetRoot is the staged runtime content directory containing shaders
    // and every manifest-relative asset. The manifest must outlive the
    // renderer.
    VulkanRenderer(
        SDL_Window* window,
        std::filesystem::path assetRoot,
        const AssetManifest& manifest,
        bool vsync = false);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(const RenderFrameData& frameData, const UiDrawData& uiDrawData);
    void preloadAssets(const RenderAssetRequirements& requirements);
    void ensureAssets(const RenderAssetRequirements& requirements);
    void handleEvent(const SDL_Event& event);
    void beginDebugUiFrame();
    [[nodiscard]] bool wantsKeyboardCapture() const;
    [[nodiscard]] bool wantsMouseCapture() const;
    [[nodiscard]] std::optional<GridPosition3> pickIsoGridCell(const RenderFrameData& frameData, Vec2 pixelPosition) const;
    void waitIdle() const;
    [[nodiscard]] AntiAliasingMode antiAliasingMode() const;
    [[nodiscard]] VkSampleCountFlagBits activeSampleCount() const;
    [[nodiscard]] RenderStats renderStats() const;
    [[nodiscard]] VulkanModelResources::LoadingStats assetLoadingStats() const;
    void setAntiAliasingMode(AntiAliasingMode mode);
    [[nodiscard]] bool wireframeEnabled() const;
    void setWireframeEnabled(bool enabled);
    [[nodiscard]] bool wideLinesSupported() const;
    [[nodiscard]] float wireframeLineWidth() const;
    [[nodiscard]] std::array<float, 2> wireframeLineWidthRange() const;
    void setWireframeLineWidth(float lineWidth);
    // Debug: overrides the player model's animation with an arbitrary clip
    // (nullptr restores gameplay animation). The clip must stay alive while
    // set; call every frame with the current preview time.
    void setAnimationPreview(const GltfAnimationClip* clip, float timeSeconds);

private:
    struct QueueFamilyIndices {
        uint32_t graphics = UINT32_MAX;
        uint32_t present = UINT32_MAX;

        [[nodiscard]] bool complete() const
        {
            return graphics != UINT32_MAX && present != UINT32_MAX;
        }
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

    struct ShadowRenderLayout {
        Vec3 lightRight {};
        Vec3 lightUp {};
        Vec3 lightForward {};
        Vec3 center {};
        float halfWidth = 1.0f;
        float halfHeight = 1.0f;
        float nearestDepth = 0.0f;
        float farthestDepth = 1.0f;
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    [[nodiscard]] VulkanSceneDescriptors::Resources descriptorResources() const;
    void createCommandPool();
    void createPipeline();
    void destroyPipeline();
    void createFrameResources();
    void initializeDebugUi();
    void shutdownDebugUi();
    void renderDebugUi(VkCommandBuffer commandBuffer) const;
    void recreateSwapchain();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderFrameData& frameData, const UiDrawData& uiDrawData);
    void recordShadowMapRendering(VkCommandBuffer commandBuffer, const RenderFrameData& frameData, const ShadowRenderLayout& layout);
    void recordGameRendering(VkCommandBuffer commandBuffer, VkImageView colorView, VkImageView resolveView, VkImage resolvedColorImage, const RenderFrameData& frameData);
    void recordScenePass(
        VkCommandBuffer commandBuffer,
        VkImageView colorView,
        VkImageView resolveView,
        const RenderFrameData& frameData,
        const ShadowRenderLayout& shadowLayout,
        bool translucentPass,
        bool loadColor,
        bool storeColor,
        bool loadDepth,
        bool writeDepth);
    void recordOverlayRendering(
        VkCommandBuffer commandBuffer,
        VkImage colorImage,
        VkImageView colorView,
        const UiDrawData& uiDrawData);
    [[nodiscard]] TileRenderLayout calculateTileRenderLayout(const RenderFrameData& frameData) const;
    [[nodiscard]] IsoRenderLayout calculateIsoRenderLayout(const RenderFrameData& frameData) const;
    [[nodiscard]] ShadowRenderLayout calculateShadowRenderLayout(const RenderFrameData& frameData) const;
    void drawTile(VkCommandBuffer commandBuffer, const TileRenderLayout& layout, const RenderFrameData::Tile& tile, const RenderFrameData::Lighting& lighting) const;
    void drawIsoFrame(VkCommandBuffer commandBuffer, const IsoRenderLayout& layout, const ShadowRenderLayout& shadowLayout, const RenderFrameData& frameData, const RenderFrameData::Lighting& lighting, bool translucentPass) const;
    void drawTopDownGridOverlay(VkCommandBuffer commandBuffer, const TileRenderLayout& layout, const RenderFrameData& frameData) const;
    void drawFace(
        VkCommandBuffer commandBuffer,
        const std::array<Vec3, 4>& vertices,
        const std::array<Vec4, 4>& shadowVertices,
        Vec4 color,
        Vec3 normal,
        const RenderFrameData::Lighting& lighting,
        bool blurBehind = false,
        Vec4 gridColor = {},
        Vec2 gridSize = {},
        float gridLineWidth = 0.0f,
        bool isEditorPreview = false) const;
    void drawShadowFace(VkCommandBuffer commandBuffer, const std::array<Vec4, 4>& shadowVertices) const;

    void drawModel(
        VkCommandBuffer commandBuffer,
        const IsoRenderLayout& layout,
        const ShadowRenderLayout& shadowLayout,
        const RenderFrameData::Tile& tile,
        const RenderFrameData::Lighting& lighting) const;
    void drawModelShadow(
        VkCommandBuffer commandBuffer,
        const ShadowRenderLayout& layout,
        const RenderFrameData::Tile& tile) const;
    void drawUiRect(VkCommandBuffer commandBuffer, const UiDrawCommand& command, Vec2 viewportSize, const RenderFrameData::Lighting& lighting) const;
    [[nodiscard]] Vec3 projectIsoPoint(const IsoRenderLayout& layout, Vec3 point) const;
    [[nodiscard]] Vec4 projectShadowPoint(const ShadowRenderLayout& layout, Vec3 point) const;
    [[nodiscard]] Vec2 pixelSizeToClipSpace(float pixelSize) const;

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    [[nodiscard]] VkSampleCountFlagBits sampleCountForMode(AntiAliasingMode mode) const;
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

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkFormat shadowFormat_ = VK_FORMAT_D32_SFLOAT;
    VulkanSwapchainResources swapchainResources_;
    VulkanShadowPass shadowPass_;
    VulkanSsaoPass ssaoPass_;
    VulkanSceneDescriptors sceneDescriptors_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VulkanPipelineFactory pipelines_;
    VulkanModelResources modelResources_;

    static constexpr uint32_t maxFramesInFlight_ = 2;
    std::array<FrameResources, maxFramesInFlight_> frames_ {};
    uint32_t currentFrame_ = 0;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::Msaa8x;
    VkSampleCountFlagBits activeSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    bool wireframeEnabled_ = false;
    bool wideLinesSupported_ = false;
    float wireframeLineWidth_ = 1.0f;
    std::array<float, 2> wireframeLineWidthRange_ { 1.0f, 1.0f };
    mutable RenderStats pendingStats_ {};
    RenderStats lastStats_ {};
    uint64_t nextStatsFrameIndex_ = 1;
    uint64_t pipelineRebuilds_ = 0;
    uint64_t swapchainRecreations_ = 0;
};

} // namespace sokoban
