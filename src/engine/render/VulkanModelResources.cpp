#include "engine/render/VulkanModelResources.hpp"

#include "engine/Log.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/render/TextureSourceLoader.hpp"
#include "engine/render/TextureUploadPlan.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>
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

VkSamplerAddressMode vulkanAddressMode(TextureAddressMode mode)
{
    switch (mode) {
    case TextureAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureAddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case TextureAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkFilter vulkanMagnificationFilter(TextureMagnificationFilter filter)
{
    return filter == TextureMagnificationFilter::Nearest
        ? VK_FILTER_NEAREST
        : VK_FILTER_LINEAR;
}

VkFilter vulkanMinificationFilter(TextureMinificationFilter filter)
{
    switch (filter) {
    case TextureMinificationFilter::Nearest:
    case TextureMinificationFilter::NearestMipmapNearest:
    case TextureMinificationFilter::NearestMipmapLinear:
        return VK_FILTER_NEAREST;
    case TextureMinificationFilter::Linear:
    case TextureMinificationFilter::LinearMipmapNearest:
    case TextureMinificationFilter::LinearMipmapLinear:
        return VK_FILTER_LINEAR;
    }
    return VK_FILTER_LINEAR;
}

VkSamplerMipmapMode vulkanMipmapMode(TextureMinificationFilter filter)
{
    return filter == TextureMinificationFilter::NearestMipmapLinear ||
            filter == TextureMinificationFilter::LinearMipmapLinear
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

bool supportsBc7Textures(VkPhysicalDevice physicalDevice)
{
    for (VkFormat format : {
             VK_FORMAT_BC7_UNORM_BLOCK,
             VK_FORMAT_BC7_SRGB_BLOCK,
         }) {
        VkFormatProperties properties {};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, format, &properties);
        if (!textureUploadPlan::supports(
                properties.optimalTilingFeatures,
                textureUploadPlan::compressedSamplingFeatures)) {
            return false;
        }
    }
    return true;
}

void remapBindingTextures(
    PrimitiveMaterialBinding& binding,
    uint32_t manifestTextureCount,
    uint32_t discoveredTextureBase)
{
    const auto remap = [=](std::optional<uint32_t>& index) {
        if (index) {
            *index = TextureDescriptorSpace::descriptorIndexFor(
                *index, manifestTextureCount, discoveredTextureBase);
        }
    };
    if (binding.bindBaseColorTexture) {
        binding.textureIndex = TextureDescriptorSpace::descriptorIndexFor(
            binding.textureIndex,
            manifestTextureCount,
            discoveredTextureBase);
    }
    remap(binding.normalTextureIndex);
    remap(binding.metallicRoughnessTextureIndex);
    remap(binding.emissiveTextureIndex);
    remap(binding.occlusionTextureIndex);
}

} // namespace

// A default Aabb is inverted, so the empty case needs no special handling:
// folding nothing into it leaves it invalid, and folding the first vertex in
// yields exactly that vertex.
Aabb VulkanModelResources::boundsOf(const std::vector<MeshVertex>& vertices)
{
    Aabb bounds;
    for (const MeshVertex& vertex : vertices) {
        bounds = expand(bounds, vertex.position);
    }
    return bounds;
}

Aabb VulkanModelResources::boundsForModel(RenderModel model) const
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
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::filesystem::path assetRoot,
    const AssetManifest& manifest,
    const RuntimeTextureCatalog& textureCatalog,
    uint32_t textureDescriptorCapacity,
    float maxSamplerAnisotropy,
    AssetLoadingBudget loadingBudget)
{
    destroy();
    scheduler_ = AssetLoadScheduler(loadingBudget);
    if (textureDescriptorCapacity == 0 ||
        textureCatalog.textures().size() > textureDescriptorCapacity ||
        textureCatalog.manifestTextureCount() != manifest.textures().size()) {
        throw std::runtime_error(
            "Runtime texture catalog exceeds the selected descriptor capacity");
    }
    physicalDevice_ = physicalDevice;
    supportsBc7_ = supportsBc7Textures(physicalDevice_);
    allocator_ = &allocator;
    maxSamplerAnisotropy_ = std::max(maxSamplerAnisotropy, 1.0f);
    textureDescriptorCapacity_ = textureDescriptorCapacity;
    device_ = device;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;
    assetRoot_ = std::move(assetRoot);
    manifest_ = &manifest;
    models_.resize(manifest.models().size());
    animations_.resize(manifest.animations().size());
    textures_.resize(textureDescriptorCapacity_);
    textureDefinitions_.resize(textureDescriptorCapacity_);
    textureSpace_.reset(
        textureDescriptorCapacity_,
        textureCatalog.manifestTextureCount(),
        textureCatalog.discoveredTextureCount());
    if (TextureDescriptorSpace::rangesOverlap(
            textureSpace_.manifestCount(), textureSpace_.discoveredBase())) {
        throw std::runtime_error(
            "Runtime texture catalog leaves no stable manifest descriptor range");
    }
    textureSpace_.reserveActive(textureCatalog.textures().size());
    for (uint32_t logicalIndex = 0;
         logicalIndex < textureCatalog.textures().size();
         ++logicalIndex) {
        const uint32_t descriptorIndex = textureCatalog.descriptorIndex(
            logicalIndex, textureDescriptorCapacity_);
        textureDefinitions_[descriptorIndex] =
            textureCatalog.textures()[logicalIndex];
        textureSpace_.markActive(descriptorIndex);
    }
    modelTextureDependencies_.resize(models_.size());
    modelMaterialBindings_.resize(models_.size());
    for (uint32_t modelIndex = 0; modelIndex < models_.size(); ++modelIndex) {
        const RuntimeModelTextures& catalogModel =
            textureCatalog.model(modelIndex);
        std::vector<uint32_t>& dependencies =
            modelTextureDependencies_[modelIndex];
        dependencies.reserve(catalogModel.requiredTextures.size());
        for (uint32_t logicalIndex : catalogModel.requiredTextures) {
            dependencies.push_back(textureCatalog.descriptorIndex(
                logicalIndex, textureDescriptorCapacity_));
        }
        modelMaterialBindings_[modelIndex] = catalogModel.primitiveMaterials;
        for (PrimitiveMaterialBinding& binding :
             modelMaterialBindings_[modelIndex]) {
            remapBindingTextures(
                binding,
                textureSpace_.manifestCount(),
                textureSpace_.discoveredBase());
        }
    }
    animationController_.configure(
        manifest.playerModel(), manifest.playerIdleAnimation());

    try {
        uploadRing_.create(*allocator_, device_);
        geometryArena_.create(
            *allocator_, device_, commandPool_, graphicsQueue_, uploadRing_);
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
            {
                .colorSpace = TextureColorSpace::Srgb,
                .wrapU = TextureAddressMode::ClampToEdge,
                .wrapV = TextureAddressMode::ClampToEdge,
                .magFilter = TextureMagnificationFilter::Nearest,
                .minFilter = TextureMinificationFilter::Nearest,
            });
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
        retiredTextures_.drainAll(
            [this](RetiredTextureResources& retired) {
                destroyTexture(retired.gpu.image, retired.gpu.sampler);
            });
        retiredModels_.drainAll(
            [this](RetiredModelResources& retired) {
                destroyMesh(retired.gpu);
                destroySkinnedMesh(retired.skinnedGpu);
            });
        destroySkinningBuffer();
        destroyModelInstanceBuffer();
        destroyMaterialBuffer();
        geometryArena_.destroy();
        uploadRing_.destroy();
    }

    models_.clear();
    animations_.clear();
    textures_.clear();
    textureDefinitions_.clear();
    modelTextureDependencies_.clear();
    modelMaterialBindings_.clear();
    materialStorage_.clear();
    materialRanges_.reset(0);
    fallbackTexture_ = {};
    animationController_.clear();
    manifest_ = nullptr;
    assetRoot_.clear();
    graphicsQueue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
    physicalDevice_ = VK_NULL_HANDLE;
    supportsBc7_ = false;
    textureDescriptorCapacity_ = 0;
    textureSpace_.clear();
    textureUploadSubmissions_ = 0;
    textureUploadCompletions_ = 0;
    scheduler_.clear();
    visibleRequestStamp_ = 0;
    modelResidency_.reset();
    textureResidency_.reset();
    residencyEvictions_ = 0;
    droppedDrawInstances_ = 0;
    droppedSkinningInstances_ = 0;
    residencyBudgetBlocks_ = 0;
    residencyOversizedBlocks_ = 0;
    residencyMipPlanBlocks_ = 0;
    residencyNoVictimBlocks_ = 0;
    residencyBudgetBlocked_ = false;
    textureDescriptorsDirty_ = false;
    retirementFrameMask_ = 0;
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
    for (uint32_t i = 0; i < textureSpace_.manifestCount(); ++i) {
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
    // Offline callers have no submitted render frames referencing residency
    // resources, so evictions from this blocking path can retire immediately.
    retirementFrameMask_ = 0;
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
        for (uint32_t textureIndex : textureSpace_.active()) {
            if (textures_[textureIndex].state == LoadState::Loading) {
                (void)publishTexture(textureIndex, true);
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

VulkanModelResources::PublicationResult VulkanModelResources::publishReadyAssets(
    std::size_t maxPublications,
    uint32_t pendingFrameMask)
{
    retirementFrameMask_ = pendingFrameMask;
    startQueuedAssets();
    std::size_t publications = 0;
    bool descriptorsChanged = false;
    const auto finish = [&] {
        const bool evictionChangedDescriptors =
            std::exchange(textureDescriptorsDirty_, false);
        return PublicationResult {
            .publications = publications,
            .descriptorsChanged =
                descriptorsChanged || evictionChangedDescriptors,
        };
    };

    for (uint32_t i = 0; i < animations_.size(); ++i) {
        if (publications >= maxPublications) {
            return finish();
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
            return finish();
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

    for (uint32_t i : textureSpace_.active()) {
        if (publications >= maxPublications) {
            return finish();
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
    return finish();
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

void VulkanModelResources::completeFrame(uint32_t frameIndex)
{
    retiredModels_.completeFrame(frameIndex);
    retiredTextures_.completeFrame(frameIndex);
    destroyCompletedResidencyRetirements();
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
    // Both states re-request, and the second is not redundant: the scheduler
    // raises an inactive entry's priority, so an already-queued asset that has
    // just become visible gets promoted by this call. Assigning Queued to a
    // slot that is already Queued is the no-op it looks like.
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        slot.state = LoadState::Queued;
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
    options.primitiveMaterials = modelMaterialBindings_.at(model.index());
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
    if (textureIndex >= textures_.size() ||
        textureIndex >= textureDefinitions_.size() ||
        !textureDefinitions_[textureIndex]) {
        throw std::runtime_error("Model material references an invalid texture index");
    }
    TextureSlot& slot = textures_[textureIndex];
    if (priority == AssetLoadPriority::Visible) {
        slot.lastRequested = visibleRequestStamp_;
    }
    // Both states re-request, and the second is not redundant: the scheduler
    // raises an inactive entry's priority, so an already-queued asset that has
    // just become visible gets promoted by this call. Assigning Queued to a
    // slot that is already Queued is the no-op it looks like.
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        slot.state = LoadState::Queued;
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

    if (textureIndex >= textureDefinitions_.size() ||
        !textureDefinitions_[textureIndex]) {
        throw std::logic_error("Started an undefined runtime texture slot");
    }
    const TextureSourceIdentity identity =
        textureDefinitions_[textureIndex]->identity;
    const std::filesystem::path assetRoot = assetRoot_;
    const bool supportsBc7 = supportsBc7_;
    slot.future = taskSystem().enqueue([assetRoot, identity, supportsBc7] {
        return loadPreparedTextureSource(assetRoot, identity, supportsBc7);
    });
    slot.state = LoadState::Loading;
}

std::filesystem::path VulkanModelResources::textureDiagnosticPath(
    std::size_t textureIndex) const
{
    if (textureIndex >= textureDefinitions_.size() ||
        !textureDefinitions_[textureIndex]) {
        return "texture descriptor " + std::to_string(textureIndex);
    }
    const RuntimeTextureDefinition& definition =
        *textureDefinitions_[textureIndex];
    if (const auto* external =
            std::get_if<ExternalTextureSource>(&definition.identity.source)) {
        return assetRoot_ / external->path;
    }
    return std::filesystem::path(definition.label);
}

void VulkanModelResources::queueAnimation(
    RenderAnimation animation,
    AssetLoadPriority priority)
{
    AnimationSlot& slot = animations_[animation.index()];
    if (priority == AssetLoadPriority::Visible) {
        slot.lastRequested = visibleRequestStamp_;
    }
    // Both states re-request, and the second is not redundant: the scheduler
    // raises an inactive entry's priority, so an already-queued asset that has
    // just become visible gets promoted by this call. Assigning Queued to a
    // slot that is already Queued is the no-op it looks like.
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        slot.state = LoadState::Queued;
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
    for (uint32_t textureIndex :
         modelTextureDependencies_.at(model.index())) {
        queueTexture(textureIndex, priority);
    }
    const AssetManifest::Model& definition = manifest_->model(model);
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

template <typename Slot>
VulkanModelResources::PublishGate VulkanModelResources::publishGate(
    const Slot& slot,
    const std::filesystem::path& path,
    const char* kind,
    bool wait) const
{
    // Uploading belongs here with Ready: its bytes are already charged and its
    // fence is already in flight, so there is nothing a second attempt could
    // usefully do.
    if (slot.state == LoadState::Ready || slot.state == LoadState::Uploading) {
        return PublishGate::Stop;
    }
    if (slot.state == LoadState::Failed) {
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, kind);
        }
        return PublishGate::Stop;
    }
    if (slot.state == LoadState::Unrequested ||
        slot.state == LoadState::Queued) {
        return PublishGate::Stop;
    }
    return PublishGate::Proceed;
}

template <typename Slot>
void VulkanModelResources::recordPublishFailure(
    Slot& slot,
    const std::filesystem::path& path,
    const char* kind,
    const char* phase,
    bool wait)
{
    slot.failure = std::current_exception();
    slot.state = LoadState::Failed;
    if (wait) {
        throwIfFailed(slot.state, slot.failure, path, kind);
    }
    log::error(log::Category::Assets)
        << "Background " << kind << " " << phase << " failed: "
        << path.string();
}

bool VulkanModelResources::publishModel(RenderModel model, bool wait)
{
    ModelSlot& slot = models_[model.index()];
    const AssetManifest::Model& definition = manifest_->model(model);
    const std::filesystem::path path = assetRoot_ / definition.path;
    if (publishGate(slot, path, "model", wait) == PublishGate::Stop) {
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
                slot.materialPolicy = modelMaterialPolicy(mesh.materials);
                slot.materialBase = writeMaterials(mesh.materials);
                slot.materialCount =
                    static_cast<uint32_t>(mesh.materials.size());
                slot.gpu = uploadMesh(mesh, slot.upload);
                slot.gpuBytes = bytes;
                modelResidency_.addResident(bytes);
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
                slot.materialPolicy = modelMaterialPolicy(
                    slot.skinnedSource->materials);
                slot.materialBase = writeMaterials(slot.skinnedSource->materials);
                slot.materialCount = static_cast<uint32_t>(
                    slot.skinnedSource->materials.size());
                slot.skinnedGpu = uploadSkinnedMesh(*slot.skinnedSource, slot.upload);
                slot.gpuBytes = bytes;
                modelResidency_.addResident(bytes);
                slot.state = LoadState::Uploading;
            }
            slot.prepared.reset();
        } catch (...) {
            // This range was never published to a frame and is safe to reuse
            // immediately when Vulkan publication fails.
            materialRanges_.release(slot.materialBase, slot.materialCount);
            slot.materialBase = 0;
            slot.materialCount = 0;
            slot.materialPolicy = {};
            recordPublishFailure(slot, path, "model", "publication", wait);
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
            // Not aabbFromMinMax: sorting the pair would repair an
            // inverted box rather than leave it invalid. The loader rejects
            // an empty skinned mesh, so these are already ordered.
            slot.bounds = Aabb {
                skinned.sourceMinimum,
                skinned.sourceMaximum,
            };
        }
        return publishModel(model, wait);
    } catch (...) {
        recordPublishFailure(slot, path, "model", "publication", wait);
    }
    return true;
}

bool VulkanModelResources::publishTexture(std::size_t textureIndex, bool wait)
{
    TextureSlot& slot = textures_.at(textureIndex);
    const std::filesystem::path path = textureDiagnosticPath(textureIndex);
    if (publishGate(slot, path, "texture", wait) == PublishGate::Stop) {
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
            recordPublishFailure(slot, path, "texture", "preparation", wait);
            return true;
        }
    }

    if (slot.state != LoadState::CpuReady || !slot.prepared) {
        return false;
    }

    try {
        const PreparedTextureSource& texture = *slot.prepared;
        const TextureInterpretation& interpretation =
            textureDefinitions_.at(textureIndex)->identity.interpretation;
        uint64_t bytes = textureBytes(texture, interpretation);
        std::optional<TextureMipResidencyPlan> mipPlan;
        if (const auto* compressed =
                std::get_if<CompressedTextureArtifact>(&texture)) {
            mipPlan = chooseTextureMipResidency(
                *compressed,
                texturePublicationCapacity(textureIndex));
            if (!mipPlan) {
                markResidencyBudgetBlocked(ResidencyBlock::NoMipTailFits);
                return false;
            }
            bytes = mipPlan->residentBytes;
        }
        if (!makeTextureResident(textureIndex, bytes)) {
            return false;
        }
        if (const auto* compressed =
                std::get_if<CompressedTextureArtifact>(&texture)) {
            beginTextureUpload(
                *compressed,
                mipPlan->sourceBaseMip,
                slot.gpu.image,
                slot.gpu.sampler,
                slot.upload,
                interpretation);
            slot.gpu.width = mipPlan->width;
            slot.gpu.height = mipPlan->height;
            slot.gpu.compressed = true;
            slot.fullQualityBytes = mipPlan->fullQualityBytes;
            slot.sourceMipLevels =
                static_cast<uint32_t>(compressed->mips.size());
            slot.residentBaseMip = mipPlan->sourceBaseMip;
        } else {
            const ImageData& image = std::get<ImageData>(texture);
            beginTextureUpload(
                image,
                slot.gpu.image,
                slot.gpu.sampler,
                slot.upload,
                interpretation);
            slot.gpu.width = image.width;
            slot.gpu.height = image.height;
            slot.gpu.compressed = false;
            slot.fullQualityBytes = bytes;
            slot.sourceMipLevels = slot.gpu.image.mipLevels;
            slot.residentBaseMip = 0;
        }
        slot.gpuBytes = bytes;
        textureResidency_.addResident(bytes);
        slot.prepared.reset();
        slot.state = LoadState::Uploading;
        ++textureUploadSubmissions_;
    } catch (...) {
        if (!slot.upload.submitted) {
            destroyTextureUpload(slot.upload);
            destroyTexture(slot.gpu.image, slot.gpu.sampler);
        }
        recordPublishFailure(slot, path, "texture", "publication", wait);
    }
    return true;
}

uint64_t VulkanModelResources::meshBytes(const MeshData& mesh)
{
    return static_cast<uint64_t>(sizeof(MeshVertex)) * mesh.vertices.size() +
        static_cast<uint64_t>(sizeof(uint32_t)) * mesh.indices.size();
}

uint64_t VulkanModelResources::textureBytes(
    const PreparedTextureSource& texture,
    const TextureInterpretation& interpretation)
{
    if (const auto* compressed =
            std::get_if<CompressedTextureArtifact>(&texture)) {
        return compressed->residentBytes();
    }
    const ImageData& image = std::get<ImageData>(texture);
    const uint64_t base = image.rgba.size();
    switch (interpretation.minFilter) {
    case TextureMinificationFilter::NearestMipmapNearest:
    case TextureMinificationFilter::LinearMipmapNearest:
    case TextureMinificationFilter::NearestMipmapLinear:
    case TextureMinificationFilter::LinearMipmapLinear:
        // A complete mip pyramid converges to four thirds of the base level.
        return base + base / 3U;
    case TextureMinificationFilter::Nearest:
    case TextureMinificationFilter::Linear:
        return base;
    }
    return base;
}

void VulkanModelResources::markResidencyBudgetBlocked(ResidencyBlock reason)
{
    residencyBudgetBlocked_ = true;
    ++residencyBudgetBlocks_;
    switch (reason) {
    case ResidencyBlock::AssetLargerThanBudget:
        ++residencyOversizedBlocks_;
        break;
    case ResidencyBlock::NoMipTailFits:
        ++residencyMipPlanBlocks_;
        break;
    case ResidencyBlock::NothingEvictable:
        ++residencyNoVictimBlocks_;
        break;
    }
}

template <typename Slot, typename Retire>
bool VulkanModelResources::makeResident(
    ResidencyBudget& budget,
    std::vector<Slot>& slots,
    std::size_t protectedIndex,
    uint64_t requiredBytes,
    uint64_t limitBytes,
    Retire retire)
{
    destroyCompletedResidencyRetirements();
    if (requiredBytes > limitBytes) {
        markResidencyBudgetBlocked(ResidencyBlock::AssetLargerThanBudget);
        return false;
    }
    if (budget.fits(requiredBytes, limitBytes)) {
        return true;
    }
    if (budget.retirementPending()) {
        // A previous publication already selected enough victims for its own
        // retry. Do not let other CPU-ready slots cascade into additional
        // evictions while the first fence-owned retirement is still pending.
        residencyBudgetBlocked_ = false;
        return false;
    }

    while (budget.needsEviction(requiredBytes, limitBytes)) {
        const std::optional<std::size_t> victim = chooseResidencyVictim(
            slots,
            protectedIndex,
            visibleRequestStamp_,
            [](const Slot& slot) { return slot.state == LoadState::Ready; });
        if (!victim) {
            markResidencyBudgetBlocked(ResidencyBlock::NothingEvictable);
            return false;
        }
        // retire() raises the budget's retiring total, which is what the
        // condition above reads; there is no second tally to keep in step.
        retire(slots[*victim]);
    }
    destroyCompletedResidencyRetirements();
    residencyBudgetBlocked_ = false;
    // Retired allocations remain part of the physical residency total until
    // every referencing frame fence completes. Keep this CPU-ready asset for
    // a later publication instead of oversubscribing the hard budget.
    return budget.fits(requiredBytes, limitBytes);
}

bool VulkanModelResources::makeModelResident(
    RenderModel protectedModel,
    uint64_t requiredBytes)
{
    return makeResident(
        modelResidency_,
        models_,
        protectedModel.index(),
        requiredBytes,
        scheduler_.budget().modelResidencyBytes,
        [this](ModelSlot& slot) { retireModel(slot); });
}

bool VulkanModelResources::makeTextureResident(
    std::size_t protectedTexture,
    uint64_t requiredBytes)
{
    return makeResident(
        textureResidency_,
        textures_,
        protectedTexture,
        requiredBytes,
        scheduler_.budget().textureResidencyBytes,
        [this](TextureSlot& slot) { retireTexture(slot); });
}

uint64_t VulkanModelResources::texturePublicationCapacity(
    std::size_t protectedTexture) const
{
    return evictableCapacity(
        textures_,
        protectedTexture,
        visibleRequestStamp_,
        [](const TextureSlot& slot) { return slot.state == LoadState::Ready; },
        textureResidency_,
        scheduler_.budget().textureResidencyBytes);
}

void VulkanModelResources::retireModel(ModelSlot& slot)
{
    modelResidency_.beginRetiring(slot.gpuBytes);
    retiredModels_.retire({
        .gpu = std::exchange(slot.gpu, {}),
        .skinnedGpu = std::exchange(slot.skinnedGpu, {}),
        .materialBase = slot.materialBase,
        .materialCount = slot.materialCount,
        .gpuBytes = slot.gpuBytes,
    }, retirementFrameMask_);
    slot = {};
    ++residencyEvictions_;
}

void VulkanModelResources::retireTexture(TextureSlot& slot)
{
    textureResidency_.beginRetiring(slot.gpuBytes);
    retiredTextures_.retire({
        .gpu = std::exchange(slot.gpu, {}),
        .gpuBytes = slot.gpuBytes,
    }, retirementFrameMask_);
    slot = {};
    textureDescriptorsDirty_ = true;
    ++residencyEvictions_;
}

void VulkanModelResources::destroyCompletedResidencyRetirements()
{
    retiredModels_.drainCompleted(
        [this](RetiredModelResources& retired) {
            destroyMesh(retired.gpu);
            destroySkinnedMesh(retired.skinnedGpu);
            materialRanges_.release(
                retired.materialBase, retired.materialCount);
            modelResidency_.finishRetiring(retired.gpuBytes);
        });
    retiredTextures_.drainCompleted(
        [this](RetiredTextureResources& retired) {
            destroyTexture(retired.gpu.image, retired.gpu.sampler);
            textureResidency_.finishRetiring(retired.gpuBytes);
        });
}

bool VulkanModelResources::publishAnimation(RenderAnimation animation, bool wait)
{
    AnimationSlot& slot = animations_[animation.index()];
    const AssetManifest::Animation& definition = manifest_->animation(animation);
    const std::filesystem::path path = assetRoot_ / definition.path;
    if (publishGate(slot, path, "animation", wait) == PublishGate::Stop) {
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
        recordPublishFailure(slot, path, "animation", "publication", wait);
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
    for (uint32_t i = 0; i < textureSpace_.manifestCount(); ++i) {
        result[i] = requirements.contains(RenderTexture { i + 1 });
    }
    for (uint32_t i = 0; i < models_.size(); ++i) {
        const RenderModel model { i + 1 };
        if (!requirements.contains(model)) {
            continue;
        }
        for (uint32_t textureIndex : modelTextureDependencies_[i]) {
            result.at(textureIndex) = true;
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
                // Same reasoning as the draw-instance buffer: more skinned
                // actors on screen than the palette holds is content, not a
                // fault. Leave the instance unregistered and drop the entry -
                // the recorder already skips an instance whose pose has not
                // been published, which is exactly the state this leaves it in.
                skinnedInstances_.erase(instance);
                ++droppedSkinningInstances_;
                continue;
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
    // An invalid frame index or an unmapped buffer is a programming error and
    // stays fatal. Running out of entries is not: that is the level being
    // bigger than the buffer was sized for.
    if (frameIndex >= gpuSkinningFrameCount || !drawInstanceBuffer_.mapped()) {
        throw std::runtime_error("Draw instance buffer is not mapped");
    }
    if (drawInstanceCount_ >= drawInstanceDiscardSlot) {
        ++droppedDrawInstances_;
        return frameIndex * maxDrawInstancesPerFrame + drawInstanceDiscardSlot;
    }
    const uint32_t result = frameIndex * maxDrawInstancesPerFrame +
        drawInstanceCount_++;
    std::memcpy(
        static_cast<std::byte*>(drawInstanceBuffer_.mapped()) +
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
    const ModelMaterialPolicy policy = model.index() < models_.size()
        ? models_[model.index()].materialPolicy
        : ModelMaterialPolicy {};
    return {
        .mode = definition.materialMode,
        .textureIndex = definition.textureIndex,
        .materialBase = materialBase,
        .policy = policy,
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
    return skinningBuffer_.view(
        static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
        maxSkinnedInstancesPerFrame * sizeof(GpuSkinningInstance));
}

VulkanModelResources::DrawInstanceBufferView
VulkanModelResources::drawInstanceBuffer() const
{
    return drawInstanceBuffer_.view(
        static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
        maxDrawInstancesPerFrame * sizeof(GpuDrawInstance));
}

VulkanModelResources::MaterialBufferView
VulkanModelResources::materialBuffer() const
{
    return materialBuffer_.view(
        static_cast<VkDeviceSize>(maxModelMaterials) * sizeof(GpuMaterial));
}

VulkanModelResources::LoadingStats VulkanModelResources::loadingStats() const
{
    LoadingStats result {
        .totalModels = static_cast<uint32_t>(models_.size()),
        .totalTextures =
            static_cast<uint32_t>(textureSpace_.active().size()),
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
    for (uint32_t textureIndex : textureSpace_.active()) {
        const TextureSlot& texture = textures_[textureIndex];
        countState(texture.state, result.loadedTextures, result.pendingTextures);
        result.requestedAssets += texture.state != LoadState::Unrequested;
        if (texture.state == LoadState::Uploading) {
            ++result.uploadingTextures;
        }
        if ((texture.state == LoadState::Ready ||
                texture.state == LoadState::Uploading) &&
            texture.sourceMipLevels != 0) {
            result.availableTextureMipLevels += texture.sourceMipLevels;
            result.residentTextureMipLevels +=
                texture.sourceMipLevels - texture.residentBaseMip;
            if (texture.residentBaseMip != 0) {
                ++result.mipDegradedTextures;
                result.mipOmittedBytes +=
                    texture.fullQualityBytes - texture.gpuBytes;
            }
        }
    }
    for (const AnimationSlot& animation : animations_) {
        countState(animation.state, result.loadedAnimations, result.pendingAnimations);
        result.requestedAssets += animation.state != LoadState::Unrequested;
    }
    result.queuedAssets = static_cast<uint32_t>(scheduler_.queuedCount());
    result.activeCpuJobs = static_cast<uint32_t>(scheduler_.activeCount());
    result.cancelledPrefetches = scheduler_.cancelledPrefetchCount();
    result.modelResidencyBytes = modelResidency_.resident();
    result.textureResidencyBytes = textureResidency_.resident();
    result.modelResidencyPeakBytes = modelResidency_.peak();
    result.textureResidencyPeakBytes = textureResidency_.peak();
    result.modelResidencyBudgetBytes = scheduler_.budget().modelResidencyBytes;
    result.textureResidencyBudgetBytes = scheduler_.budget().textureResidencyBytes;
    result.residencyEvictions = residencyEvictions_;
    result.droppedDrawInstances = droppedDrawInstances_;
    result.droppedSkinningInstances = droppedSkinningInstances_;
    result.residencyBudgetBlocks = residencyBudgetBlocks_;
    result.residencyOversizedBlocks = residencyOversizedBlocks_;
    result.residencyMipPlanBlocks = residencyMipPlanBlocks_;
    result.residencyNoVictimBlocks = residencyNoVictimBlocks_;
    result.retiringModels = static_cast<uint32_t>(retiredModels_.size());
    result.retiringTextures = static_cast<uint32_t>(retiredTextures_.size());
    result.retiringModelBytes = modelResidency_.retiring();
    result.retiringTextureBytes = textureResidency_.retiring();
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
    try {
        skinningBuffer_.create(
            *allocator_,
            size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "GPU skinning palette");
        std::memset(skinningBuffer_.mapped(), 0, static_cast<std::size_t>(size));
    } catch (...) {
        destroySkinningBuffer();
        throw;
    }
}

void VulkanModelResources::destroySkinningBuffer()
{
    skinningBuffer_.destroy(allocator_);
}

void VulkanModelResources::createModelInstanceBuffer()
{
    const VkDeviceSize size = static_cast<VkDeviceSize>(gpuSkinningFrameCount) *
        maxDrawInstancesPerFrame * sizeof(GpuDrawInstance);
    try {
        drawInstanceBuffer_.create(
            *allocator_,
            size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "Static model instances");
        std::memset(
            drawInstanceBuffer_.mapped(), 0, static_cast<std::size_t>(size));
    } catch (...) {
        destroyModelInstanceBuffer();
        throw;
    }
}

void VulkanModelResources::destroyModelInstanceBuffer()
{
    drawInstanceBuffer_.destroy(allocator_);
}

void VulkanModelResources::createMaterialBuffer()
{
    const VkDeviceSize size =
        static_cast<VkDeviceSize>(maxModelMaterials) * sizeof(GpuMaterial);
    try {
        materialBuffer_.create(
            *allocator_, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "Model materials");
        // Entry zero is the material a draw lands on when nothing has been
        // published for it yet, so it has to read as an untextured white
        // surface rather than as whatever the allocation happened to hold.
        // Reserving it with the allocator is what keeps the first model to
        // publish from being handed base zero and overwriting it.
        const GpuMaterial fallback {};
        for (uint32_t index = 0; index < maxModelMaterials; ++index) {
            std::memcpy(
                static_cast<std::byte*>(materialBuffer_.mapped()) +
                    static_cast<std::size_t>(index) * sizeof(GpuMaterial),
                &fallback,
                sizeof(GpuMaterial));
        }
        materialRanges_.reset(1);
        materialStorage_.assign(materialRanges_.highWaterMark(), fallback);
    } catch (...) {
        destroyMaterialBuffer();
        throw;
    }
}

void VulkanModelResources::destroyMaterialBuffer()
{
    materialBuffer_.destroy(allocator_);
}

uint32_t VulkanModelResources::writeMaterials(
    const std::vector<MeshMaterial>& materials)
{
    if (materials.empty()) {
        return 0;
    }
    if (!materialBuffer_.mapped()) {
        throw std::runtime_error("Material buffer is not mapped");
    }

    const std::optional<uint32_t> allocated = materialRanges_.allocate(
        static_cast<uint32_t>(materials.size()), maxModelMaterials);
    if (!allocated) {
        throw std::runtime_error(
            "Material buffer is exhausted; raise maxModelMaterials");
    }
    const std::size_t base = *allocated;
    materialStorage_.resize(materialRanges_.highWaterMark());
    for (std::size_t index = 0; index < materials.size(); ++index) {
        materialStorage_[base + index] = gpuMaterialFrom(materials[index]);
    }
    std::memcpy(
        static_cast<std::byte*>(materialBuffer_.mapped()) +
            base * sizeof(GpuMaterial),
        materialStorage_.data() + base,
        materials.size() * sizeof(GpuMaterial));
    return static_cast<uint32_t>(base);
}

void VulkanModelResources::writeSkinningInstance(
    uint32_t frameIndex,
    uint32_t instanceSlot,
    const SkinnedMeshData& mesh,
    const AnimationController::SkinningRequest& request)
{
    if (!skinningBuffer_.mapped() || instanceSlot >= maxSkinnedInstancesPerFrame ||
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
        static_cast<std::byte*>(skinningBuffer_.mapped()) +
            linearIndex * sizeof(GpuSkinningInstance),
        &instance,
        sizeof(instance));
}

void VulkanModelResources::createTextureBlocking(
    const ImageData& image,
    OwnedImage& textureImage,
    VkSampler& sampler,
    TextureInterpretation sampling)
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
    TextureInterpretation sampling)
{
    if (!textureUploadPlan::uncompressedSourceIsUsable(
            image.width, image.height, !image.rgba.empty())) {
        throw std::runtime_error("Texture image contains no pixels");
    }
    const VkFormat textureFormat =
        textureUploadPlan::uncompressedFormat(sampling.colorSpace);
    VkFormatProperties formatProperties {};
    vkGetPhysicalDeviceFormatProperties(
        physicalDevice_, textureFormat, &formatProperties);
    // Held because it also gates anisotropy below, where it is not the same
    // question as "more than one level" - see the note on generatesMipmaps.
    const bool supportsMipmaps = textureUploadPlan::generatesMipmaps(
        sampling.minFilter, formatProperties.optimalTilingFeatures);
    textureImage.mipLevels = textureUploadPlan::uncompressedMipLevels(
        image.width,
        image.height,
        sampling.minFilter,
        formatProperties.optimalTilingFeatures);
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
    allocator_->createDeviceImage(
        imageInfo,
        textureImage.image,
        textureImage.allocation,
        "Model texture");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE, textureImage.image, "Model texture");

    textureImage.view = createImageView(
        textureImage.image,
        textureFormat,
        VK_IMAGE_ASPECT_COLOR_BIT,
        textureImage.mipLevels);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE_VIEW, textureImage.view, "Model texture view");
    const VkFilter minFilter = vulkanMinificationFilter(sampling.minFilter);
    const float anisotropy = supportsMipmaps && minFilter == VK_FILTER_LINEAR
        ? maxSamplerAnisotropy_
        : 1.0f;
    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = vulkanMagnificationFilter(sampling.magFilter),
        .minFilter = minFilter,
        .mipmapMode = vulkanMipmapMode(sampling.minFilter),
        .addressModeU = vulkanAddressMode(sampling.wrapU),
        .addressModeV = vulkanAddressMode(sampling.wrapV),
        // glTF authors U and V only; there is no third axis to author. Every
        // other sampler in the renderer clamps W for the same reason, and
        // copying V here read as an unfinished line rather than a decision.
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
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

void VulkanModelResources::beginTextureUpload(
    const CompressedTextureArtifact& texture,
    uint32_t sourceBaseMip,
    OwnedImage& textureImage,
    VkSampler& sampler,
    PendingTextureUpload& upload,
    TextureInterpretation sampling)
{
    if (!textureUploadPlan::compressedSourceIsUsable(
            texture.width,
            texture.height,
            texture.mips.size(),
            sourceBaseMip)) {
        throw std::runtime_error("Compressed texture contains no mip data");
    }
    const VkFormat textureFormat =
        textureUploadPlan::compressedFormat(texture.format);
    if (!textureUploadPlan::colorSpaceAgrees(
            textureFormat, sampling.colorSpace)) {
        throw std::runtime_error(
            "Compressed texture format disagrees with source colour space");
    }
    const CompressedTextureMip& residentBase = texture.mips[sourceBaseMip];
    textureImage.mipLevels = textureUploadPlan::compressedMipLevels(
        texture.mips.size(), sourceBaseMip);
    const VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = textureFormat,
        .extent = { residentBase.width, residentBase.height, 1 },
        .mipLevels = textureImage.mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    allocator_->createDeviceImage(
        imageInfo,
        textureImage.image,
        textureImage.allocation,
        "BC7 model texture");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE, textureImage.image,
        "BC7 model texture");
    textureImage.view = createImageView(
        textureImage.image,
        textureFormat,
        VK_IMAGE_ASPECT_COLOR_BIT,
        textureImage.mipLevels);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE_VIEW, textureImage.view,
        "BC7 model texture view");

    const VkFilter minFilter = vulkanMinificationFilter(sampling.minFilter);
    const float anisotropy = textureImage.mipLevels > 1U &&
            minFilter == VK_FILTER_LINEAR
        ? maxSamplerAnisotropy_
        : 1.0f;
    const VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = vulkanMagnificationFilter(sampling.magFilter),
        .minFilter = minFilter,
        .mipmapMode = vulkanMipmapMode(sampling.minFilter),
        .addressModeU = vulkanAddressMode(sampling.wrapU),
        .addressModeV = vulkanAddressMode(sampling.wrapV),
        // glTF authors U and V only; there is no third axis to author. Every
        // other sampler in the renderer clamps W for the same reason, and
        // copying V here read as an unfinished line rather than a decision.
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = static_cast<float>(textureImage.mipLevels - 1U),
    };
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler),
        "vkCreateSampler BC7 model texture failed");
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_SAMPLER, sampler,
        "BC7 model texture sampler");
    recordTextureCopy(texture, sourceBaseMip, textureImage, upload);
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

    upload.commandBuffer = vulkanResources::beginOneShotCommands(
        device_,
        commandPool_,
        "texture upload",
        "Model texture upload command buffer");

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
    upload.fence = vulkanResources::submitOneShotCommands(
        device_,
        graphicsQueue_,
        upload.commandBuffer,
        "texture upload",
        "Model texture upload fence");
    uploadRing_.commit(upload.staging);
    upload.submitted = true;
}

void VulkanModelResources::recordTextureCopy(
    const CompressedTextureArtifact& texture,
    uint32_t sourceBaseMip,
    OwnedImage& textureImage,
    PendingTextureUpload& upload)
{
    std::vector<VkDeviceSize> levelOffsets;
    levelOffsets.reserve(textureImage.mipLevels);
    VkDeviceSize uploadBytes = 0;
    for (uint32_t sourceLevel = sourceBaseMip;
         sourceLevel < texture.mips.size();
         ++sourceLevel) {
        const CompressedTextureMip& mip = texture.mips[sourceLevel];
        uploadBytes = (uploadBytes + 15U) & ~VkDeviceSize { 15U };
        levelOffsets.push_back(uploadBytes);
        uploadBytes += mip.bytes.size();
    }
    const auto staging = uploadRing_.reserve(uploadBytes, 16);
    if (!staging) {
        throw std::runtime_error(
            "Shared upload ring is full for compressed texture upload");
    }
    upload.staging = *staging;
    std::vector<VkBufferImageCopy> copyRegions;
    copyRegions.reserve(textureImage.mipLevels);
    for (uint32_t level = 0; level < textureImage.mipLevels; ++level) {
        const CompressedTextureMip& mip =
            texture.mips[sourceBaseMip + level];
        uploadRing_.write(
            upload.staging,
            levelOffsets[level],
            mip.bytes.data(),
            mip.bytes.size());
        copyRegions.push_back({
            .bufferOffset = upload.staging.offset + levelOffsets[level],
            .imageSubresource = {
                VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
            .imageExtent = { mip.width, mip.height, 1 },
        });
    }

    upload.commandBuffer = vulkanResources::beginOneShotCommands(
        device_,
        commandPool_,
        "compressed texture upload",
        "BC7 texture upload command buffer");

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
    vkCmdCopyBufferToImage(
        upload.commandBuffer,
        uploadRing_.buffer(),
        textureImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(copyRegions.size()),
        copyRegions.data());
    vulkanResources::transitionImage(
        upload.commandBuffer,
        textureImage.image,
        allMipLevels,
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
    upload.fence = vulkanResources::submitOneShotCommands(
        device_,
        graphicsQueue_,
        upload.commandBuffer,
        "compressed texture upload",
        "BC7 texture upload fence");
    uploadRing_.commit(upload.staging);
    upload.submitted = true;
}

bool VulkanModelResources::syncManifestTextures()
{
    if (manifest_ == nullptr ||
        textureSpace_.manifestCount() >= manifest_->textures().size()) {
        return false;
    }
    const uint32_t wanted =
        static_cast<uint32_t>(manifest_->textures().size());
    if (!textureSpace_.manifestCanHold(wanted)) {
        throw std::runtime_error(
            "Runtime manifest requires " +
            std::to_string(manifest_->textures().size()) +
            " stable low texture descriptors, but discovered glTF textures "
            "begin at descriptor " +
            std::to_string(textureSpace_.discoveredBase()));
    }
    for (uint32_t index = textureSpace_.manifestCount(); index < wanted;
         ++index) {
        textureDefinitions_[index] = runtimeTextureDefinitionFor(
            manifest_->textures()[index]);
    }
    textureSpace_.growManifestRange(wanted);
    return true;
}

bool VulkanModelResources::syncManifestModels()
{
    if (manifest_ == nullptr ||
        models_.size() >= manifest_->models().size()) {
        return false;
    }
    const std::size_t previousSize = models_.size();
    models_.resize(manifest_->models().size());
    modelTextureDependencies_.resize(models_.size());
    modelMaterialBindings_.resize(models_.size());
    for (std::size_t modelIndex = previousSize;
         modelIndex < models_.size();
         ++modelIndex) {
        const AssetManifest::Model& definition =
            manifest_->models()[modelIndex];
        // Both of these are logical texture indices and have to be mapped into
        // the descriptor heap, exactly as create() does for the startup path.
        // The mapping is the identity for a manifest texture, which is all an
        // editor-appended model can reference today - so this changes nothing
        // now, and stops the path depending on an invariant stated nowhere.
        // syncManifestTextures() runs immediately before this (ApplicationTools),
        // so the manifest range already covers every index seen here.
        //
        // The bindings carry base colour only. That is not an omission: a
        // manifest primitive material has a texture and a scroll flag and
        // nothing else. Normal, metallic-roughness, emissive and occlusion
        // handles come from glTF discovery, which builds the runtime catalog at
        // startup and has no equivalent for a model appended later.
        if (definition.materialMode == ModelMaterialMode::SingleTexture) {
            modelTextureDependencies_[modelIndex].push_back(
                textureSpace_.descriptorIndexFor(definition.textureIndex));
        } else if (
            definition.materialMode == ModelMaterialMode::PrimitiveMaterials) {
            for (const AssetManifest::Model::PrimitiveMaterial& material :
                 definition.primitiveMaterials) {
                modelTextureDependencies_[modelIndex].push_back(
                    textureSpace_.descriptorIndexFor(material.textureIndex));
                modelMaterialBindings_[modelIndex].push_back({
                    .textureIndex = material.textureIndex,
                    .flags = material.scrollV
                        ? PrimitiveMaterialScrollV
                        : PrimitiveMaterialNone,
                });
                // The same call the startup path makes, so the two cannot
                // drift: if a map handle is ever added here it is remapped too.
                remapBindingTextures(
                    modelMaterialBindings_[modelIndex].back(),
                    textureSpace_.manifestCount(),
                    textureSpace_.discoveredBase());
            }
        }
    }
    return true;
}

VulkanModelResources::TextureUpdate VulkanModelResources::updateTexture(
    RenderTexture texture, const ImageData& image)
{
    if (texture.isNone() ||
        !textureSpace_.isManifestTexture(
            static_cast<uint32_t>(texture.index()))) {
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
    //
    // Checked rather than discarded, unlike the two waits in the catch blocks
    // below and the one in VulkanDeviceContext: this is an ordinary editor
    // path with a caller that can take an exception, and continuing past a
    // failed wait would rewrite an image the GPU is still reading.
    vkCheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle", "texture repaint");

    const bool sizeChanged =
        image.width != slot.gpu.width || image.height != slot.gpu.height;
    if (sizeChanged || slot.gpu.compressed) {
        // A resized board needs a differently sized image. The old view and
        // sampler go with it, so descriptor sets pointing at them must be
        // rewritten - reported back rather than done here, because this class
        // does not own the descriptor sets.
        destroyTexture(slot.gpu.image, slot.gpu.sampler);
        createTextureBlocking(
            image,
            slot.gpu.image,
            slot.gpu.sampler,
            textureDefinitions_[texture.index()]->identity.interpretation);
        slot.gpu.width = image.width;
        slot.gpu.height = image.height;
        slot.gpu.compressed = false;
        const uint64_t previousBytes = slot.gpuBytes;
        slot.gpuBytes = textureBytes(
            PreparedTextureSource { image },
            textureDefinitions_[texture.index()]->identity.interpretation);
        slot.fullQualityBytes = slot.gpuBytes;
        slot.sourceMipLevels = slot.gpu.image.mipLevels;
        slot.residentBaseMip = 0;
        textureResidency_.replaceResident(previousBytes, slot.gpuBytes);
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
    allocator_->destroyImage(textureImage.image, textureImage.allocation);
    textureImage = {};
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
    // Skinned meshes live in skinnedGpu and are reached through meshForTile's
    // other branch. Ask what the model's geometry is rather than whether it is
    // the player: the player was the only skinned model when this was written,
    // and the manifest has carried more than one for a while now.
    if (model.isCube() || model.index() >= models_.size() ||
        modelUsesGpuSkinning(model)) {
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
