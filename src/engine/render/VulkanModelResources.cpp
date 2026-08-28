#include "engine/render/VulkanModelResources.hpp"

#include "engine/Log.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace sokoban {
namespace {

template <typename Result>
bool futureReady(std::future<Result>& future)
{
    return future.valid() &&
        future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

uint32_t mipLevelCount(uint32_t width, uint32_t height)
{
    const uint32_t largestDimension = std::max(width, height);
    return largestDimension == 0
        ? 1U
        : 1U + static_cast<uint32_t>(
              std::floor(std::log2(static_cast<double>(largestDimension))));
}

} // namespace

VulkanModelResources::ModelBounds VulkanModelResources::boundsOf(
    const std::vector<MeshVertex>& vertices)
{
    if (vertices.empty()) {
        return {};
    }
    ModelBounds bounds {
        .minimum = vertices.front().position,
        .maximum = vertices.front().position,
        .valid = true,
    };
    for (const MeshVertex& vertex : vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    return bounds;
}

VulkanModelResources::ModelBounds VulkanModelResources::boundsForModel(
    RenderModel model) const
{
    if (model.isCube() || model.index() >= models_.size()) {
        return {};
    }
    return models_[model.index()].bounds;
}

bool VulkanModelResources::modelReady(RenderModel model) const
{
    if (model.isCube() || model.index() >= models_.size()) {
        return false;
    }
    return models_[model.index()].state == LoadState::Ready;
}

bool VulkanModelResources::modelUsesGpuSkinning(RenderModel model) const
{
    return !model.isCube() && manifest_ != nullptr &&
        model.index() < models_.size() &&
        manifest_->model(model).geometry == ModelGeometry::Skinned;
}

bool VulkanModelResources::tileReadyForDraw(
    const RenderFrameData::Tile& tile,
    uint32_t frameIndex) const
{
    const bool meshReady = modelReady(tile.model);
    const bool requiresPublishedPose = modelUsesGpuSkinning(tile.model);
    const bool posePublished = !requiresPublishedPose ||
        skinnedInstances_.contains({
            frameIndex,
            tile.animationInstanceId,
            tile.model.value,
        });
    return modelInstanceReadyForDraw(
        meshReady, requiresPublishedPose, posePublished);
}

VulkanModelResources::~VulkanModelResources()
{
    destroy();
}

void VulkanModelResources::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::filesystem::path assetRoot,
    const AssetManifest& manifest,
    uint32_t textureDescriptorCapacity,
    float maxSamplerAnisotropy)
{
    destroy();
    if (textureDescriptorCapacity == 0 ||
        manifest.textures().size() > textureDescriptorCapacity) {
        throw std::runtime_error(
            "Asset manifest texture count exceeds the selected descriptor capacity");
    }
    physicalDevice_ = physicalDevice;
    maxSamplerAnisotropy_ = std::max(maxSamplerAnisotropy, 1.0f);
    textureDescriptorCapacity_ = textureDescriptorCapacity;
    device_ = device;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;
    assetRoot_ = std::move(assetRoot);
    manifest_ = &manifest;
    models_.resize(manifest.models().size());
    animations_.resize(manifest.animations().size());
    textures_.resize(manifest.textures().size());
    animationController_.configure(
        manifest.playerModel(), manifest.playerIdleAnimation());

    try {
        uploadRing_.create(physicalDevice_, device_);
        geometryArena_.create(
            physicalDevice_, device_, commandPool_, graphicsQueue_, uploadRing_);
        createSkinningBuffer();
        createModelInstanceBuffer();
        createMaterialBuffer();
        ImageData fallback {
            .width = 1,
            .height = 1,
            .rgba = {
                std::byte { 0xff },
                std::byte { 0xff },
                std::byte { 0xff },
                std::byte { 0xff },
            },
        };
        createTextureBlocking(
            fallback,
            fallbackTexture_.image,
            fallbackTexture_.sampler,
            TextureSampling {});
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanModelResources::destroy()
{
    if (device_) {
        std::vector<VkFence> uploadFences;
        for (const ModelSlot& model : models_) {
            if (model.upload.submitted && model.upload.fence) {
                uploadFences.push_back(model.upload.fence);
            }
        }
        for (const TextureSlot& texture : textures_) {
            if (texture.upload.submitted && texture.upload.fence) {
                uploadFences.push_back(texture.upload.fence);
            }
        }
        if (!uploadFences.empty()) {
            (void)vkWaitForFences(
                device_,
                static_cast<uint32_t>(uploadFences.size()),
                uploadFences.data(),
                VK_TRUE,
                UINT64_MAX);
        }

        skinnedInstances_.clear();
        for (auto texture = textures_.rbegin(); texture != textures_.rend(); ++texture) {
            destroyTextureUpload(texture->upload);
            destroyTexture(texture->gpu.image, texture->gpu.sampler);
        }
        destroyTexture(fallbackTexture_.image, fallbackTexture_.sampler);
        for (auto model = models_.rbegin(); model != models_.rend(); ++model) {
            geometryArena_.destroyUpload(model->upload);
            destroyMesh(model->gpu);
            destroySkinnedMesh(model->skinnedGpu);
        }
        destroySkinningBuffer();
        destroyModelInstanceBuffer();
        destroyMaterialBuffer();
        geometryArena_.destroy();
        uploadRing_.destroy();
    }

    models_.clear();
    animations_.clear();
    textures_.clear();
    materialStorage_.clear();
    fallbackTexture_ = {};
    animationController_.clear();
    manifest_ = nullptr;
    assetRoot_.clear();
    graphicsQueue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    textureDescriptorCapacity_ = 0;
    textureUploadSubmissions_ = 0;
    textureUploadCompletions_ = 0;
    scheduler_.clear();
    visibleRequestStamp_ = 0;
    modelResidencyBytes_ = 0;
    textureResidencyBytes_ = 0;
    modelResidencyPeakBytes_ = 0;
    textureResidencyPeakBytes_ = 0;
    residencyEvictions_ = 0;
    residencyBudgetBlocks_ = 0;
    residencyBudgetBlocked_ = false;
    textureDescriptorsDirty_ = false;
    activeSkinningFrame_ = UINT32_MAX;
    skinningInstanceCount_ = 0;
}

void VulkanModelResources::requestAssets(
    const RenderAssetRequirements& requirements,
    AssetLoadPriority priority)
{
    if (priority == AssetLoadPriority::Visible) {
        ++visibleRequestStamp_;
    }
    for (uint32_t i = 0; i < models_.size(); ++i) {
        const RenderModel model { i + 1 };
        if (requirements.contains(model)) {
            queueModel(model, priority);
        }
    }
    for (uint32_t i = 0; i < animations_.size(); ++i) {
        const RenderAnimation animation { i + 1 };
        if (requirements.contains(animation)) {
            queueAnimation(animation, priority);
        }
    }
    for (uint32_t i = 0; i < textures_.size(); ++i) {
        if (requirements.contains(RenderTexture { i + 1 })) {
            queueTexture(i, priority);
        }
    }
}

void VulkanModelResources::cancelQueuedPrefetches()
{
    for (const AssetLoadKey key : scheduler_.cancelQueuedPrefetches()) {
        resetCancelledAsset(key);
    }
}

bool VulkanModelResources::waitForAssets(const RenderAssetRequirements& requirements)
{
    requestAssets(requirements);

    // This is an offline-only path. Let the normal bounded scheduler drain as
    // each completed job is published; gameplay never waits here.
    while (scheduler_.queuedCount() != 0 || scheduler_.activeCount() != 0) {
        startQueuedAssets();

        for (uint32_t i = 0; i < animations_.size(); ++i) {
            const RenderAnimation animation { i + 1 };
            if (animations_[i].state == LoadState::Loading) {
                (void)publishAnimation(animation, true);
                break;
            }
        }
        for (uint32_t i = 0; i < models_.size(); ++i) {
            const RenderModel model { i + 1 };
            if (models_[i].state == LoadState::Loading) {
                (void)publishModel(model, true);
                break;
            }
        }
        for (std::size_t i = 0; i < textures_.size(); ++i) {
            if (textures_[i].state == LoadState::Loading) {
                (void)publishTexture(i, true);
                break;
            }
        }
    }
    retireCompletedGeometryUploads(true);

    if (requirements.contains(manifest_->playerModel())) {
        (void)publishAnimation(manifest_->playerIdleAnimation(), true);
    }
    for (uint32_t i = 0; i < animations_.size(); ++i) {
        const RenderAnimation animation { i + 1 };
        if (requirements.contains(animation)) {
            (void)publishAnimation(animation, true);
        }
    }
    for (uint32_t i = 0; i < models_.size(); ++i) {
        const RenderModel model { i + 1 };
        if (requirements.contains(model)) {
            (void)publishModel(model, true);
        }
    }

    bool descriptorsChanged = std::exchange(textureDescriptorsDirty_, false);
    const std::vector<bool> textureRequirements = requiredTextures(requirements);
    for (std::size_t i = 0; i < textureRequirements.size(); ++i) {
        if (!textureRequirements[i]) {
            continue;
        }
        const bool wasPublished = textures_[i].gpu.image.view != VK_NULL_HANDLE;
        (void)publishTexture(i, true);
        descriptorsChanged = descriptorsChanged ||
            (!wasPublished && textures_[i].gpu.image.view != VK_NULL_HANDLE) ||
            std::exchange(textureDescriptorsDirty_, false);
    }

    if (!assetsReady(requirements)) {
        throw std::runtime_error("Required render assets did not reach the ready state");
    }
    return descriptorsChanged;
}

bool VulkanModelResources::publishReadyAssets(std::size_t maxPublications)
{
    startQueuedAssets();
    std::size_t publications = 0;
    bool descriptorsChanged = false;

    for (uint32_t i = 0; i < animations_.size(); ++i) {
        if (publications >= maxPublications) {
            return descriptorsChanged;
        }
        const RenderAnimation animation { i + 1 };
        AnimationSlot& slot = animations_[i];
        if (slot.state == LoadState::Loading && futureReady(slot.future) &&
            publishAnimation(animation, false)) {
            ++publications;
        }
    }

    for (uint32_t i = 0; i < models_.size(); ++i) {
        if (publications >= maxPublications) {
            return descriptorsChanged;
        }
        const RenderModel model { i + 1 };
        ModelSlot& slot = models_[i];
        const bool canPublish =
            (slot.state == LoadState::Loading && futureReady(slot.future)) ||
            slot.state == LoadState::CpuReady;
        if (canPublish && publishModel(model, false)) {
            ++publications;
        }
    }

    for (std::size_t i = 0; i < textures_.size(); ++i) {
        if (publications >= maxPublications) {
            return descriptorsChanged;
        }
        TextureSlot& slot = textures_[i];
        const bool canPublish =
            (slot.state == LoadState::Loading && futureReady(slot.future)) ||
            slot.state == LoadState::CpuReady;
        if (!canPublish) {
            continue;
        }
        const bool wasPublished = slot.gpu.image.view != VK_NULL_HANDLE;
        if (publishTexture(i, false)) {
            ++publications;
            descriptorsChanged = descriptorsChanged ||
                (!wasPublished && slot.gpu.image.view != VK_NULL_HANDLE) ||
                std::exchange(textureDescriptorsDirty_, false);
        }
    }
    startQueuedAssets();
    return descriptorsChanged;
}

void VulkanModelResources::retireCompletedUploads()
{
    retireCompletedGeometryUploads(false);
    for (TextureSlot& slot : textures_) {
        if (slot.state != LoadState::Uploading) {
            continue;
        }

        const VkResult status = vkGetFenceStatus(device_, slot.upload.fence);
        if (status == VK_NOT_READY) {
            continue;
        }
        vkCheck(status, "vkGetFenceStatus texture upload failed");
        destroyTextureUpload(slot.upload);
        slot.state = LoadState::Ready;
        ++textureUploadCompletions_;
    }
}

void VulkanModelResources::retireCompletedGeometryUploads(bool wait)
{
    for (ModelSlot& slot : models_) {
        if (slot.state != LoadState::Uploading || !slot.upload.submitted) {
            continue;
        }
        if (wait) {
            vkCheck(
                vkWaitForFences(device_, 1, &slot.upload.fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences geometry upload failed");
        }
        if (!geometryArena_.uploadComplete(slot.upload)) {
            continue;
        }
        geometryArena_.destroyUpload(slot.upload);
        slot.state = LoadState::Ready;
    }
}

void VulkanModelResources::queueModel(
    RenderModel model,
    AssetLoadPriority priority)
{
    ModelSlot& slot = models_[model.index()];
    if (priority == AssetLoadPriority::Visible) {
        slot.lastRequested = visibleRequestStamp_;
    }
    if (slot.state == LoadState::Unrequested) {
        slot.state = LoadState::Queued;
        scheduler_.request({
            AssetLoadKind::Model,
            static_cast<uint32_t>(model.index()),
        }, priority);
    } else if (slot.state == LoadState::Queued) {
        scheduler_.request({
            AssetLoadKind::Model,
            static_cast<uint32_t>(model.index()),
        }, priority);
    }

    queueModelDependencies(model, priority);
}

void VulkanModelResources::startModel(RenderModel model)
{
    const AssetManifest::Model& definition = manifest_->model(model);
    ModelSlot& slot = models_[model.index()];
    if (slot.state != LoadState::Queued) {
        throw std::logic_error("Started a model asset that was not queued");
    }

    const std::filesystem::path path = assetRoot_ / definition.path;
    GltfMeshLoadOptions options {
        .preserveAspectRatio = definition.preserveAspectRatio,
        .preserveSourceScale = definition.preserveSourceScale,
        .rotateHalfTurn = definition.rotateHalfTurn,
    };
    options.primitiveMaterials.reserve(definition.primitiveMaterials.size());
    for (const AssetManifest::Model::PrimitiveMaterial& material :
         definition.primitiveMaterials) {
        options.primitiveMaterials.push_back({
            .textureIndex = material.textureIndex,
            .flags = material.scrollV ? PrimitiveMaterialScrollV : PrimitiveMaterialNone,
        });
    }
    const ModelGeometry geometry = definition.geometry;
    const std::filesystem::path assetRoot = assetRoot_;
    const std::vector<AssetManifest::Model::Attachment> attachments =
        definition.attachments;
    slot.future = taskSystem().enqueue(
        [path, options, geometry, assetRoot, attachments]() -> PreparedModel {
            if (geometry == ModelGeometry::Skinned) {
                SkinnedMeshData mesh = loadGltfSkinnedMesh(path, options);
                for (const AssetManifest::Model::Attachment& attachment : attachments) {
                    addSkinnedAttachment(
                        mesh,
                        loadGltfMesh(
                            assetRoot / attachment.path,
                            GltfMeshLoadOptions {
                                .preserveSourceScale = true,
                                .rotateHalfTurn = attachment.rotateHalfTurn,
                            }),
                        attachment.node);
                }
                return mesh;
            }
            return loadGltfMesh(path, options);
        });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::queueTexture(
    std::size_t textureIndex,
    AssetLoadPriority priority)
{
    if (textureIndex >= textures_.size()) {
        throw std::runtime_error("Model material references an invalid texture index");
    }
    TextureSlot& slot = textures_[textureIndex];
    if (priority == AssetLoadPriority::Visible) {
        slot.lastRequested = visibleRequestStamp_;
    }
    if (slot.state == LoadState::Unrequested) {
        slot.state = LoadState::Queued;
        scheduler_.request({
            AssetLoadKind::Texture,
            static_cast<uint32_t>(textureIndex),
        }, priority);
    } else if (slot.state == LoadState::Queued) {
        scheduler_.request({
            AssetLoadKind::Texture,
            static_cast<uint32_t>(textureIndex),
        }, priority);
    }
}

void VulkanModelResources::startTexture(std::size_t textureIndex)
{
    TextureSlot& slot = textures_[textureIndex];
    if (slot.state != LoadState::Queued) {
        throw std::logic_error("Started a texture asset that was not queued");
    }

    const std::filesystem::path path =
        assetRoot_ / manifest_->textures()[textureIndex].path;
    slot.future = taskSystem().enqueue([path] {
        return loadRgbaImage(path);
    });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::queueAnimation(
    RenderAnimation animation,
    AssetLoadPriority priority)
{
    AnimationSlot& slot = animations_[animation.index()];
    if (priority == AssetLoadPriority::Visible) {
        slot.lastRequested = visibleRequestStamp_;
    }
    if (slot.state == LoadState::Unrequested) {
        slot.state = LoadState::Queued;
        scheduler_.request({
            AssetLoadKind::Animation,
            static_cast<uint32_t>(animation.index()),
        }, priority);
    } else if (slot.state == LoadState::Queued) {
        scheduler_.request({
            AssetLoadKind::Animation,
            static_cast<uint32_t>(animation.index()),
        }, priority);
    }
}

void VulkanModelResources::startAnimation(RenderAnimation animation)
{
    const AssetManifest::Animation& definition = manifest_->animation(animation);
    AnimationSlot& slot = animations_[animation.index()];
    if (slot.state != LoadState::Queued) {
        throw std::logic_error("Started an animation asset that was not queued");
    }

    const std::filesystem::path path = assetRoot_ / definition.path;
    const uint32_t animationIndex =
        animationIndexFromManifestClip(definition.clip);
    slot.future = taskSystem().enqueue([path, animationIndex] {
        return loadGltfAnimationClip(path, animationIndex);
    });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::queueModelDependencies(
    RenderModel model,
    AssetLoadPriority priority)
{
    const AssetManifest::Model& definition = manifest_->model(model);
    if (definition.materialMode == ModelMaterialMode::SingleTexture) {
        queueTexture(definition.textureIndex, priority);
    } else if (definition.materialMode == ModelMaterialMode::PrimitiveMaterials) {
        for (const AssetManifest::Model::PrimitiveMaterial& material :
             definition.primitiveMaterials) {
            queueTexture(material.textureIndex, priority);
        }
    }
    if (definition.geometry == ModelGeometry::Skinned) {
        queueAnimation(manifest_->playerIdleAnimation(), priority);
    }
}

void VulkanModelResources::startQueuedAssets()
{
    while (const std::optional<AssetLoadKey> key = scheduler_.beginNext()) {
        try {
            startAsset(*key);
        } catch (...) {
            scheduler_.complete(*key);
            resetCancelledAsset(*key);
            throw;
        }
    }
}

void VulkanModelResources::startAsset(AssetLoadKey key)
{
    switch (key.kind) {
    case AssetLoadKind::Model:
        startModel(RenderModel { key.index + 1 });
        return;
    case AssetLoadKind::Animation:
        startAnimation(RenderAnimation { key.index + 1 });
        return;
    case AssetLoadKind::Texture:
        startTexture(key.index);
        return;
    }
    throw std::logic_error("Unknown queued asset type");
}

void VulkanModelResources::resetCancelledAsset(AssetLoadKey key)
{
    switch (key.kind) {
    case AssetLoadKind::Model:
        if (key.index < models_.size() &&
            models_[key.index].state == LoadState::Queued) {
            models_[key.index] = {};
        }
        return;
    case AssetLoadKind::Animation:
        if (key.index < animations_.size() &&
            animations_[key.index].state == LoadState::Queued) {
            animations_[key.index] = {};
        }
        return;
    case AssetLoadKind::Texture:
        if (key.index < textures_.size() &&
            textures_[key.index].state == LoadState::Queued) {
            textures_[key.index] = {};
        }
        return;
    }
}

void VulkanModelResources::completeCpuJob(AssetLoadKey key)
{
    scheduler_.complete(key);
}

bool VulkanModelResources::publishModel(RenderModel model, bool wait)
{
    ModelSlot& slot = models_[model.index()];
    const AssetManifest::Model& definition = manifest_->model(model);
    if (slot.state == LoadState::Ready) {
        return false;
    }
    if (slot.state == LoadState::Failed) {
        if (wait) {
            throwIfFailed(slot.state, slot.failure, assetRoot_ / definition.path, "model");
        }
        return false;
    }
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        return false;
    }
    if (slot.state == LoadState::CpuReady) {
        try {
            if (!slot.prepared) {
                throw std::runtime_error("Model publication lost its prepared mesh");
            }
            if (std::holds_alternative<MeshData>(*slot.prepared)) {
                const MeshData& mesh = std::get<MeshData>(*slot.prepared);
                const uint64_t bytes = meshBytes(mesh);
                if (!makeModelResident(model, bytes)) {
                    return false;
                }
                slot.bounds = boundsOf(mesh.vertices);
                slot.materialBase = writeMaterials(mesh.materials);
                slot.materialCount =
                    static_cast<uint32_t>(mesh.materials.size());
                slot.gpu = uploadMesh(mesh, slot.upload);
                slot.gpuBytes = bytes;
                modelResidencyBytes_ += bytes;
                modelResidencyPeakBytes_ = std::max(
                    modelResidencyPeakBytes_, modelResidencyBytes_);
                slot.state = LoadState::Uploading;
            } else {
                slot.skinnedSource = std::make_shared<SkinnedMeshData>(
                    std::move(std::get<SkinnedMeshData>(*slot.prepared)));
                const std::vector<GpuSkinnedVertex> vertices =
                    makeGpuSkinnedVertices(*slot.skinnedSource);
                const std::vector<uint32_t> indices =
                    makeGpuSkinnedIndices(*slot.skinnedSource);
                if (vertices.empty() || indices.empty()) {
                    throw std::runtime_error("glTF skinned mesh contains no geometry");
                }
                const uint64_t bytes =
                    static_cast<uint64_t>(vertices.size()) * sizeof(GpuSkinnedVertex) +
                    static_cast<uint64_t>(indices.size()) *
                        sizeof(uint32_t);
                if (!makeModelResident(model, bytes)) {
                    slot.skinnedSource.reset();
                    return false;
                }
                slot.materialBase = writeMaterials(slot.skinnedSource->materials);
                slot.materialCount = static_cast<uint32_t>(
                    slot.skinnedSource->materials.size());
                slot.skinnedGpu = uploadSkinnedMesh(*slot.skinnedSource, slot.upload);
                slot.gpuBytes = bytes;
                modelResidencyBytes_ += bytes;
                modelResidencyPeakBytes_ = std::max(
                    modelResidencyPeakBytes_, modelResidencyBytes_);
                slot.state = LoadState::Uploading;
            }
            slot.prepared.reset();
        } catch (...) {
            // A range claimed before the upload threw belongs to nobody now.
            // Zeroing the count is what makes the next repack reclaim it.
            slot.materialBase = 0;
            slot.materialCount = 0;
            slot.failure = std::current_exception();
            slot.state = LoadState::Failed;
            if (wait) {
                throwIfFailed(slot.state, slot.failure, assetRoot_ / definition.path, "model");
            }
            log::error(log::Category::Assets)
                << "Background model publication failed: "
                << (assetRoot_ / definition.path).string();
        }
        return slot.state == LoadState::Ready ||
            slot.state == LoadState::Uploading ||
            slot.state == LoadState::Failed;
    }
    if (slot.state != LoadState::Loading ||
        (!wait && !futureReady(slot.future))) {
        return false;
    }

    try {
        completeCpuJob({
            AssetLoadKind::Model,
            static_cast<uint32_t>(model.index()),
        });
        slot.prepared = slot.future.get();
        slot.state = LoadState::CpuReady;
        if (std::holds_alternative<SkinnedMeshData>(*slot.prepared)) {
            const SkinnedMeshData& skinned =
                std::get<SkinnedMeshData>(*slot.prepared);
            slot.bounds = {
                .minimum = skinned.sourceMinimum,
                .maximum = skinned.sourceMaximum,
                .valid = true,
            };
        }
        return publishModel(model, wait);
    } catch (...) {
        slot.failure = std::current_exception();
        slot.state = LoadState::Failed;
        if (wait) {
            throwIfFailed(slot.state, slot.failure, assetRoot_ / definition.path, "model");
        }
        log::error(log::Category::Assets)
            << "Background model publication failed: "
            << (assetRoot_ / definition.path).string();
    }
    return true;
}

bool VulkanModelResources::publishTexture(std::size_t textureIndex, bool wait)
{
    TextureSlot& slot = textures_.at(textureIndex);
    const std::filesystem::path path =
        assetRoot_ / manifest_->textures()[textureIndex].path;
    if (slot.state == LoadState::Uploading || slot.state == LoadState::Ready) {
        return false;
    }
    if (slot.state == LoadState::Failed) {
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, "texture");
        }
        return false;
    }
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        return false;
    }
    if (slot.state == LoadState::Loading &&
        !wait && !futureReady(slot.future)) {
        return false;
    }

    if (slot.state == LoadState::Loading) {
        try {
            completeCpuJob({
                AssetLoadKind::Texture,
                static_cast<uint32_t>(textureIndex),
            });
            slot.prepared = slot.future.get();
            slot.state = LoadState::CpuReady;
        } catch (...) {
            if (!slot.upload.submitted) {
                destroyTextureUpload(slot.upload);
                destroyTexture(slot.gpu.image, slot.gpu.sampler);
            }
            slot.failure = std::current_exception();
            slot.state = LoadState::Failed;
            if (wait) {
                throwIfFailed(slot.state, slot.failure, path, "texture");
            }
            log::error(log::Category::Assets)
                << "Background texture preparation failed: "
                << path.string();
            return true;
        }
    }

    if (slot.state != LoadState::CpuReady || !slot.prepared) {
        return false;
    }

    try {
        const ImageData& image = *slot.prepared;
        const uint64_t bytes = textureBytes(image);
        if (!makeTextureResident(textureIndex, bytes)) {
            return false;
        }
        beginTextureUpload(
            image,
            slot.gpu.image,
            slot.gpu.sampler,
            slot.upload,
            samplingFor(manifest_->textures()[textureIndex]));
        slot.gpu.width = image.width;
        slot.gpu.height = image.height;
        slot.gpuBytes = bytes;
        textureResidencyBytes_ += bytes;
        textureResidencyPeakBytes_ = std::max(
            textureResidencyPeakBytes_, textureResidencyBytes_);
        slot.prepared.reset();
        slot.state = LoadState::Uploading;
        ++textureUploadSubmissions_;
    } catch (...) {
        if (!slot.upload.submitted) {
            destroyTextureUpload(slot.upload);
            destroyTexture(slot.gpu.image, slot.gpu.sampler);
        }
        slot.failure = std::current_exception();
        slot.state = LoadState::Failed;
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, "texture");
        }
        log::error(log::Category::Assets)
            << "Background texture publication failed: "
            << path.string();
    }
    return true;
}

uint64_t VulkanModelResources::meshBytes(const MeshData& mesh)
{
    return static_cast<uint64_t>(sizeof(MeshVertex)) * mesh.vertices.size() +
        static_cast<uint64_t>(sizeof(uint32_t)) * mesh.indices.size();
}

uint64_t VulkanModelResources::textureBytes(const ImageData& image)
{
    // Linear-filtered textures may generate a full mip chain. Four thirds of
    // the base level is a conservative estimate and keeps the residency cap
    // meaningful even before the Vulkan allocation is created.
    const uint64_t base = image.rgba.size();
    return base + base / 3U;
}

void VulkanModelResources::markResidencyBudgetBlocked()
{
    residencyBudgetBlocked_ = true;
    ++residencyBudgetBlocks_;
}

bool VulkanModelResources::makeModelResident(
    RenderModel protectedModel,
    uint64_t requiredBytes)
{
    const uint64_t budget = scheduler_.budget().modelResidencyBytes;
    if (requiredBytes > budget) {
        markResidencyBudgetBlocked();
        return false;
    }
    if (modelResidencyBytes_ + requiredBytes <= budget) {
        return true;
    }

    auto findVictim = [&]() -> std::optional<std::size_t> {
        std::optional<std::size_t> victim;
        for (std::size_t index = 0; index < models_.size(); ++index) {
            const ModelSlot& slot = models_[index];
            if (index == protectedModel.index() ||
                slot.state != LoadState::Ready || slot.gpuBytes == 0 ||
                (visibleRequestStamp_ != 0 &&
                    slot.lastRequested == visibleRequestStamp_)) {
                continue;
            }
            if (!victim ||
                slot.lastRequested < models_[*victim].lastRequested) {
                victim = index;
            }
        }
        return victim;
    };
    if (!findVictim()) {
        markResidencyBudgetBlocked();
        return false;
    }

    // A descriptor set in another in-flight frame may still reference a
    // candidate. Eviction is rare and only happens at the hard cap, so the
    // explicit idle is a safer trade-off than use-after-free GPU resources.
    vkCheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle model residency eviction failed");
    while (modelResidencyBytes_ + requiredBytes > budget) {
        const std::optional<std::size_t> victim = findVictim();
        if (!victim) {
            repackMaterials();
            markResidencyBudgetBlocked();
            return false;
        }
        ModelSlot& slot = models_[*victim];
        modelResidencyBytes_ -= slot.gpuBytes;
        destroyMesh(slot.gpu);
        destroySkinnedMesh(slot.skinnedGpu);
        slot = {};
        ++residencyEvictions_;
    }
    // The evicted slots took their material ranges with them. Nothing has
    // been submitted since the wait above, so closing the gaps now cannot
    // move a range out from under a frame that is still reading it.
    repackMaterials();
    residencyBudgetBlocked_ = false;
    return true;
}

bool VulkanModelResources::makeTextureResident(
    std::size_t protectedTexture,
    uint64_t requiredBytes)
{
    const uint64_t budget = scheduler_.budget().textureResidencyBytes;
    if (requiredBytes > budget) {
        markResidencyBudgetBlocked();
        return false;
    }
    if (textureResidencyBytes_ + requiredBytes <= budget) {
        return true;
    }

    auto findVictim = [&]() -> std::optional<std::size_t> {
        std::optional<std::size_t> victim;
        for (std::size_t index = 0; index < textures_.size(); ++index) {
            const TextureSlot& slot = textures_[index];
            if (index == protectedTexture ||
                slot.state != LoadState::Ready || slot.gpuBytes == 0 ||
                (visibleRequestStamp_ != 0 &&
                    slot.lastRequested == visibleRequestStamp_)) {
                continue;
            }
            if (!victim ||
                slot.lastRequested < textures_[*victim].lastRequested) {
                victim = index;
            }
        }
        return victim;
    };
    if (!findVictim()) {
        markResidencyBudgetBlocked();
        return false;
    }

    vkCheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle texture residency eviction failed");
    retireCompletedUploads();
    while (textureResidencyBytes_ + requiredBytes > budget) {
        const std::optional<std::size_t> victim = findVictim();
        if (!victim) {
            markResidencyBudgetBlocked();
            return false;
        }
        TextureSlot& slot = textures_[*victim];
        textureResidencyBytes_ -= slot.gpuBytes;
        destroyTexture(slot.gpu.image, slot.gpu.sampler);
        slot = {};
        textureDescriptorsDirty_ = true;
        ++residencyEvictions_;
    }
    residencyBudgetBlocked_ = false;
    return true;
}

bool VulkanModelResources::publishAnimation(RenderAnimation animation, bool wait)
{
    AnimationSlot& slot = animations_[animation.index()];
    const AssetManifest::Animation& definition = manifest_->animation(animation);
    const std::filesystem::path path = assetRoot_ / definition.path;
    if (slot.state == LoadState::Ready) {
        return false;
    }
    if (slot.state == LoadState::Failed) {
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, "animation");
        }
        return false;
    }
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        return false;
    }
    if (!wait && !futureReady(slot.future)) {
        return false;
    }

    try {
        completeCpuJob({
            AssetLoadKind::Animation,
            static_cast<uint32_t>(animation.index()),
        });
        animationController_.setClip(animation, slot.future.get());
        slot.state = LoadState::Ready;
    } catch (...) {
        slot.failure = std::current_exception();
        slot.state = LoadState::Failed;
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, "animation");
        }
        log::error(log::Category::Assets)
            << "Background animation publication failed: "
            << path.string();
    }
    return true;
}

