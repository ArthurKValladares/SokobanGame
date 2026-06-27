#pragma once

#include "engine/Config.hpp"
#include "engine/Math.hpp"
#include "engine/render/GltfMesh.hpp"
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

enum class RenderModel {
    Cube,
    BricksA,
    Stone,
    Water,
    Glass,
    Rogue,
};

enum class RenderAnimation {
    None,
    RogueIdle,
    RogueMovement,
};

struct RenderFrameData {
    struct DirectionalLight {
        Vec3 direction { 0.0f, 0.0f, 1.0f };
        Vec3 color { config::sunColor };
        float intensity = config::sunIntensity;
    };

    struct AmbientLight {
        Vec3 color { config::ambientLightColor };
        float intensity = config::ambientLightIntensity;
    };

    struct Lighting {
        struct Shadows {
            bool enabled = config::shadowsEnabled;
            float opacity = config::shadowOpacity;
            float bias = config::shadowBias;
        };

        DirectionalLight sun {};
        AmbientLight ambient {};
        Shadows shadows {};
        float specularStrength = config::specularStrength;
        float specularPower = config::specularPower;
        float modelShadowReceive = config::modelShadowReceive;
    };

    struct Tile {
        GridPosition3 cell {};
        Vec2 position {};
        Vec2 size { 1.0f, 1.0f };
        Vec4 color {};
        float baseElevation = 0.0f;
        float height = 0.0f;
        bool blurBehind = false;
        bool pickOnly = false;
        bool showGrid = true;
        bool isEditorPreview = false;
        RenderModel model = RenderModel::Cube;
        RenderAnimation animation = RenderAnimation::None;
        float animationTimeSeconds = 0.0f;
        uint32_t modelRotationQuarterTurns = 0;
    };

    struct IsoFace {
        std::array<Vec3, 4> vertices {};
        Vec3 normal {};
        Vec4 color {};
    };

    struct GridOverlay {
        Vec4 color { config::tileGridLineColor };
        float width = config::tileGridLineWidth;
    };

    RenderViewMode viewMode = RenderViewMode::TopDown2D;
    Lighting lighting {};
    GridOverlay gridOverlay {};
    uint32_t levelWidth = 0;
    uint32_t levelHeight = 0;
    uint32_t levelDepth = 1;
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
    [[nodiscard]] std::optional<GridPosition3> pickIsoGridCell(const RenderFrameData& frameData, Vec2 pixelPosition) const;
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

    struct OwnedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct GpuMesh {
        OwnedBuffer vertexBuffer {};
        OwnedBuffer indexBuffer {};
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
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
    void createSwapchain();
    void createImageViews();
    void createMsaaColorResources();
    void createDepthResources();
    void createShadowResources();
    void createSceneColorResources();
    void createDescriptorResources();
    void updateDescriptorSet();
    void createCommandPool();
    void createModelResources();
    void destroyModelResources();
    void createModelTextureResources();
    void destroyModelTextureResources();
    [[nodiscard]] GpuMesh uploadMesh(const MeshData& mesh) const;
    void updateMeshVertices(const GpuMesh& gpuMesh, const std::vector<MeshVertex>& vertices) const;
    void updateAnimatedModelMeshes(const RenderFrameData& frameData);
    void destroyMesh(GpuMesh& mesh) const;
    [[nodiscard]] const GpuMesh& meshForModel(RenderModel model) const;
    void createPipeline();
    void createShadowPipeline(VkShaderModule shadowVertexShader);
    void createModelShadowPipeline(VkShaderModule shadowVertexShader);
    void destroyPipeline();
    void destroyDescriptorResources();
    void cleanupShadowResources();
    void cleanupSceneColorResources();
    void createFrameResources();
    void initializeDebugUi();
    void shutdownDebugUi();
    void renderDebugUi(VkCommandBuffer commandBuffer) const;
    void recreateSwapchain();
    void cleanupSwapchain();
    void cleanupMsaaColorResources();
    void cleanupDepthResources();

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
    void copyResolvedSceneColor(VkCommandBuffer commandBuffer, VkImage resolvedColorImage);
    void recordUiRendering(VkCommandBuffer commandBuffer, VkImageView colorView, const UiDrawData& uiDrawData);
    void recordDebugUiRendering(VkCommandBuffer commandBuffer, VkImageView colorView) const;
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
    [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    [[nodiscard]] VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    [[nodiscard]] VkShaderModule createShaderModule(const std::filesystem::path& path) const;
    [[nodiscard]] VkPipeline createGraphicsPipeline(VkShaderModule vertexShader, VkShaderModule fragmentShader) const;
    [[nodiscard]] VkPipeline createModelGraphicsPipeline(VkShaderModule vertexShader, VkShaderModule fragmentShader) const;
    [[nodiscard]] std::array<VkPipeline, 2> createGraphicsPipelineLibraries(VkShaderModule vertexShader, VkShaderModule fragmentShader) const;
    [[nodiscard]] VkSampleCountFlagBits sampleCountForMode(AntiAliasingMode mode) const;
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    [[nodiscard]] OwnedBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) const;
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
    VkFormat shadowFormat_ = VK_FORMAT_D32_SFLOAT;
    VkExtent2D swapchainExtent_ {};
    std::vector<SwapchainImage> swapchainImages_;
    OwnedImage msaaColorImage_ {};
    OwnedImage depthImage_ {};
    OwnedImage shadowImage_ {};
    OwnedImage sceneColorImage_ {};
    VkImageLayout shadowImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout sceneColorImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;
    VkSampler sceneColorSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> pipelineLibraries_ {};
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline modelPipeline_ = VK_NULL_HANDLE;
    VkPipeline modelShadowPipeline_ = VK_NULL_HANDLE;
    GpuMesh bricksAMesh_ {};
    GpuMesh stoneMesh_ {};
    GpuMesh waterMesh_ {};
    GpuMesh glassMesh_ {};
    GpuMesh rogueMesh_ {};
    SkinnedMeshData rogueSkinnedMesh_ {};
    GltfAnimationClip rogueIdleAnimation_ {};
    GltfAnimationClip rogueMovementAnimation_ {};
    RenderAnimation activeRogueAnimation_ = RenderAnimation::None;
    float activeRogueAnimationTime_ = -1.0f;
    OwnedImage rogueTextureImage_ {};
    VkSampler rogueTextureSampler_ = VK_NULL_HANDLE;

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
