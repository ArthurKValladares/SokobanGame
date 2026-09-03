#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/Geometry.hpp"
#include "engine/render/AnimationController.hpp"
#include "engine/render/AssetLoadScheduler.hpp"
#include "engine/render/FrameRetirementQueue.hpp"
#include "engine/render/GpuMappedBuffer.hpp"
#include "engine/render/GpuSkinning.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/MaterialRangeAllocator.hpp"
#include "engine/render/MaterialRenderPolicy.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/ResidencyBudget.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/TextureDescriptorSpace.hpp"
#include "engine/render/TextureSourceLoader.hpp"
#include "engine/render/TextureMipResidency.hpp"
#include "engine/render/VulkanRenderConstants.hpp"
#include "engine/render/VulkanGeometryArena.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanTextureUploader.hpp"
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

    // One shape, three jobs. The names are kept because a signature saying
    // MaterialBufferView says which binding it feeds, which GpuBufferView on
    // its own would not - but they are the same type, so a caller can no longer
    // be handed the wrong one and have it compile.
    using SkinningBufferView = GpuBufferView;
    using DrawInstanceBufferView = GpuBufferView;
    using MaterialBufferView = GpuBufferView;

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
        // Draws and skinned poses a frame asked for beyond what the buffers
        // hold. Both used to terminate the process; they are dropped now, so
        // these are the only sign it happened.
        uint64_t droppedDrawInstances = 0;
        uint64_t droppedSkinningInstances = 0;
        // Total refusals, and the three unrelated reasons behind them. They
        // were one number, which made it useless: under a small budget the
        // first two are permanent and expected, so a large total said nothing
        // about whether residency was actually struggling.
        uint64_t residencyBudgetBlocks = 0;
        // The asset is larger than the whole budget. Retried every frame and
        // refused every time; no amount of eviction can help.
        uint64_t residencyOversizedBlocks = 0;
        // No mip tail of a compressed texture fits the capacity available.
        uint64_t residencyMipPlanBlocks = 0;
        // Eviction was needed and there was nothing it was allowed to take.
        // This is the one that reports real pressure.
        uint64_t residencyNoVictimBlocks = 0;
        uint32_t retiringModels = 0;
        uint32_t retiringTextures = 0;
        uint64_t retiringModelBytes = 0;
        uint64_t retiringTextureBytes = 0;
        uint32_t mipDegradedTextures = 0;
        uint32_t residentTextureMipLevels = 0;
        uint32_t availableTextureMipLevels = 0;
        uint64_t mipOmittedBytes = 0;
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
        float maxSamplerAnisotropy = 1.0f,
        AssetLoadingBudget loadingBudget = {});
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
    struct PublicationResult {
        std::size_t publications = 0;
        bool descriptorsChanged = false;
    };

    // Publishes up to maxPublications completed background tasks without
    // waiting. Failed resources stay observable in LoadingStats while frames
    // continue using available content and fallback textures.
    [[nodiscard]] PublicationResult publishReadyAssets(
        std::size_t maxPublications,
        uint32_t pendingFrameMask = 0);
    // Reclaims upload command buffers and staging resources whose GPU fences
    // have signaled. This never waits for GPU work.
    void retireCompletedUploads();
    // Releases residency resources once the fence-owned frame slot no longer
    // references them. Must be called only after that frame's fence signals.
    void completeFrame(uint32_t frameIndex);

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
    // Axis-aligned extent of a model's mesh in its own space: the editor
    // frames a model on its own from it, and the recorder transforms it to
    // world space to range-test point-shadow casters. Tiles are drawn at a
    // known grid size and do not consult it.
    //
    // Invalid until the model has finished uploading. Every caller must check
    // valid() before reading the extent - an invalid box is inverted, not
    // empty, so its minimum and maximum are not points in the scene.
    [[nodiscard]] Aabb boundsForModel(RenderModel model) const;
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

    // Both types belong to the uploader; these keep the spelling everything
    // here already used.
    using OwnedImage = VulkanTextureUploader::OwnedImage;
    using PendingTextureUpload = VulkanTextureUploader::PendingTextureUpload;

    // Two structurally identical types, kept apart on purpose.
    //
    // A ModelSlot holds one of each - a static mesh and, for a skinned model,
    // the posed mesh uploaded per frame - and they are produced by different
    // functions and consumed by different pipelines. Merging them into one
    // type would save four lines and make `slot.gpu = uploadSkinnedMesh(...)`
    // compile, which is a mix-up nothing downstream would catch: the draw
    // would bind the wrong index range and render whatever geometry happened
    // to be at that offset. Distinct types are worth keeping exactly where
    // they prevent that, and this is such a place.
    struct GpuMesh {
        VulkanGeometryArena::Allocation allocation {};
        uint32_t indexCount = 0;
    };

    struct GpuSkinnedMesh {
        VulkanGeometryArena::Allocation allocation {};
        uint32_t indexCount = 0;
    };

    struct TextureResource {
        OwnedImage image {};
        VkSampler sampler = VK_NULL_HANDLE;
        // Kept so an in-place update can refuse a differently sized image,
        // which would need a new image and a descriptor rewrite.
        uint32_t width = 0;
        uint32_t height = 0;
        bool compressed = false;
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
        Aabb bounds {};
        // Where this model's materials sit in the shared material buffer.
        // Zero means unpublished: the range never starts at the reserved
        // fallback entry. The recorder reads the base off this slot for every
        // draw. Published ranges remain stable for their entire lifetime;
        // evicted ranges are reused only after their referencing frames retire.
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
        std::future<PreparedTextureSource> future;
        std::optional<PreparedTextureSource> prepared;
        std::exception_ptr failure;
        uint64_t lastRequested = 0;
        uint64_t gpuBytes = 0;
        uint64_t fullQualityBytes = 0;
        uint32_t sourceMipLevels = 0;
        uint32_t residentBaseMip = 0;
    };

    struct AnimationSlot {
        LoadState state = LoadState::Unrequested;
        std::future<GltfAnimationClip> future;
        std::exception_ptr failure;
        uint64_t lastRequested = 0;
    };

    struct RetiredModelResources {
        GpuMesh gpu {};
        GpuSkinnedMesh skinnedGpu {};
        uint32_t materialBase = 0;
        uint32_t materialCount = 0;
        uint64_t gpuBytes = 0;
    };

    struct RetiredTextureResources {
        TextureResource gpu {};
        uint64_t gpuBytes = 0;
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
    [[nodiscard]] uint64_t texturePublicationCapacity(
        std::size_t protectedTexture) const;
    void retireModel(ModelSlot& slot);
    void retireTexture(TextureSlot& slot);
    void destroyCompletedResidencyRetirements();
    [[nodiscard]] static uint64_t meshBytes(const MeshData& mesh);
    [[nodiscard]] static uint64_t textureBytes(
        const PreparedTextureSource& texture,
        const TextureInterpretation& interpretation);

    // Whether a publication attempt should do any work at all.
    //
    // All three publish functions opened with the same ladder, and the three
    // copies had drifted in shape without drifting in behaviour: models fell
    // through on Uploading and returned false further down, textures rejected
    // it in the ladder, animations never reach it. Checked exhaustively over
    // every load state and both values of `wait` before merging - forty
    // reachable cases, all three reproduced exactly.
    enum class PublishGate { Stop, Proceed };
    template <typename Slot>
    [[nodiscard]] PublishGate publishGate(
        const Slot& slot,
        const std::filesystem::path& path,
        const char* kind,
        bool wait) const;

    // The tail every publication failure shares. Any cleanup particular to one
    // asset kind happens at the call site before this; what is here is the part
    // that must never differ - remember the exception, mark the slot failed,
    // rethrow for a caller that is waiting, and otherwise say so in the log.
    template <typename Slot>
    void recordPublishFailure(
        Slot& slot,
        const std::filesystem::path& path,
        const char* kind,
        const char* phase,
        bool wait);

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

    [[nodiscard]] static Aabb boundsOf(
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
    // Allocates a stable range for a model's materials and returns its base.
    // Throws when the buffer is full, which fails that one model's
    // publication rather than the frame.
    [[nodiscard]] uint32_t writeMaterials(
        const std::vector<MeshMaterial>& materials);
    void writeSkinningInstance(
        uint32_t frameIndex,
        uint32_t instanceSlot,
        const SkinnedMeshData& mesh,
        const AnimationController::SkinningRequest& request);
    void destroyMesh(GpuMesh& mesh);
    void destroySkinnedMesh(GpuSkinnedMesh& mesh);
    [[nodiscard]] const GpuMesh& gpuMeshForModel(RenderModel model) const;

    bool supportsBc7_ = false;
    VulkanMemoryAllocator* allocator_ = nullptr;
    uint32_t textureDescriptorCapacity_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    std::filesystem::path assetRoot_;
    const AssetManifest* manifest_ = nullptr;

    std::vector<ModelSlot> models_; // indexed by RenderModel::index()
    std::vector<AnimationSlot> animations_; // indexed by RenderAnimation::index()
    // Descriptor-indexed. Manifest/editor definitions occupy low indices;
    // discovered glTF definitions occupy stable high indices.
    std::vector<std::optional<RuntimeTextureDefinition>> textureDefinitions_;
    // How the heap is split between the manifest's stable low indices and the
    // glTF textures discovered at load time, and which slots are live.
    TextureDescriptorSpace textureSpace_;
    std::vector<std::vector<uint32_t>> modelTextureDependencies_;
    std::vector<std::vector<PrimitiveMaterialBinding>> modelMaterialBindings_;
    std::vector<TextureSlot> textures_;
    TextureResource fallbackTexture_ {};
    VulkanUploadRing uploadRing_ {};
    // Borrows the ring above, the allocator and the device handles.
    VulkanTextureUploader textureUploader_ {};
    VulkanGeometryArena geometryArena_ {};
    GpuMappedBuffer skinningBuffer_ {};
    GpuMappedBuffer drawInstanceBuffer_ {};
    GpuMappedBuffer materialBuffer_ {};
    // Mirrors the mapped buffer, sized to the allocator's high-water mark.
    // Live ranges never move, and retired ranges return to materialRanges_
    // only after their frame fences complete.
    std::vector<GpuMaterial> materialStorage_;
    MaterialRangeAllocator materialRanges_;
    FrameRetirementQueue<RetiredModelResources> retiredModels_;
    FrameRetirementQueue<RetiredTextureResources> retiredTextures_;
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
    // One pool each, with the same policy applied to both. The eviction
    // counters below stay shared because they report activity across the two.
    ResidencyBudget modelResidency_;
    ResidencyBudget textureResidency_;
    // Owns the eviction ladder and every counter that says why a publication
    // was refused; see ResidencyLadder for why the drain it runs has to cover
    // both pools.
    ResidencyLadder residencyLadder_;
    uint64_t droppedDrawInstances_ = 0;
    uint64_t droppedSkinningInstances_ = 0;
    bool textureDescriptorsDirty_ = false;
    uint32_t retirementFrameMask_ = 0;
};

} // namespace sokoban