void VulkanModelResources::throwIfFailed(
    LoadState state,
    const std::exception_ptr& failure,
    const std::filesystem::path& path,
    const char* kind) const
{
    if (state != LoadState::Failed) {
        return;
    }
    try {
        if (failure) {
            std::rethrow_exception(failure);
        }
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "Failed to load " + std::string(kind) + " asset '" +
            path.string() + "': " + error.what());
    }
    throw std::runtime_error(
        "Failed to load " + std::string(kind) + " asset '" +
        path.string() + "'");
}

std::vector<bool> VulkanModelResources::requiredTextures(
    const RenderAssetRequirements& requirements) const
{
    std::vector<bool> result(textures_.size(), false);
    for (uint32_t i = 0; i < textures_.size(); ++i) {
        result[i] = requirements.contains(RenderTexture { i + 1 });
    }
    for (uint32_t i = 0; i < models_.size(); ++i) {
        const RenderModel model { i + 1 };
        if (!requirements.contains(model)) {
            continue;
        }
        const AssetManifest::Model& definition = manifest_->model(model);
        if (definition.materialMode == ModelMaterialMode::SingleTexture) {
            result.at(definition.textureIndex) = true;
        } else if (definition.materialMode == ModelMaterialMode::PrimitiveMaterials) {
            for (const AssetManifest::Model::PrimitiveMaterial& material :
                 definition.primitiveMaterials) {
                result.at(material.textureIndex) = true;
            }
        }
    }
    return result;
}

