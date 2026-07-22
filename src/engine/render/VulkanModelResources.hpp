#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/SkinnedMeshUpdater.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <optional>
#include <variant>
#include <vector>

namespace sokoban {

// Owns lazy CPU preparation and render-thread Vulkan publication for model
// meshes, textures, and animation clips. Worker tasks never touch Vulkan.
class VulkanModelResources {
public:
    using MeshView = SkinnedMeshUpdater::MeshView;

    struct TextureView {
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        [[nodiscard]] bool valid() const { return imageView && sampler; }
    };

    struct MaterialBinding {
        ModelMaterialMode mode = ModelMaterialMode::Untextured;
        uint32_t textureIndex = 0;
    };

    struct LoadingStats {
        uint32_t loadedModels = 0;
        uint32_t pendingModels = 0;
        uint32_t totalModels = 0;
        uint32_t loadedTextures = 0;
        uint32_t pendingTextures = 0;
        uint32_t totalTextures = 0;
        uint32_t loadedAnimations = 0;
        uint32_t pendingAnimations = 0;
        uint32_t totalAnimations = 0;
        uint32_t failedAssets = 0;
        uint32_t uploadingTextures = 0;
        uint64_t textureUploadSubmissions = 0;
        uint64_t textureUploadCompletions = 0;
    };

    VulkanModelResources() = default;
    ~VulkanModelResources();

    VulkanModelResources(const VulkanModelResources&) = delete;
    VulkanModelResources& operator=(const VulkanModelResources&) = delete;

    // The manifest must outlive this object; it defines every model,
    // texture, and animation slot.
    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::filesystem::path assetRoot,
        const AssetManifest& manifest);
    void destroy();

    // Starts independent CPU tasks and returns immediately.
    void requestAssets(const RenderAssetRequirements& requirements);
    // Publishes every required asset, waiting for CPU tasks when necessary.
    // Texture uploads are queued but never waited here. Returns true when
    // frame-local texture descriptors must be refreshed.
    [[nodiscard]] bool ensureAssets(const RenderAssetRequirements& requirements);
    // Publishes up to maxPublications completed background tasks without
    // waiting. Failed preloads are retained and rethrown if later required.
    [[nodiscard]] bool publishReadyAssets(std::size_t maxPublications);
    // Reclaims upload command buffers and staging resources whose GPU fences
    // have signaled. This never waits for GPU work.
    void retireCompletedUploads();

    void setAnimationPreview(const GltfAnimationClip* clip, float timeSeconds);
    void updateAnimations(const RenderFrameData& frameData);

    [[nodiscard]] MeshView meshForModel(RenderModel model) const;
    [[nodiscard]] MaterialBinding materialForModel(RenderModel model) const;
    [[nodiscard]] std::vector<TextureView> textures() const;
    [[nodiscard]] uint32_t textureCount() const;
    [[nodiscard]] LoadingStats loadingStats() const;

private:
    enum class LoadState {
        Unrequested,
        Loading,
        CpuReady,
        Uploading,
        Ready,
        Failed,
    };

    struct OwnedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct OwnedImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct GpuMesh {
        OwnedBuffer vertexBuffer {};
        OwnedBuffer indexBuffer {};
        uint32_t indexCount = 0;
    };

    struct TextureResource {
        OwnedImage image {};
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct PendingTextureUpload {
        OwnedBuffer staging {};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool submitted = false;
    };

    using PreparedModel = std::variant<MeshData, SkinnedMeshData>;

    struct ModelSlot {
        LoadState state = LoadState::Unrequested;
        GpuMesh gpu {};
        std::future<PreparedModel> future;
        std::optional<PreparedModel> prepared;
        std::exception_ptr failure;
    };

    struct TextureSlot {
        LoadState state = LoadState::Unrequested;
        TextureResource gpu {};
        PendingTextureUpload upload {};
        std::future<ImageData> future;
        std::exception_ptr failure;
    };

    struct AnimationSlot {
        LoadState state = LoadState::Unrequested;
        std::future<GltfAnimationClip> future;
        std::exception_ptr failure;
    };

    void requestModel(RenderModel model);
    void requestTexture(std::size_t textureIndex);
    void requestAnimation(RenderAnimation animation);
    void requestModelDependencies(RenderModel model);

    [[nodiscard]] bool publishModel(RenderModel model, bool wait);
    [[nodiscard]] bool publishTexture(std::size_t textureIndex, bool wait);
    [[nodiscard]] bool publishAnimation(RenderAnimation animation, bool wait);
    void finalizeSkinnedMeshIfReady();
    void throwIfFailed(
        LoadState state,
        const std::exception_ptr& failure,
        const std::filesystem::path& path,
        const char* kind) const;

    [[nodiscard]] std::vector<bool> requiredTextures(const RenderAssetRequirements& requirements) const;
    [[nodiscard]] bool assetsReady(const RenderAssetRequirements& requirements) const;

    [[nodiscard]] GpuMesh uploadMesh(const MeshData& mesh) const;
    void createTextureBlocking(
        const ImageData& image,
        OwnedImage& gpuImage,
        VkSampler& sampler);
    void beginTextureUpload(
        const ImageData& image,
        OwnedImage& gpuImage,
        VkSampler& sampler,
        PendingTextureUpload& upload);
    void destroyTextureUpload(PendingTextureUpload& upload) const;
    void destroyTexture(OwnedImage& image, VkSampler& sampler);
    void destroyMesh(GpuMesh& mesh) const;
    [[nodiscard]] const GpuMesh& gpuMeshForModel(RenderModel model) const;
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    [[nodiscard]] OwnedBuffer createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties) const;
    [[nodiscard]] VkImageView createImageView(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectMask) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::filesystem::path assetRoot_;
    const AssetManifest* manifest_ = nullptr;

    std::vector<ModelSlot> models_; // indexed by RenderModel::index()
    std::vector<AnimationSlot> animations_; // indexed by RenderAnimation::index()
    std::vector<TextureSlot> textures_;
    TextureResource fallbackTexture_ {};
    AnimationController animationController_ {};
    SkinnedMeshUpdater skinnedMeshUpdater_ {};
    uint64_t textureUploadSubmissions_ = 0;
    uint64_t textureUploadCompletions_ = 0;
};

} // namespace sokoban
