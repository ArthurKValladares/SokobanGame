#pragma once

#include "engine/Math.hpp"
#include "engine/PresentationPolicy.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/render/GltfMesh.hpp"
#include "engine/render/FrameDescriptorSync.hpp"
#include "engine/render/FrameResourceTracker.hpp"
#include "engine/render/FrameTimeTelemetry.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/RendererReconfiguration.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/render/ReusableScratchPool.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/VulkanDeviceContext.hpp"
#include "engine/render/VulkanDiagnostics.hpp"
#include "engine/render/VulkanGpuProfiler.hpp"
#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanPipelineCache.hpp"
#include "engine/render/VulkanPipelineFactory.hpp"
#include "engine/render/VulkanSceneRecorder.hpp"
#include "engine/render/VulkanSceneDescriptors.hpp"
#include "engine/render/VulkanShadowPass.hpp"
#include "engine/render/VulkanSsaoPass.hpp"
#include "engine/render/VulkanSwapchainResources.hpp"
#include "engine/render/VulkanThumbnailPass.hpp"
#include "engine/render/VulkanUiResources.hpp"
#include "engine/ui/Ui.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class AssetManifest;
class FontAtlas;

// One render-finished semaphore per swapchain image.
//
// A present carries no fence. The only things that prove the presentation
// engine has finished waiting on a semaphore are that its image came back
// out of vkAcquireNextImageKHR, or that the present queue went idle. Indexing
// these by frame-in-flight rather than by image therefore lets a submission
// re-signal a semaphore that a queued present is still waiting on, whenever
// the swapchain holds more images than there are frames in flight - which is
// the normal case, since the image count is minImageCount + 1.
//
// The set belongs to one swapchain generation and is retired with it, so the
// present-queue wait in destroyCompletedRetirements() is what makes
// destruction safe.
class SwapchainPresentSemaphores {
public:
    SwapchainPresentSemaphores() = default;
    SwapchainPresentSemaphores(VkDevice device, uint32_t imageCount);
    ~SwapchainPresentSemaphores();

    SwapchainPresentSemaphores(SwapchainPresentSemaphores&& other) noexcept;
    SwapchainPresentSemaphores& operator=(
        SwapchainPresentSemaphores&& other) noexcept;
    SwapchainPresentSemaphores(const SwapchainPresentSemaphores&) = delete;
    SwapchainPresentSemaphores& operator=(
        const SwapchainPresentSemaphores&) = delete;

    [[nodiscard]] VkSemaphore forImage(uint32_t imageIndex) const;
    [[nodiscard]] uint32_t size() const
    {
        return static_cast<uint32_t>(semaphores_.size());
    }

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> semaphores_;
};

class VulkanRenderer {
private:
    struct PreparedFrameScratch;

public:
    struct GameViewportDisplay {
        Vec2 position {};
        Vec2 size {};
        bool hovered = false;
        bool focused = false;
    };

    struct PreparedFrame {
        uint32_t levelWidth = 0;
        uint32_t levelHeight = 0;

    private:
        friend class VulkanRenderer;
        std::shared_ptr<const PreparedFrameScratch> scratch;
        uint64_t generation = 0;
    };