bool VulkanModelResources::assetsReady(
    const RenderAssetRequirements& requirements) const
{
    for (uint32_t i = 0; i < models_.size(); ++i) {
        if (requirements.contains(RenderModel { i + 1 }) &&
            models_[i].state != LoadState::Ready) {
            return false;
        }
    }
    for (uint32_t i = 0; i < animations_.size(); ++i) {
        if (requirements.contains(RenderAnimation { i + 1 }) &&
            animations_[i].state != LoadState::Ready) {
            return false;
        }
    }
    const std::vector<bool> textureRequirements = requiredTextures(requirements);
    for (std::size_t i = 0; i < textureRequirements.size(); ++i) {
        if (textureRequirements[i] &&
            textures_[i].state != LoadState::Uploading &&
            textures_[i].state != LoadState::Ready) {
            return false;
        }
    }
    return true;
}

void VulkanModelResources::setAnimationPreview(
    RenderModel model,
    const GltfAnimationClip* clip,
    float timeSeconds)
{
    animationController_.setPreview(model, clip, timeSeconds);
}

void VulkanModelResources::updateAnimations(
    const RenderFrameData& frameData,
    uint32_t frameIndex)
{
    if (activeSkinningFrame_ != frameIndex) {
        beginAnimationFrame(frameIndex);
    }
    for (const AnimationController::InstanceSkinningRequest& request :
         animationController_.updateInstances(frameData)) {
        if (request.model.isCube() || request.model.index() >= models_.size()) {
            continue;
        }
        ModelSlot& model = models_[request.model.index()];
        if (model.state != LoadState::Ready || !model.skinnedSource) {
            // The animation data or skinned mesh is still loading. The scene
            // recorder will skip this instance until its pose is published.
            continue;
        }
        const AnimatedMeshKey key {
            frameIndex,
            request.instanceId,
            request.model.value,
        };
        auto [instance, inserted] = skinnedInstances_.try_emplace(key, 0);
        if (inserted) {
            if (skinningInstanceCount_ >= maxSkinnedInstancesPerFrame) {
                throw std::runtime_error("GPU skinning instance budget exceeded");
            }
            instance->second = skinningInstanceCount_++;
        }
        writeSkinningInstance(
            frameIndex,
            instance->second,
            *model.skinnedSource,
            request.skinning);
    }
}

