#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/render/AnimationController.hpp"
#include "engine/render/AssetLoadScheduler.hpp"
#include "engine/render/GpuSkinning.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/MaterialRenderPolicy.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanGeometryArena.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanUploadRing.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sokoban {

// Owns lazy CPU preparation and render-thread Vulkan publication for model
// meshes, textures, and animation clips. Worker tasks never touch Vulkan.
class VulkanModelResources {
public:
    struct MeshView {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexOffset = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t firstInstance = 0;
        bool skinned = false;
    };

    struct SkinningBufferView {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize range = 0;

        [[nodiscard]] bool valid() const { return buffer && range > 0; }
    };

    struct DrawInstanceBufferView {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize range = 0;

        [[nodiscard]] bool valid() const { return buffer && range > 0; }
    };

    struct MaterialBufferView {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize range = 0;

        [[nodiscard]] bool valid() const { return buffer && range > 0; }
    };

    struct TextureView {
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        [[nodiscard]] bool valid() const { return imageView && sampler; }
    };

    struct MaterialBinding {
        ModelMaterialMode mode = ModelMaterialMode::Untextured;
        uint32_t textureIndex = 0;
        // Where this model's materials start in the material buffer. Zero
        // until the model has published; entry zero is reserved as the
        // untextured fallback and no real range is ever handed it, so a draw
        // recorded a frame early reads white rather than another model.
        uint32_t materialBase = 0;
        ModelMaterialPolicy policy {};
    };

    // Axis-aligned extent of a model's mesh in its own space. Needed to frame
    // a model on its own (palette thumbnails); the scene never needs it,
    // because tiles are drawn at a known grid size.
    struct ModelBounds {
        Vec3 minimum {};
        Vec3 maximum {};
        bool valid = false;
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
        uint32_t requestedAssets = 0;
        uint32_t readyRequestedAssets = 0;
        uint32_t queuedAssets = 0;
        uint32_t activeCpuJobs = 0;
        uint64_t cancelledPrefetches = 0;
        uint64_t modelResidencyBytes = 0;
        uint64_t textureResidencyBytes = 0;
        uint64_t modelResidencyPeakBytes = 0;
        uint64_t textureResidencyPeakBytes = 0;
        uint64_t modelResidencyBudgetBytes = 0;
        uint64_t textureResidencyBudgetBytes = 0;
        uint64_t residencyEvictions = 0;
        uint64_t residencyBudgetBlocks = 0;
        bool residencyBudgetBlocked = false;
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
    // maxSamplerAnisotropy is the device's limit when the samplerAnisotropy
    // feature was enabled, and 1.0 otherwise. Passing a value above 1.0
    // without that feature enabled is a validation error, so this must come
    // from VulkanDeviceContext rather than be re-derived from device limits.
    void create(
        VkPhysicalDevice physicalDevice,
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::filesystem::path assetRoot,
        const AssetManifest& manifest,
        const RuntimeTextureCatalog& textureCatalog,
        uint32_t textureDescriptorCapacity,
        float maxSamplerAnisotropy = 1.0f);
    void destroy();

    // Queues independent CPU work and returns immediately. Visible requests
    // always preempt speculative prefetches when a worker slot opens.
    void requestAssets(
        const RenderAssetRequirements& requirements,
        AssetLoadPriority priority = AssetLoadPriority::Visible);
    // Drops prefetches that have not started, such as those for a screen the
    // player just left. Active filesystem work is allowed to finish safely.
    void cancelQueuedPrefetches();
    // Explicitly blocking path for offline tooling such as thumbnail baking.
    // Normal gameplay must use requestAssets() and publishReadyAssets().
    // Texture uploads are queued but never waited here. Returns true when
    // frame-local texture descriptors must be refreshed.
    [[nodiscard]] bool waitForAssets(const RenderAssetRequirements& requirements);

    // Grows the per-texture slots to match a manifest that gained entries at
    // runtime (the level editor creating a splat map). Returns true when it
    // grew. New slots start unrequested and load from disk on the next
    // ensureAssets; existing slots and their ids are untouched, because ids
    // are indices and the manifest only ever appends.
    bool syncManifestTextures();
    // Grows static model slots after the Debug decoration authoring path
    // appends manifest models. Existing slots and ids remain untouched.
    bool syncManifestModels();