    // assetRoot is the staged runtime content directory containing shaders
    // and every manifest-relative asset. The manifest must outlive the
    // renderer.
    VulkanRenderer(
        SDL_Window* window,
        std::filesystem::path assetRoot,
        std::filesystem::path pipelineCachePath,
        const AssetManifest& manifest,
        const FontAtlas& uiFont,
        AntiAliasingMode antiAliasingMode = AntiAliasingMode::Msaa4x,
        int renderScalePercent = 100,
        PresentationPolicy presentationPolicy = {},
        AssetLoadingBudget assetLoadingBudget = {},
        bool parallelScenePreparationEnabled = true,
        bool pointShadowOptimizationsEnabled = true,
        bool recorderScratchReuseEnabled = true);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    [[nodiscard]] PreparedFrame prepareFrame(
        RenderFrameData frameData,
        std::optional<RenderFrameData> previewFrameData = std::nullopt);
    void drawFrame(
        const PreparedFrame& frame,
        const UiDrawData& uiDrawData,
        bool developerWorkspaceVisible = false);
    // Queues missing content and returns immediately. Ready assets are
    // published incrementally during drawFrame().
    void preloadAssets(const RenderAssetRequirements& requirements);
    // Cancels only prefetch jobs that have not started. Visible work and
    // already-running preparation are preserved.
    void cancelQueuedAssetPrefetches();
    void ensureAssets(const RenderAssetRequirements& requirements);
    // Deliberately blocking path for offline tools; never use for gameplay.
    void waitForAssets(const RenderAssetRequirements& requirements);
    void handleEvent(const SDL_Event& event);
    void beginDebugUiFrame();
    [[nodiscard]] bool wantsKeyboardCapture() const;
    [[nodiscard]] bool wantsMouseCapture() const;
    [[nodiscard]] std::optional<GridPosition3> pickIsoGridCell(
        const PreparedFrame& frame,
        Vec2 pixelPosition) const;
    // Continuous world-tile position on splattable ground, for brush painting.
    // Z is the surface's world height, so overlays can sit on it.
    [[nodiscard]] std::optional<Vec3> pickIsoGroundPoint(
        const PreparedFrame& frame,
        Vec2 pixelPosition) const;
    // Selects Debug-authored decorations using their transformed mesh bounds.
    // The frame tags only editor decoration models, so gameplay models are
    // never accidentally selectable through this path.
    [[nodiscard]] std::optional<std::size_t> pickDecoration(
        const PreparedFrame& frame,
        Vec2 pixelPosition) const;
    // Uploads freshly painted pixels over a resident texture.
    bool updateTexture(RenderTexture texture, const ImageData& image);
    // Picks up textures appended to the manifest after startup.
    void syncManifestTextures();
    [[nodiscard]] uint32_t textureDescriptorCapacity() const;
    // Picks up static models appended by Debug decoration authoring.
    void syncManifestModels();
    // Reads the last drawn frame back as RGBA. The whole render extent when
    // `region` is empty, otherwise that rectangle of it. Blocking; used by the
    // offline thumbnail bake, which needs the real render rather than a
    // re-creation of it.
    [[nodiscard]] ImageData captureRenderedFrame(
        std::optional<VkRect2D> region = std::nullopt);
    [[nodiscard]] VkExtent2D renderExtent() const;
    // ImGui descriptor for the latest completed game render. Zero in builds
    // without the developer workspace.
    [[nodiscard]] uint64_t gameViewportTexture() const;
    void setGameViewportDisplay(
        std::optional<GameViewportDisplay> display);
    [[nodiscard]] bool hasGameViewportDisplay() const;
    [[nodiscard]] Vec2 mapPointerToGameViewport(
        Vec2 pointer,
        Vec2 gameUiExtent) const;
    // Rendered preview of a tile type for the editor palette, or nullptr when
    // thumbnails are unavailable or the tile has no model of its own (the
    // caller should fall back to a swatch).
    [[nodiscard]] VkDescriptorSet tileThumbnail(TileType tile);
    // Drops loaded thumbnails so a re-bake is picked up without restarting.
    void invalidateTileThumbnails();
    // World point -> pixel position, using the frame's own camera. Lets the
    // debug UI draw overlays that sit correctly on the 3D board (the brush
    // preview ring) without duplicating the projection.
    [[nodiscard]] std::optional<Vec2> projectToPixels(
        const PreparedFrame& frame,
        Vec3 worldPoint) const;
    // Pixel bounds of the primary player tile as it appears in this prepared
    // frame, including its presentation position, scale and current camera.
    [[nodiscard]] std::optional<UiRect> primaryPlayerBoundsToPixels(
        const PreparedFrame& frame) const;
    void waitIdle() const;
    [[nodiscard]] bool hasFatalFailure() const {
        return fatalFailure_.has_value();
    }
    [[nodiscard]] std::string_view fatalFailureMessage() const;
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
    void setPresentationPolicy(PresentationPolicy policy);
    [[nodiscard]] bool wireframeEnabled() const;
    void setWireframeEnabled(bool enabled);
    [[nodiscard]] bool wireframeSupported() const;
    // Developer toggle for model back-face culling. Dynamic state, so it takes
    // effect on the next recorded frame with no pipeline rebuild.
    [[nodiscard]] bool modelBackfaceCullingEnabled() const;
    void setModelBackfaceCullingEnabled(bool enabled);
    // Developer toggle for the opaque face order. CPU-side, so it takes
    // effect on the next prepared frame with no pipeline rebuild.
    [[nodiscard]] bool opaqueFrontToBackSortEnabled() const;
    void setOpaqueFrontToBackSortEnabled(bool enabled);
    // Main color/depth frustum culling. The toggle exists for matched evidence
    // and developer A/B inspection; normal play leaves it enabled.
    [[nodiscard]] bool frustumCullingEnabled() const;
    void setFrustumCullingEnabled(bool enabled);
    [[nodiscard]] bool wideLinesSupported() const;
    [[nodiscard]] float wireframeLineWidth() const;
    [[nodiscard]] std::array<float, 2> wireframeLineWidthRange() const;
    void setWireframeLineWidth(float lineWidth);
    // Debug: applies an arbitrary clip to one selected skinned model
    // (nullptr restores ordinary frame animation). The clip must stay alive
    // while set; call every frame with the current preview time.
    void setAnimationPreview(
        RenderModel model,
        const GltfAnimationClip* clip,
        float timeSeconds);

private:
    struct FrameResources {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };
    // renderFinished deliberately does not live here: it is indexed by
    // swapchain image, not by frame slot. See SwapchainPresentSemaphores.

    struct PreparedFrameScratch {
        RenderFrameData frameData;
        PreparedRenderScene scene;
        std::optional<RenderFrameData> previewFrameData;
        std::optional<PreparedRenderScene> previewScene;
        uint64_t generation = 0;
    };