void VulkanModelResources::beginAnimationFrame(uint32_t frameIndex)
{
    if (frameIndex >= gpuSkinningFrameCount) {
        throw std::out_of_range("GPU skinning frame index is out of range");
    }
    for (auto it = skinnedInstances_.begin(); it != skinnedInstances_.end();) {
        if (it->first.frameIndex == frameIndex) {
            it = skinnedInstances_.erase(it);
        } else {
            ++it;
        }
    }
    activeSkinningFrame_ = frameIndex;
    skinningInstanceCount_ = 0;
    drawInstanceCount_ = 0;
}

uint32_t VulkanModelResources::writeDrawInstance(
    uint32_t frameIndex,
    const GpuDrawInstance& instance)
{
    if (frameIndex >= gpuSkinningFrameCount || !drawInstanceBuffer_.mapped ||
        drawInstanceCount_ >= maxDrawInstancesPerFrame) {
        throw std::runtime_error("Draw instance buffer is exhausted or invalid");
    }
    const uint32_t result = frameIndex * maxDrawInstancesPerFrame +
        drawInstanceCount_++;
    std::memcpy(
        static_cast<std::byte*>(drawInstanceBuffer_.mapped) +
            static_cast<uint64_t>(result) * sizeof(GpuDrawInstance),
        &instance,
        sizeof(instance));
    return result;
}