    struct TextureUpdate {
        bool updated = false;
        // True when the image had to be recreated (a size change), which
        // invalidates the view every descriptor set points at.
        bool descriptorsChanged = false;
    };
    // Replaces a published texture's pixels, for maps painted in the level
    // editor. Same-size updates write into the existing image and leave
    // descriptors valid; a size change - the board was resized - recreates the
    // image and requires the caller to refresh descriptors. Returns
    // `updated = false` when the texture is not resident.
    TextureUpdate updateTexture(RenderTexture texture, const ImageData& image);
    // Publishes up to maxPublications completed background tasks without
    // waiting. Failed resources stay observable in LoadingStats while frames
    // continue using available content and fallback textures.
    [[nodiscard]] bool publishReadyAssets(std::size_t maxPublications);
    // Reclaims upload command buffers and staging resources whose GPU fences
    // have signaled. This never waits for GPU work.
    void retireCompletedUploads();

    void setAnimationPreview(
        RenderModel model,
        const GltfAnimationClip* clip,
        float timeSeconds);
    void updateAnimations(const RenderFrameData& frameData, uint32_t frameIndex);
    // Called after this frame index's fence is complete and before its main
    // and preview animation requests are populated.
    void beginAnimationFrame(uint32_t frameIndex);
    // Writes one static-model transform into the fence-owned region for the
    // current frame and returns the absolute gl_InstanceIndex for a draw.
    [[nodiscard]] uint32_t writeDrawInstance(
        uint32_t frameIndex,
        const GpuDrawInstance& instance);

    [[nodiscard]] MeshView meshForTile(
        const RenderFrameData::Tile& tile,
        uint32_t frameIndex) const;
    [[nodiscard]] MaterialBinding materialForModel(RenderModel model) const;
    // Invalid until the model has finished uploading.
    [[nodiscard]] ModelBounds boundsForModel(RenderModel model) const;
    // True once the model's mesh is resident. Skinned instances additionally
    // need tileReadyForDraw(), because their pose is published per frame.
    [[nodiscard]] bool modelReady(RenderModel model) const;
    [[nodiscard]] bool modelUsesGpuSkinning(RenderModel model) const;
    // Static models are ready with resident geometry. Every skinned model,
    // regardless of its gameplay role, also requires this instance's pose for
    // the current frame before any colour or shadow pass may draw it.
    [[nodiscard]] bool tileReadyForDraw(
        const RenderFrameData::Tile& tile,
        uint32_t frameIndex) const;
    [[nodiscard]] const AssetManifest& manifest() const { return *manifest_; }
    [[nodiscard]] std::vector<TextureView> textures() const;
    [[nodiscard]] uint32_t textureCount() const;
    [[nodiscard]] LoadingStats loadingStats() const;
    [[nodiscard]] SkinningBufferView skinningBuffer() const;
    [[nodiscard]] DrawInstanceBufferView drawInstanceBuffer() const;
    [[nodiscard]] MaterialBufferView materialBuffer() const;

private:
    enum class LoadState {
        Unrequested,
        Queued,
        Loading,
        CpuReady,
        Uploading,
        Ready,
        Failed,
    };

    struct OwnedImage {
        VkImage image = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t mipLevels = 1;
    };

    struct GpuMesh {
        VulkanGeometryArena::Allocation allocation {};
        uint32_t indexCount = 0;
    };

    struct GpuSkinnedMesh {
        VulkanGeometryArena::Allocation allocation {};
        uint32_t indexCount = 0;
    };

    struct SkinningBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        void* mapped = nullptr;
    };