    struct RenderResourceSet {
        std::unique_ptr<VulkanSwapchainResources> swapchain;
        // Sized by, and retired with, `swapchain`.
        SwapchainPresentSemaphores presentSemaphores;
        std::unique_ptr<VulkanSsaoPass> ssaoPass;
        std::unique_ptr<VulkanSceneDescriptors> sceneDescriptors;
        std::unique_ptr<VulkanPipelineFactory> pipelines;
        VkDescriptorSet gameViewportTexture = VK_NULL_HANDLE;
    };

    struct RetiredRenderResources {
        RenderResourceSet resources;
        uint32_t pendingFrameMask = 0;
    };

    [[nodiscard]] const PreparedFrameScratch& resolvePreparedFrame(
        const PreparedFrame& frame) const;
    [[nodiscard]] VulkanSceneDescriptors::Resources descriptorResources(
        const RenderResourceSet& resources) const;
    [[nodiscard]] RenderResourceSet createRenderResources(
        const RendererSettingsSnapshot& settings);
    [[nodiscard]] std::unique_ptr<VulkanPipelineFactory> createPipelines(
        const RenderResourceSet& resources,
        const RendererSettingsSnapshot& settings);
    void applyPendingReconfiguration();
    void completeFrame(uint32_t frameIndex);
    void retireResources(
        RenderResourceSet resources,
        uint32_t pendingFrameMask);
    void destroyCompletedRetirements();
    [[nodiscard]] uint32_t pendingFrameMask() const;
    [[nodiscard]] uint32_t pendingFrameMaskForGeneration(
        uint64_t generation) const;
    void createFrameResources();
    void initializeDebugUi();
    void shutdownDebugUi();
    void registerGameViewportTexture(RenderResourceSet& resources);
    void releaseGameViewportTexture(RenderResourceSet& resources);
    void logRenderConfiguration() const;
    void reportFatalFailure(VulkanFailure failure) noexcept;
    [[nodiscard]] VkSampleCountFlagBits sampleCountForMode(AntiAliasingMode mode) const;
    [[nodiscard]] uint32_t sampleCountValue() const;

    SDL_Window* window_ = nullptr;
    std::filesystem::path assetRoot_;
    RuntimeTextureCatalog runtimeTextureCatalog_;
    VulkanDeviceContext deviceContext_;
    VulkanPipelineCache pipelineCache_;
    VulkanGpuProfiler gpuProfiler_;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkFormat shadowFormat_ = VK_FORMAT_D32_SFLOAT;
    VulkanShadowPass shadowPass_;
    VulkanThumbnailPass thumbnailPass_;
    VulkanUiResources uiResources_;

    VulkanModelResources modelResources_;
    VulkanSceneRecorder sceneRecorder_;
    RendererReconfigurationQueue reconfigurationQueue_;
    RenderResourceSet activeResources_;
    std::vector<RetiredRenderResources> retiredResources_;

    static constexpr uint32_t maxFramesInFlight_ = 2;
    static constexpr uint32_t preparedFrameSlotCount_ = 2;
    std::array<FrameResources, maxFramesInFlight_> frames_ {};
    FrameResourceTracker frameResourceTracker_ {
        maxFramesInFlight_ };
    ReusableScratchPool<PreparedFrameScratch, preparedFrameSlotCount_>
        preparedFrameScratch_;
    RenderAssetRequirements frameAssetRequirements_;
    // Kept separate from asset loading: frame preparation waits for this
    // worker every frame and must not sit behind texture/model jobs.
    TaskSystem framePreparationTasks_ { 1 };
    IsoScenePreparer scenePreparer_;
    IsoScenePreparer previewScenePreparer_;
    FrameDescriptorSync descriptorSync_ { maxFramesInFlight_ };
    uint32_t currentFrame_ = 0;
    uint64_t nextPreparedFrameGeneration_ = 1;
    VkSampleCountFlagBits activeSampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    float wireframeLineWidth_ = 1.0f;
    bool modelBackfaceCulling_ = true;
    uint64_t activeResourceGeneration_ = 1;
    bool swapchainRecreationRequested_ = false;
    std::optional<VulkanFailure> fatalFailure_;
    PresentationPolicy presentationPolicy_ {};
    RenderStats lastStats_ {};
    FrameTimeTelemetry scenePreparationTimeTelemetry_ {};
    FrameTimeTelemetry cpuFrameTimeTelemetry_ {};
    bool parallelScenePreparationEnabled_ = true;
    bool pointShadowOptimizationsEnabled_ = true;
    bool recorderScratchReuseEnabled_ = true;
    uint64_t nextStatsFrameIndex_ = 1;
    std::optional<GameViewportDisplay> gameViewportDisplay_;
    uint64_t pipelineRebuilds_ = 0;
    uint64_t swapchainRecreations_ = 0;
    uint64_t swapchainRecreationDeferrals_ = 0;
    uint64_t renderResourceReconfigurations_ = 0;
    uint64_t presentQueueRetirementWaits_ = 0;
};

} // namespace sokoban