VulkanModelResources::MeshView VulkanModelResources::meshForTile(
    const RenderFrameData::Tile& tile,
    uint32_t frameIndex) const
{
    const AssetManifest::Model& definition = manifest_->model(tile.model);
    if (definition.geometry == ModelGeometry::Skinned) {
        if (models_[tile.model.index()].state != LoadState::Ready) {
            throw std::runtime_error("Skinned model was used before it was ready");
        }
        const auto instance = skinnedInstances_.find(
            { frameIndex, tile.animationInstanceId, tile.model.value });
        if (instance == skinnedInstances_.end()) {
            throw std::runtime_error(
                "Skinned model instance pose was not published (model='" +
                definition.name + "', modelId=" +
                std::to_string(tile.model.value) + ", animationId=" +
                std::to_string(tile.animation.value) + ", instanceId=" +
                std::to_string(tile.animationInstanceId) + ", frame=" +
                std::to_string(frameIndex) + ")");
        }
        const ModelSlot& slot = models_[tile.model.index()];
        if (!slot.skinnedGpu.allocation.valid()) {
            throw std::runtime_error("Skinned model has no GPU geometry");
        }
        return {
            .vertexBuffer = geometryArena_.vertexBuffer(slot.skinnedGpu.allocation),
            .vertexOffset = geometryArena_.vertexOffset(slot.skinnedGpu.allocation),
            .indexBuffer = geometryArena_.indexBuffer(slot.skinnedGpu.allocation),
            .indexOffset = geometryArena_.indexOffset(slot.skinnedGpu.allocation),
            .indexCount = slot.skinnedGpu.indexCount,
            .firstInstance = frameIndex * maxSkinnedInstancesPerFrame +
                instance->second,
            .skinned = true,
        };
    }
    const GpuMesh& mesh = gpuMeshForModel(tile.model);
    return {
        .vertexBuffer = geometryArena_.vertexBuffer(mesh.allocation),
        .vertexOffset = geometryArena_.vertexOffset(mesh.allocation),
        .indexBuffer = geometryArena_.indexBuffer(mesh.allocation),
        .indexOffset = geometryArena_.indexOffset(mesh.allocation),
        .indexCount = mesh.indexCount,
    };
}

VulkanModelResources::MaterialBinding VulkanModelResources::materialForModel(
    RenderModel model) const
{
    const AssetManifest::Model& definition = manifest_->model(model);
    const uint32_t materialBase = model.index() < models_.size()
        ? models_[model.index()].materialBase
        : 0;
    return {
        .mode = definition.materialMode,
        .textureIndex = definition.textureIndex,
        .materialBase = materialBase,
    };
}

std::vector<VulkanModelResources::TextureView> VulkanModelResources::textures() const
{
    // Every allocated descriptor is initialized. Nonresident manifest entries
    // and reserved future slots both point at the fallback, so the renderer
    // does not need partially-bound descriptor behavior.
    std::vector<TextureView> result;
    result.reserve(textureDescriptorCapacity_);
    for (const TextureSlot& texture : textures_) {
        const TextureResource& resource =
            (texture.state == LoadState::Uploading ||
                texture.state == LoadState::Ready)
            ? texture.gpu
            : fallbackTexture_;
        result.push_back({
            .imageView = resource.image.view,
            .sampler = resource.sampler,
        });
    }
    while (result.size() < textureDescriptorCapacity_) {
        result.push_back({
            .imageView = fallbackTexture_.image.view,
            .sampler = fallbackTexture_.sampler,
        });
    }
    return result;
}

uint32_t VulkanModelResources::textureCount() const
{
    return textureDescriptorCapacity_;
}

VulkanModelResources::SkinningBufferView VulkanModelResources::skinningBuffer() const
{
    return {
        .buffer = skinningBuffer_.buffer,
        .range = static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
            maxSkinnedInstancesPerFrame * sizeof(GpuSkinningInstance),
    };
}

VulkanModelResources::DrawInstanceBufferView
VulkanModelResources::drawInstanceBuffer() const
{
    return {
        .buffer = drawInstanceBuffer_.buffer,
        .range = static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
            maxDrawInstancesPerFrame * sizeof(GpuDrawInstance),
    };
}

VulkanModelResources::MaterialBufferView
VulkanModelResources::materialBuffer() const
{
    return {
        .buffer = materialBuffer_.buffer,
        .range = static_cast<VkDeviceSize>(maxModelMaterials) *
            sizeof(GpuMaterial),
    };
}