    struct ModelInstanceBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        void* mapped = nullptr;
    };

    struct MaterialBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        void* mapped = nullptr;
    };

    struct TextureResource {
        OwnedImage image {};
        VkSampler sampler = VK_NULL_HANDLE;
        // Kept so an in-place update can refuse a differently sized image,
        // which would need a new image and a descriptor rewrite.
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct PendingTextureUpload {
        VulkanUploadRing::Reservation staging {};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool submitted = false;
    };

    using PreparedModel = std::variant<MeshData, SkinnedMeshData>;

    struct ModelSlot {
        LoadState state = LoadState::Unrequested;
        GpuMesh gpu {};
        GpuSkinnedMesh skinnedGpu {};
        std::future<PreparedModel> future;
        std::optional<PreparedModel> prepared;
        std::shared_ptr<const SkinnedMeshData> skinnedSource;
        std::exception_ptr failure;
        // Captured at upload, because the CPU mesh is released immediately
        // afterwards and nothing else keeps it.
        ModelBounds bounds {};
        // Where this model's materials sit in the shared material buffer.
        // Zero means unpublished: the range never starts at the reserved
        // fallback entry. The recorder reads the base off this slot for every
        // draw, so a repack is free to move the range as long as no frame is
        // in flight.
        uint32_t materialBase = 0;
        uint32_t materialCount = 0;
        ModelMaterialPolicy materialPolicy {};
        uint64_t lastRequested = 0;
        uint64_t gpuBytes = 0;
        VulkanGeometryArena::Upload upload {};
    };

    struct TextureSlot {
        LoadState state = LoadState::Unrequested;
        TextureResource gpu {};
        PendingTextureUpload upload {};
        std::future<ImageData> future;
        std::optional<ImageData> prepared;
        std::exception_ptr failure;
        uint64_t lastRequested = 0;
        uint64_t gpuBytes = 0;
    };

    struct AnimationSlot {
        LoadState state = LoadState::Unrequested;
        std::future<GltfAnimationClip> future;
        std::exception_ptr failure;
        uint64_t lastRequested = 0;
    };

    void queueModel(RenderModel model, AssetLoadPriority priority);
    void queueTexture(std::size_t textureIndex, AssetLoadPriority priority);
    void queueAnimation(RenderAnimation animation, AssetLoadPriority priority);
    void queueModelDependencies(RenderModel model, AssetLoadPriority priority);
    void startQueuedAssets();
    void startAsset(AssetLoadKey key);
    void startModel(RenderModel model);
    void startTexture(std::size_t textureIndex);
    [[nodiscard]] std::filesystem::path textureDiagnosticPath(
        std::size_t textureIndex) const;
    void startAnimation(RenderAnimation animation);
    void resetCancelledAsset(AssetLoadKey key);
    void completeCpuJob(AssetLoadKey key);
    void retireCompletedGeometryUploads(bool wait);
    [[nodiscard]] bool makeModelResident(
        RenderModel protectedModel,
        uint64_t requiredBytes);
    [[nodiscard]] bool makeTextureResident(
        std::size_t protectedTexture,
        uint64_t requiredBytes);
    void markResidencyBudgetBlocked();
    [[nodiscard]] static uint64_t meshBytes(const MeshData& mesh);
    [[nodiscard]] static uint64_t textureBytes(
        const ImageData& image,
        const TextureInterpretation& interpretation);

    [[nodiscard]] bool publishModel(RenderModel model, bool wait);
    [[nodiscard]] bool publishTexture(std::size_t textureIndex, bool wait);
    [[nodiscard]] bool publishAnimation(RenderAnimation animation, bool wait);
    void throwIfFailed(
        LoadState state,
        const std::exception_ptr& failure,
        const std::filesystem::path& path,
        const char* kind) const;

    [[nodiscard]] std::vector<bool> requiredTextures(const RenderAssetRequirements& requirements) const;
    [[nodiscard]] bool assetsReady(const RenderAssetRequirements& requirements) const;

    [[nodiscard]] static ModelBounds boundsOf(
        const std::vector<MeshVertex>& vertices);
    [[nodiscard]] GpuMesh uploadMesh(
        const MeshData& mesh,
        VulkanGeometryArena::Upload& upload);
    [[nodiscard]] GpuSkinnedMesh uploadSkinnedMesh(
        const SkinnedMeshData& mesh,
        VulkanGeometryArena::Upload& upload);
    void createSkinningBuffer();
    void destroySkinningBuffer();
    void createModelInstanceBuffer();
    void destroyModelInstanceBuffer();
    void createMaterialBuffer();
    void destroyMaterialBuffer();
    // Appends a model's materials and returns the base index of the range.
    // Throws when the buffer is full, which fails that one model's
    // publication rather than the frame.
    [[nodiscard]] uint32_t writeMaterials(
        const std::vector<MeshMaterial>& materials);
    // Closes the gaps left by evicted models. Only safe while the device is
    // idle, which is exactly where residency eviction already is.
    void repackMaterials();
    void writeSkinningInstance(
        uint32_t frameIndex,
        uint32_t instanceSlot,
        const SkinnedMeshData& mesh,
        const AnimationController::SkinningRequest& request);
    void createTextureBlocking(
        const ImageData& image,
        OwnedImage& gpuImage,
        VkSampler& sampler,
        TextureInterpretation sampling);
    void beginTextureUpload(
        const ImageData& image,
        OwnedImage& gpuImage,
        VkSampler& sampler,
        PendingTextureUpload& upload,
        TextureInterpretation sampling);
    // Stages `image` and records the copy into an image that already exists,
    // leaving it shader-readable. Shared by first upload and repaint.
    void recordTextureCopy(
        const ImageData& image,
        OwnedImage& gpuImage,
        PendingTextureUpload& upload);
    void destroyTextureUpload(PendingTextureUpload& upload);
    void destroyTexture(OwnedImage& image, VkSampler& sampler);
    void destroyMesh(GpuMesh& mesh);
    void destroySkinnedMesh(GpuSkinnedMesh& mesh);
    [[nodiscard]] const GpuMesh& gpuMeshForModel(RenderModel model) const;
    [[nodiscard]] VkImageView createImageView(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectMask,
        uint32_t mipLevels) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    float maxSamplerAnisotropy_ = 1.0f;
    uint32_t textureDescriptorCapacity_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::filesystem::path assetRoot_;
    const AssetManifest* manifest_ = nullptr;

    std::vector<ModelSlot> models_; // indexed by RenderModel::index()
    std::vector<AnimationSlot> animations_; // indexed by RenderAnimation::index()
    // Descriptor-indexed. Manifest/editor definitions occupy low indices;
    // discovered glTF definitions occupy stable high indices.
    std::vector<std::optional<RuntimeTextureDefinition>> textureDefinitions_;
    std::vector<uint32_t> activeTextureIndices_;
    std::vector<std::vector<uint32_t>> modelTextureDependencies_;
    std::vector<std::vector<PrimitiveMaterialBinding>> modelMaterialBindings_;
    std::vector<TextureSlot> textures_;
    uint32_t manifestTextureCount_ = 0;
    uint32_t discoveredTextureBase_ = 0;
    TextureResource fallbackTexture_ {};
    VulkanUploadRing uploadRing_ {};
    VulkanGeometryArena geometryArena_ {};
    SkinningBuffer skinningBuffer_ {};
    ModelInstanceBuffer drawInstanceBuffer_ {};
    MaterialBuffer materialBuffer_ {};
    // Mirrors the mapped buffer so a repack can move ranges without reading
    // back through host-visible device memory.
    std::vector<GpuMaterial> materialStorage_;
    AnimationController animationController_ {};
    struct AnimatedMeshKey {
        uint32_t frameIndex = 0;
        uint64_t instanceId = 0;
        uint32_t modelValue = 0;

        bool operator==(const AnimatedMeshKey&) const = default;
    };
    struct AnimatedMeshKeyHash {
        std::size_t operator()(AnimatedMeshKey key) const
        {
            return std::hash<uint64_t> {}(
                key.instanceId ^
                (static_cast<uint64_t>(key.frameIndex) << 56) ^
                (static_cast<uint64_t>(key.modelValue) << 24));
        }
    };
    std::unordered_map<AnimatedMeshKey, uint32_t, AnimatedMeshKeyHash>
        skinnedInstances_;
    uint32_t activeSkinningFrame_ = UINT32_MAX;
    uint32_t skinningInstanceCount_ = 0;
    uint32_t drawInstanceCount_ = 0;
    uint64_t textureUploadSubmissions_ = 0;
    uint64_t textureUploadCompletions_ = 0;
    AssetLoadScheduler scheduler_ {};
    uint64_t visibleRequestStamp_ = 0;
    uint64_t modelResidencyBytes_ = 0;
    uint64_t textureResidencyBytes_ = 0;
    uint64_t modelResidencyPeakBytes_ = 0;
    uint64_t textureResidencyPeakBytes_ = 0;
    uint64_t residencyEvictions_ = 0;
    uint64_t residencyBudgetBlocks_ = 0;
    bool residencyBudgetBlocked_ = false;
    bool textureDescriptorsDirty_ = false;
};

} // namespace sokoban
