#pragma once

#include "engine/Config.hpp"
#include "engine/Math.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/FrameDescriptorSync.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/render/VulkanDeviceContext.hpp"
#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanPipelineFactory.hpp"
#include "engine/render/VulkanSceneRecorder.hpp"
#include "engine/render/VulkanSceneDescriptors.hpp"
#include "engine/render/VulkanShadowPass.hpp"
#include "engine/render/VulkanSsaoPass.hpp"
#include "engine/render/VulkanSwapchainResources.hpp"
#include "engine/render/VulkanUiResources.hpp"
#include "engine/ui/Ui.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class AssetManifest;
class FontAtlas;

enum class AntiAliasingMode {
    None,
    Msaa2x,
    Msaa4x,
    Msaa8x,
};

class VulkanRenderer {
public:
    struct PreparedFrame {
        uint32_t levelWidth = 0;
        uint32_t levelHeight = 0;

    private:
        friend class VulkanRenderer;
        uint32_t scratchIndex = 0;
        uint64_t generation = 0;
    };

    // assetRoot is the staged runtime content directory containing shaders
    // and every manifest-relative asset. The manifest must outlive the
    // renderer.
    VulkanRenderer(
        SDL_Window* window,
        std::filesystem::path assetRoot,
        const AssetManifest& manifest,
        const FontAtlas& uiFont,
        AntiAliasingMode antiAliasingMode = AntiAliasingMode::Msaa8x,
        int renderScalePercent = 100,
        bool vsync = false);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    [[nodiscard]] PreparedFrame prepareFrame(RenderFrameData frameData);
    void drawFrame(
        const PreparedFrame& frame,
        const UiDrawData& uiDrawData);
    void preloadAssets(const RenderAssetRequirements& requirements);
    void ensureAssets(const RenderAssetRequirements& requirements);
    void handleEvent(const SDL_Event& event);
    void beginDebugUiFrame();
    [[nodiscard]] bool wantsKeyboardCapture() const;
    [[nodiscard]] bool wantsMouseCapture() const;
    [[nodiscard]] std::optional<GridPosition3> pickIsoGridCell(
        const PreparedFrame& frame,
        Vec2 pixelPosition) const;
    void waitIdle() const;
    [[nodiscard]] AntiAliasingMode antiAliasingMode() const;
    [[nodiscard]] VkSampleCountFlagBits activeSampleCount() const;
    [[nodiscard]] RenderStats renderStats() const;
    [[nodiscard]] VulkanModelResources::LoadingStats assetLoadingStats() const;
    [[nodiscard]] std::string_view physicalDeviceName() const;
    [[nodiscard]] const char* physicalDeviceTypeName() const;
    [[nodiscard]] const char* presentModeName() const;
    void setAntiAliasingMode(AntiAliasingMode mode);
    [[nodiscard]] int renderScalePercent() const;
    void setRenderScalePercent(int percent);
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
    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    struct PreparedFrameScratch {
        RenderFrameData frameData;
        PreparedRenderScene scene;
        uint64_t generation = 0;
    };

    [[nodiscard]] const PreparedFrameScratch& resolvePreparedFrame(
        const PreparedFrame& frame) const;
    [[nodiscard]] VulkanSceneDescriptors::Resources descriptorResources() const;
    void createPipeline();
    void destroyPipeline();
    void createFrameResources();
    void initializeDebugUi();
    void shutdownDebugUi();
    void recreateSwapchain();
    void logRenderConfiguration() const;
    [[nodiscard]] VkSampleCountFlagBits sampleCountForMode(AntiAliasingMode mode) const;
    [[nodiscard]] uint32_t sampleCountValue() const;

    SDL_Window* window_ = nullptr;
    std::filesystem::path assetRoot_;
    VulkanDeviceContext deviceContext_;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkFormat shadowFormat_ = VK_FORMAT_D32_SFLOAT;
    VulkanSwapchainResources swapchainResources_;
    VulkanShadowPass shadowPass_;
    VulkanSsaoPass ssaoPass_;
    VulkanSceneDescriptors sceneDescriptors_;
    VulkanUiResources uiResources_;

    VulkanPipelineFactory pipelines_;
    VulkanModelResources modelResources_;
    VulkanSceneRecorder sceneRecorder_;

    static constexpr uint32_t maxFramesInFlight_ = 2;
    static constexpr uint32_t preparedFrameSlotCount_ = 2;
    std::array<FrameResources, maxFramesInFlight_> frames_ {};
    std::array<PreparedFrameScratch, preparedFrameSlotCount_>
        preparedFrameScratch_ {};
    IsoScenePreparer scenePreparer_;
    FrameDescriptorSync descriptorSync_ { maxFramesInFlight_ };
    uint32_t currentFrame_ = 0;
    uint32_t nextPreparedFrameSlot_ = 0;
    uint64_t nextPreparedFrameGeneration_ = 1;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::Msaa8x;
    VkSampleCountFlagBits activeSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    bool wireframeEnabled_ = false;
    float wireframeLineWidth_ = 1.0f;
    RenderStats lastStats_ {};
    uint64_t nextStatsFrameIndex_ = 1;
    uint64_t pipelineRebuilds_ = 0;
    uint64_t swapchainRecreations_ = 0;
    uint64_t swapchainRecreationDeferrals_ = 0;
};

} // namespace sokoban