VulkanModelResources::LoadingStats VulkanModelResources::loadingStats() const
{
    LoadingStats result {
        .totalModels = static_cast<uint32_t>(models_.size()),
        .totalTextures = static_cast<uint32_t>(textures_.size()),
        .totalAnimations = static_cast<uint32_t>(animations_.size()),
    };
    auto countState = [&result](LoadState state, uint32_t& loaded, uint32_t& pending) {
        if (state == LoadState::Ready) {
            ++loaded;
            ++result.readyRequestedAssets;
        } else if (state == LoadState::Loading ||
            state == LoadState::Queued ||
            state == LoadState::CpuReady ||
            state == LoadState::Uploading) {
            ++pending;
        } else if (state == LoadState::Failed) {
            ++result.failedAssets;
        }
    };
    for (const ModelSlot& model : models_) {
        countState(model.state, result.loadedModels, result.pendingModels);
        result.requestedAssets += model.state != LoadState::Unrequested;
    }
    for (const TextureSlot& texture : textures_) {
        countState(texture.state, result.loadedTextures, result.pendingTextures);
        result.requestedAssets += texture.state != LoadState::Unrequested;
        if (texture.state == LoadState::Uploading) {
            ++result.uploadingTextures;
        }
    }
    for (const AnimationSlot& animation : animations_) {
        countState(animation.state, result.loadedAnimations, result.pendingAnimations);
        result.requestedAssets += animation.state != LoadState::Unrequested;
    }
    result.queuedAssets = static_cast<uint32_t>(scheduler_.queuedCount());
    result.activeCpuJobs = static_cast<uint32_t>(scheduler_.activeCount());
    result.cancelledPrefetches = scheduler_.cancelledPrefetchCount();
    result.modelResidencyBytes = modelResidencyBytes_;
    result.textureResidencyBytes = textureResidencyBytes_;
    result.modelResidencyPeakBytes = modelResidencyPeakBytes_;
    result.textureResidencyPeakBytes = textureResidencyPeakBytes_;
    result.modelResidencyBudgetBytes = scheduler_.budget().modelResidencyBytes;
    result.textureResidencyBudgetBytes = scheduler_.budget().textureResidencyBytes;
    result.residencyEvictions = residencyEvictions_;
    result.residencyBudgetBlocks = residencyBudgetBlocks_;
    result.residencyBudgetBlocked = residencyBudgetBlocked_;
    result.textureUploadSubmissions = textureUploadSubmissions_;
    result.textureUploadCompletions = textureUploadCompletions_;
    return result;
}

VulkanModelResources::GpuMesh VulkanModelResources::uploadMesh(
    const MeshData& mesh,
    VulkanGeometryArena::Upload& upload)
{
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::runtime_error("glTF mesh contains no geometry");
    }

    const VkDeviceSize vertexBytes = sizeof(MeshVertex) * mesh.vertices.size();
    const VkDeviceSize indexBytes = sizeof(uint32_t) * mesh.indices.size();
    GpuMesh result;
    result.allocation = geometryArena_.allocate(vertexBytes, indexBytes);
    try {
        upload = geometryArena_.beginUpload(result.allocation, mesh);
        result.indexCount = static_cast<uint32_t>(mesh.indices.size());
        return result;
    } catch (...) {
        geometryArena_.release(result.allocation);
        throw;
    }
}

VulkanModelResources::GpuSkinnedMesh VulkanModelResources::uploadSkinnedMesh(
    const SkinnedMeshData& mesh,
    VulkanGeometryArena::Upload& upload)
{
    const std::vector<GpuSkinnedVertex> vertices = makeGpuSkinnedVertices(mesh);
    const std::vector<uint32_t> indices = makeGpuSkinnedIndices(mesh);
    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("glTF skinned mesh contains no geometry");
    }
    const VkDeviceSize vertexBytes = sizeof(GpuSkinnedVertex) * vertices.size();
    const VkDeviceSize indexBytes = sizeof(uint32_t) * indices.size();
    GpuSkinnedMesh result;
    result.allocation = geometryArena_.allocate(vertexBytes, indexBytes);
    try {
        upload = geometryArena_.beginUpload(
            result.allocation,
            vertices.data(),
            vertexBytes,
            indices.data(),
            indexBytes);
        result.indexCount = static_cast<uint32_t>(indices.size());
        return result;
    } catch (...) {
        geometryArena_.release(result.allocation);
        throw;
    }
}

void VulkanModelResources::createSkinningBuffer()
{
    const VkDeviceSize size = static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
        maxSkinnedInstancesPerFrame * sizeof(GpuSkinningInstance);
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &skinningBuffer_.buffer),
        "vkCreateBuffer GPU skinning palette failed");
    try {
        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device_, skinningBuffer_.buffer, &requirements);
        const VkMemoryAllocateInfo allocationInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(
                    device_, &allocationInfo, nullptr, &skinningBuffer_.memory),
            "vkAllocateMemory GPU skinning palette failed");
        vkCheck(vkBindBufferMemory(
                    device_, skinningBuffer_.buffer, skinningBuffer_.memory, 0),
            "vkBindBufferMemory GPU skinning palette failed");
        vkCheck(vkMapMemory(device_, skinningBuffer_.memory, 0, size, 0,
                    &skinningBuffer_.mapped),
            "vkMapMemory GPU skinning palette failed");
        std::memset(skinningBuffer_.mapped, 0, static_cast<std::size_t>(size));
    } catch (...) {
        destroySkinningBuffer();
        throw;
    }
}

void VulkanModelResources::destroySkinningBuffer()
{
    if (device_ && skinningBuffer_.mapped) {
        vkUnmapMemory(device_, skinningBuffer_.memory);
    }
    if (device_ && skinningBuffer_.buffer) {
        vkDestroyBuffer(device_, skinningBuffer_.buffer, nullptr);
    }
    if (device_ && skinningBuffer_.memory) {
        vkFreeMemory(device_, skinningBuffer_.memory, nullptr);
    }
    skinningBuffer_ = {};
}

void VulkanModelResources::createModelInstanceBuffer()
{
    const VkDeviceSize size = static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
        maxDrawInstancesPerFrame * sizeof(GpuDrawInstance);
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &drawInstanceBuffer_.buffer),
        "vkCreateBuffer static model instances failed");
    try {
        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device_, drawInstanceBuffer_.buffer, &requirements);
        const VkMemoryAllocateInfo allocationInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(
                    device_, &allocationInfo, nullptr, &drawInstanceBuffer_.memory),
            "vkAllocateMemory static model instances failed");
        vkCheck(vkBindBufferMemory(
                    device_, drawInstanceBuffer_.buffer, drawInstanceBuffer_.memory, 0),
            "vkBindBufferMemory static model instances failed");
        vkCheck(vkMapMemory(device_, drawInstanceBuffer_.memory, 0, size, 0,
                    &drawInstanceBuffer_.mapped),
            "vkMapMemory static model instances failed");
        std::memset(drawInstanceBuffer_.mapped, 0, static_cast<std::size_t>(size));
    } catch (...) {
        destroyModelInstanceBuffer();
        throw;
    }
}

void VulkanModelResources::destroyModelInstanceBuffer()
{
    if (device_ && drawInstanceBuffer_.mapped) {
        vkUnmapMemory(device_, drawInstanceBuffer_.memory);
    }
    if (device_ && drawInstanceBuffer_.buffer) {
        vkDestroyBuffer(device_, drawInstanceBuffer_.buffer, nullptr);
    }
    if (device_ && drawInstanceBuffer_.memory) {
        vkFreeMemory(device_, drawInstanceBuffer_.memory, nullptr);
    }
    drawInstanceBuffer_ = {};
}

void VulkanModelResources::createMaterialBuffer()
{
    const VkDeviceSize size =
        static_cast<VkDeviceSize>(maxModelMaterials) * sizeof(GpuMaterial);
    const VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &materialBuffer_.buffer),
        "vkCreateBuffer model materials failed");
    try {
        VkMemoryRequirements requirements {};
        vkGetBufferMemoryRequirements(device_, materialBuffer_.buffer, &requirements);
        const VkMemoryAllocateInfo allocationInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(
                    device_, &allocationInfo, nullptr, &materialBuffer_.memory),
            "vkAllocateMemory model materials failed");
        vkCheck(vkBindBufferMemory(
                    device_, materialBuffer_.buffer, materialBuffer_.memory, 0),
            "vkBindBufferMemory model materials failed");
        vkCheck(vkMapMemory(device_, materialBuffer_.memory, 0, size, 0,
                    &materialBuffer_.mapped),
            "vkMapMemory model materials failed");
        // Entry zero is the material a draw lands on when nothing has been
        // published for it yet, so it has to read as an untextured white
        // surface rather than as whatever the allocation happened to hold.
        // Reserving it in materialStorage_ as well is what keeps the first
        // model to publish from being handed base zero and overwriting it.
        const GpuMaterial fallback {};
        for (uint32_t index = 0; index < maxModelMaterials; ++index) {
            std::memcpy(
                static_cast<std::byte*>(materialBuffer_.mapped) +
                    static_cast<std::size_t>(index) * sizeof(GpuMaterial),
                &fallback,
                sizeof(GpuMaterial));
        }
        materialStorage_.assign(1, fallback);
    } catch (...) {
        destroyMaterialBuffer();
        throw;
    }
}

void VulkanModelResources::destroyMaterialBuffer()
{
    if (device_ && materialBuffer_.mapped) {
        vkUnmapMemory(device_, materialBuffer_.memory);
    }
    if (device_ && materialBuffer_.buffer) {
        vkDestroyBuffer(device_, materialBuffer_.buffer, nullptr);
    }
    if (device_ && materialBuffer_.memory) {
        vkFreeMemory(device_, materialBuffer_.memory, nullptr);
    }
    materialBuffer_ = {};
}

uint32_t VulkanModelResources::writeMaterials(
    const std::vector<MeshMaterial>& materials)
{
    if (materials.empty()) {
        return 0;
    }
    // Never zero: createMaterialBuffer seeds the reserved fallback entry, so
    // the first real range starts at one.
    const std::size_t base = materialStorage_.size();
    if (!materialBuffer_.mapped || base == 0 ||
        base + materials.size() > maxModelMaterials) {
        throw std::runtime_error(
            "Material buffer is exhausted; raise maxModelMaterials");
    }
    materialStorage_.reserve(base + materials.size());
    for (const MeshMaterial& material : materials) {
        materialStorage_.push_back(gpuMaterialFrom(material));
    }
    std::memcpy(
        static_cast<std::byte*>(materialBuffer_.mapped) +
            base * sizeof(GpuMaterial),
        materialStorage_.data() + base,
        materials.size() * sizeof(GpuMaterial));
    return static_cast<uint32_t>(base);
}

void VulkanModelResources::repackMaterials()
{
    if (materialStorage_.empty()) {
        return;
    }
    // Rebuilt unconditionally. An earlier version skipped the upload when the
    // total had not shrunk, which was wrong: the loop below reorders the
    // ranges into slot order as well as closing gaps, so it has already
    // rewritten every materialBase by the time a size comparison could decide
    // to keep the old buffer. Bailing out there left the bases describing a
    // layout the buffer did not have.
    std::vector<GpuMaterial> packed;
    packed.reserve(materialStorage_.size());
    // The reserved fallback keeps index zero through every repack.
    packed.push_back(materialStorage_.front());
    for (ModelSlot& slot : models_) {
        if (slot.materialCount == 0) {
            continue;
        }
        const auto first = materialStorage_.begin() + slot.materialBase;
        const auto last = first + slot.materialCount;
        slot.materialBase = static_cast<uint32_t>(packed.size());
        packed.insert(packed.end(), first, last);
    }
    materialStorage_ = std::move(packed);
    if (materialBuffer_.mapped) {
        std::memcpy(
            materialBuffer_.mapped,
            materialStorage_.data(),
            materialStorage_.size() * sizeof(GpuMaterial));
    }
}

void VulkanModelResources::writeSkinningInstance(
    uint32_t frameIndex,
    uint32_t instanceSlot,
    const SkinnedMeshData& mesh,
    const AnimationController::SkinningRequest& request)
{
    if (!skinningBuffer_.mapped || instanceSlot >= maxSkinnedInstancesPerFrame ||
        request.toClip == nullptr) {
        throw std::runtime_error("GPU skinning palette write is invalid");
    }
    const SkinnedPoseMatrices pose = request.blended()
        ? sampleGltfSkinPoseBlended(
            mesh,
            *request.fromClip,
            request.fromTimeSeconds,
            *request.toClip,
            request.toTimeSeconds,
            request.blend)
        : sampleGltfSkinPose(mesh, *request.toClip, request.toTimeSeconds);
    const GpuSkinningInstance instance = makeGpuSkinningInstance(mesh, pose);
    const uint64_t linearIndex =
        static_cast<uint64_t>(frameIndex) * maxSkinnedInstancesPerFrame + instanceSlot;
    std::memcpy(
        static_cast<std::byte*>(skinningBuffer_.mapped) +
            linearIndex * sizeof(GpuSkinningInstance),
        &instance,
        sizeof(instance));
}

VulkanModelResources::TextureSampling VulkanModelResources::samplingFor(
    const AssetManifest::Texture& texture)
{
    return {
        .tiling = texture.tiling,
        .filter = texture.filter,
        .colorSpace = texture.colorSpace,
    };
}

void VulkanModelResources::createTextureBlocking(
    const ImageData& image,
    OwnedImage& textureImage,
    VkSampler& sampler,
    TextureSampling sampling)
{
    PendingTextureUpload upload;
    try {
        beginTextureUpload(image, textureImage, sampler, upload, sampling);
        vkCheck(
            vkWaitForFences(device_, 1, &upload.fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences initial texture upload failed");
        destroyTextureUpload(upload);
    } catch (...) {
        if (upload.submitted) {
            (void)vkDeviceWaitIdle(device_);
        }
        destroyTextureUpload(upload);
        destroyTexture(textureImage, sampler);
        throw;
    }
}

void VulkanModelResources::beginTextureUpload(
    const ImageData& image,
    OwnedImage& textureImage,
    VkSampler& sampler,
    PendingTextureUpload& upload,
    TextureSampling sampling)
{
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        throw std::runtime_error("Texture image contains no pixels");
    }
    // Colour textures decode sRGB on read; data textures (the splat weight
    // map) must not, or a painted 0.5 arrives at the shader as 0.21.
    const VkFormat textureFormat =
        sampling.colorSpace == TextureColorSpace::Linear
        ? VK_FORMAT_R8G8B8A8_UNORM
        : VK_FORMAT_R8G8B8A8_SRGB;
    VkFormatProperties formatProperties {};
    vkGetPhysicalDeviceFormatProperties(
        physicalDevice_, textureFormat, &formatProperties);
    const VkFormatFeatureFlags requiredBlitFeatures =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const bool supportsMipmaps = sampling.filter == TextureFilter::Linear &&
        (formatProperties.optimalTilingFeatures & requiredBlitFeatures) ==
            requiredBlitFeatures;
    textureImage.mipLevels = supportsMipmaps
        ? mipLevelCount(image.width, image.height)
        : 1U;
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureFormat,
        .extent = { image.width, image.height, 1 },
        .mipLevels = textureImage.mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &textureImage.image),
        "vkCreateImage model texture failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE, textureImage.image, "Model texture");

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device_, textureImage.image, &requirements);
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkCheck(vkAllocateMemory(device_, &allocationInfo, nullptr, &textureImage.memory),
        "vkAllocateMemory model texture failed");
    vulkanDebug::setObjectName(
        device_,
        VK_OBJECT_TYPE_DEVICE_MEMORY,
        textureImage.memory,
        "Model texture memory");
    vkCheck(vkBindImageMemory(device_, textureImage.image, textureImage.memory, 0),
        "vkBindImageMemory model texture failed");

    textureImage.view = createImageView(
        textureImage.image,
        textureFormat,
        VK_IMAGE_ASPECT_COLOR_BIT,
        textureImage.mipLevels);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE_VIEW, textureImage.view, "Model texture view");
    // Address mode and filtering are independent: the ground material layers
    // repeat and filter smoothly, the splat map clamps but still filters
    // smoothly (it spans the board once), and atlases do neither.
    const VkSamplerAddressMode addressMode = sampling.tiling
        ? VK_SAMPLER_ADDRESS_MODE_REPEAT
        : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const VkFilter filter = sampling.filter == TextureFilter::Linear
        ? VK_FILTER_LINEAR
        : VK_FILTER_NEAREST;
    const float anisotropy = supportsMipmaps &&
            sampling.filter == TextureFilter::Linear
        ? maxSamplerAnisotropy_
        : 1.0f;
    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,
        // Anisotropy only earns its cost where a mip chain exists and the
        // surface is viewed obliquely - the splatted ground being the case
        // that motivated it. Point-sampled atlases keep their crisp texels.
        .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = static_cast<float>(textureImage.mipLevels - 1U),
    };
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler),
        "vkCreateSampler model texture failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_SAMPLER, sampler, "Model texture sampler");

    recordTextureCopy(image, textureImage, upload);
}

void VulkanModelResources::recordTextureCopy(
    const ImageData& image,
    OwnedImage& textureImage,
    PendingTextureUpload& upload)
{
    const VkDeviceSize imageBytes = image.rgba.size();
    const auto staging = uploadRing_.reserve(imageBytes, 4);
    if (!staging) {
        throw std::runtime_error("Shared upload ring is full for texture upload");
    }
    upload.staging = *staging;
    uploadRing_.write(upload.staging, 0, image.rgba.data(), image.rgba.size());

    VkCommandBufferAllocateInfo commandBufferInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkCheck(vkAllocateCommandBuffers(
            device_, &commandBufferInfo, &upload.commandBuffer),
        "vkAllocateCommandBuffers texture upload failed");
    vulkanDebug::setObjectName(
        device_,
        VK_OBJECT_TYPE_COMMAND_BUFFER,
        upload.commandBuffer,
        "Model texture upload command buffer");
    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(upload.commandBuffer, &beginInfo),
        "vkBeginCommandBuffer texture upload failed");

    const VkImageSubresourceRange allMipLevels {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = textureImage.mipLevels,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vulkanResources::transitionImage(
        upload.commandBuffer,
        textureImage.image,
        allMipLevels,
        {},
        {
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        });

    VkBufferImageCopy copyRegion {
        .bufferOffset = upload.staging.offset,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { image.width, image.height, 1 },
    };
    vkCmdCopyBufferToImage(
        upload.commandBuffer,
        uploadRing_.buffer(),
        textureImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    int32_t sourceWidth = static_cast<int32_t>(image.width);
    int32_t sourceHeight = static_cast<int32_t>(image.height);
    for (uint32_t level = 1; level < textureImage.mipLevels; ++level) {
        const VkImageSubresourceRange sourceLevel {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = level - 1,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        vulkanResources::transitionImage(
            upload.commandBuffer,
            textureImage.image,
            sourceLevel,
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            });

        const int32_t destinationWidth = std::max(sourceWidth / 2, 1);
        const int32_t destinationHeight = std::max(sourceHeight / 2, 1);
        const VkImageBlit blit {
            .srcSubresource = {
                VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 },
            .srcOffsets = {
                VkOffset3D { 0, 0, 0 },
                VkOffset3D { sourceWidth, sourceHeight, 1 },
            },
            .dstSubresource = {
                VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
            .dstOffsets = {
                VkOffset3D { 0, 0, 0 },
                VkOffset3D { destinationWidth, destinationHeight, 1 },
            },
        };
        vkCmdBlitImage(
            upload.commandBuffer,
            textureImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            textureImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR);
        vulkanResources::transitionImage(
            upload.commandBuffer,
            textureImage.image,
            sourceLevel,
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            });
        sourceWidth = destinationWidth;
        sourceHeight = destinationHeight;
    }
    const VkImageSubresourceRange finalLevel {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = textureImage.mipLevels - 1,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vulkanResources::transitionImage(
        upload.commandBuffer,
        textureImage.image,
        finalLevel,
        {
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        },
        {
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
    vkCheck(vkEndCommandBuffer(upload.commandBuffer),
        "vkEndCommandBuffer texture upload failed");

    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    vkCheck(vkCreateFence(device_, &fenceInfo, nullptr, &upload.fence),
        "vkCreateFence texture upload failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_FENCE, upload.fence, "Model texture upload fence");

    VkCommandBufferSubmitInfo commandBufferSubmit {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = upload.commandBuffer,
    };
    VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmit,
    };
    vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, upload.fence),
        "vkQueueSubmit2 texture upload failed");
    uploadRing_.commit(upload.staging);
    upload.submitted = true;
}

bool VulkanModelResources::syncManifestTextures()
{
    if (manifest_ == nullptr ||
        textures_.size() >= manifest_->textures().size()) {
        return false;
    }
    if (manifest_->textures().size() > textureDescriptorCapacity_) {
        throw std::runtime_error(
            "Runtime manifest requires " +
            std::to_string(manifest_->textures().size()) +
            " texture descriptors, but the stable heap capacity is " +
            std::to_string(textureDescriptorCapacity_));
    }
    textures_.resize(manifest_->textures().size());
    return true;
}

bool VulkanModelResources::syncManifestModels()
{
    if (manifest_ == nullptr ||
        models_.size() >= manifest_->models().size()) {
        return false;
    }
    models_.resize(manifest_->models().size());
    return true;
}

VulkanModelResources::TextureUpdate VulkanModelResources::updateTexture(
    RenderTexture texture, const ImageData& image)
{
    if (texture.isNone() || texture.index() >= textures_.size()) {
        return {};
    }
    TextureSlot& slot = textures_[texture.index()];
    // Only a published texture has an image to write into. An unpublished one
    // will pick the painted map up from disk when it is first loaded.
    if (slot.state != LoadState::Ready ||
        slot.gpu.image.image == VK_NULL_HANDLE) {
        return {};
    }
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        return {};
    }

    // Frames in flight may still be sampling this texture. Painting happens
    // only in the editor and only when something actually changed, so a full
    // idle here is far simpler than tracking each sampled descriptor lifetime
    // and costs nothing during normal play.
    vkDeviceWaitIdle(device_);

    const bool sizeChanged =
        image.width != slot.gpu.width || image.height != slot.gpu.height;
    if (sizeChanged) {
        // A resized board needs a differently sized image. The old view and
        // sampler go with it, so descriptor sets pointing at them must be
        // rewritten - reported back rather than done here, because this class
        // does not own the descriptor sets.
        destroyTexture(slot.gpu.image, slot.gpu.sampler);
        createTextureBlocking(
            image,
            slot.gpu.image,
            slot.gpu.sampler,
            samplingFor(manifest_->textures()[texture.index()]));
        slot.gpu.width = image.width;
        slot.gpu.height = image.height;
        return { .updated = true, .descriptorsChanged = true };
    }

    PendingTextureUpload upload;
    try {
        // Reuses the existing image, view and sampler, so every descriptor
        // that already points at this texture stays valid and no descriptor
        // rewrite is needed.
        recordTextureCopy(image, slot.gpu.image, upload);
        vkCheck(
            vkWaitForFences(device_, 1, &upload.fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences painted texture update failed");
        destroyTextureUpload(upload);
    } catch (...) {
        if (upload.submitted) {
            (void)vkDeviceWaitIdle(device_);
        }
        destroyTextureUpload(upload);
        throw;
    }
    return { .updated = true, .descriptorsChanged = false };
}

void VulkanModelResources::destroyTextureUpload(
    PendingTextureUpload& upload)
{
    if (upload.staging.valid()) {
        if (upload.submitted) {
            uploadRing_.complete(upload.staging);
        } else {
            uploadRing_.abandon(upload.staging);
        }
    }
    if (upload.fence) {
        vkDestroyFence(device_, upload.fence, nullptr);
    }
    if (upload.commandBuffer) {
        vkFreeCommandBuffers(
            device_, commandPool_, 1, &upload.commandBuffer);
    }
    upload = {};
}

void VulkanModelResources::destroyTexture(
    OwnedImage& textureImage,
    VkSampler& sampler)
{
    if (sampler) {
        vkDestroySampler(device_, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    if (textureImage.view) {
        vkDestroyImageView(device_, textureImage.view, nullptr);
        textureImage.view = VK_NULL_HANDLE;
    }
    if (textureImage.image) {
        vkDestroyImage(device_, textureImage.image, nullptr);
        textureImage.image = VK_NULL_HANDLE;
    }
    if (textureImage.memory) {
        vkFreeMemory(device_, textureImage.memory, nullptr);
        textureImage.memory = VK_NULL_HANDLE;
    }
    textureImage.mipLevels = 1;
}

void VulkanModelResources::destroyMesh(GpuMesh& mesh)
{
    if (mesh.allocation.valid()) {
        geometryArena_.release(mesh.allocation);
    }
    mesh = {};
}

void VulkanModelResources::destroySkinnedMesh(GpuSkinnedMesh& mesh)
{
    if (mesh.allocation.valid()) {
        geometryArena_.release(mesh.allocation);
    }
    mesh = {};
}

const VulkanModelResources::GpuMesh& VulkanModelResources::gpuMeshForModel(
    RenderModel model) const
{
    if (model.isCube() ||
        model == manifest_->playerModel() ||
        model.index() >= models_.size()) {
        throw std::runtime_error("Render model does not have a static GPU mesh");
    }
    const ModelSlot& slot = models_[model.index()];
    if (slot.state != LoadState::Ready ||
        !slot.gpu.allocation.valid() ||
        !geometryArena_.vertexBuffer(slot.gpu.allocation) ||
        !geometryArena_.indexBuffer(slot.gpu.allocation)) {
        throw std::runtime_error("Render model mesh was used before it was ready");
    }
    return slot.gpu;
}

uint32_t VulkanModelResources::findMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("No suitable Vulkan memory type found for model resource");
}

VkImageView VulkanModelResources::createImageView(
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask,
    uint32_t mipLevels) const
{
    VkImageViewCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = { aspectMask, 0, mipLevels, 0, 1 },
    };
    VkImageView imageView = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(device_, &createInfo, nullptr, &imageView),
        "vkCreateImageView model texture failed");
    return imageView;
}

} // namespace sokoban
