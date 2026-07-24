#include "engine/render/VulkanModelResources.hpp"

#include "engine/Log.hpp"
#include "engine/TaskSystem.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace sokoban {
namespace {

void vkCheck(VkResult result, const char* message)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(result) + ")");
    }
}

uint32_t animationIndexFromUserNumber(uint32_t animationNumber)
{
    return animationNumber == 0 ? 0 : animationNumber - 1;
}

template <typename Result>
bool futureReady(std::future<Result>& future)
{
    return future.valid() &&
        future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

} // namespace

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
    const AssetManifest& manifest)
{
    destroy();
    physicalDevice_ = physicalDevice;
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
            fallbackTexture_.sampler);
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanModelResources::destroy()
{
    if (device_) {
        std::vector<VkFence> uploadFences;
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

        skinnedMeshUpdater_.destroy();
        for (auto texture = textures_.rbegin(); texture != textures_.rend(); ++texture) {
            destroyTextureUpload(texture->upload);
            destroyTexture(texture->gpu.image, texture->gpu.sampler);
        }
        destroyTexture(fallbackTexture_.image, fallbackTexture_.sampler);
        for (auto model = models_.rbegin(); model != models_.rend(); ++model) {
            destroyMesh(model->gpu);
        }
    }

    models_.clear();
    animations_.clear();
    textures_.clear();
    fallbackTexture_ = {};
    animationController_.clear();
    manifest_ = nullptr;
    assetRoot_.clear();
    graphicsQueue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    textureUploadSubmissions_ = 0;
    textureUploadCompletions_ = 0;
}

void VulkanModelResources::requestAssets(const RenderAssetRequirements& requirements)
{
    for (uint32_t i = 0; i < models_.size(); ++i) {
        const RenderModel model { i + 1 };
        if (requirements.contains(model)) {
            requestModel(model);
        }
    }
    for (uint32_t i = 0; i < animations_.size(); ++i) {
        const RenderAnimation animation { i + 1 };
        if (requirements.contains(animation)) {
            requestAnimation(animation);
        }
    }
}

bool VulkanModelResources::ensureAssets(const RenderAssetRequirements& requirements)
{
    requestAssets(requirements);

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

    bool descriptorsChanged = false;
    const std::vector<bool> textureRequirements = requiredTextures(requirements);
    for (std::size_t i = 0; i < textureRequirements.size(); ++i) {
        if (!textureRequirements[i]) {
            continue;
        }
        const bool wasPublished = textures_[i].gpu.image.view != VK_NULL_HANDLE;
        (void)publishTexture(i, true);
        descriptorsChanged = descriptorsChanged ||
            (!wasPublished && textures_[i].gpu.image.view != VK_NULL_HANDLE);
    }

    if (!assetsReady(requirements)) {
        throw std::runtime_error("Required render assets did not reach the ready state");
    }
    return descriptorsChanged;
}

bool VulkanModelResources::publishReadyAssets(std::size_t maxPublications)
{
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
            (slot.state == LoadState::CpuReady &&
                animationController_.hasClip(manifest_->playerIdleAnimation()));
        if (canPublish && publishModel(model, false)) {
            ++publications;
        }
    }

    for (std::size_t i = 0; i < textures_.size(); ++i) {
        if (publications >= maxPublications) {
            return descriptorsChanged;
        }
        TextureSlot& slot = textures_[i];
        if (slot.state != LoadState::Loading || !futureReady(slot.future)) {
            continue;
        }
        const bool wasPublished = slot.gpu.image.view != VK_NULL_HANDLE;
        if (publishTexture(i, false)) {
            ++publications;
            descriptorsChanged = descriptorsChanged ||
                (!wasPublished && slot.gpu.image.view != VK_NULL_HANDLE);
        }
    }
    return descriptorsChanged;
}

void VulkanModelResources::retireCompletedUploads()
{
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

void VulkanModelResources::requestModel(RenderModel model)
{
    const AssetManifest::Model& definition = manifest_->model(model);
    ModelSlot& slot = models_[model.index()];
    if (slot.state != LoadState::Unrequested) {
        requestModelDependencies(model);
        return;
    }

    const std::filesystem::path path = assetRoot_ / definition.path;
    const GltfMeshLoadOptions options {
        .preserveAspectRatio = definition.preserveAspectRatio,
        .rotateHalfTurn = definition.rotateHalfTurn,
        .usePrimitiveMaterialTextures = definition.primitiveTextures,
    };
    const ModelGeometry geometry = definition.geometry;
    slot.future = taskSystem().enqueue([path, options, geometry]() -> PreparedModel {
        if (geometry == ModelGeometry::Skinned) {
            return loadGltfSkinnedMesh(path, options);
        }
        return loadGltfMesh(path, options);
    });
    slot.state = LoadState::Loading;
    requestModelDependencies(model);
}

void VulkanModelResources::requestTexture(std::size_t textureIndex)
{
    if (textureIndex >= textures_.size()) {
        throw std::runtime_error("Model material references an invalid texture index");
    }
    TextureSlot& slot = textures_[textureIndex];
    if (slot.state != LoadState::Unrequested) {
        return;
    }

    const std::filesystem::path path =
        assetRoot_ / manifest_->textures()[textureIndex].path;
    slot.future = taskSystem().enqueue([path] {
        return loadRgbaImage(path);
    });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::requestAnimation(RenderAnimation animation)
{
    const AssetManifest::Animation& definition = manifest_->animation(animation);
    AnimationSlot& slot = animations_[animation.index()];
    if (slot.state != LoadState::Unrequested) {
        return;
    }

    const std::filesystem::path path = assetRoot_ / definition.path;
    const uint32_t animationIndex =
        animationIndexFromUserNumber(definition.clip);
    slot.future = taskSystem().enqueue([path, animationIndex] {
        return loadGltfAnimationClip(path, animationIndex);
    });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::requestModelDependencies(RenderModel model)
{
    const AssetManifest::Model& definition = manifest_->model(model);
    if (definition.materialMode == ModelMaterialMode::SingleTexture) {
        requestTexture(definition.textureIndex);
    } else if (definition.materialMode == ModelMaterialMode::PrimitiveTextureIndex) {
        // Primitive material indices address the manifest descriptor array.
        // Until the manifest records a narrower mask, every slot is a real
        // dependency of this material mode.
        for (std::size_t i = 0; i < textures_.size(); ++i) {
            requestTexture(i);
        }
    }
    if (model == manifest_->playerModel()) {
        requestAnimation(manifest_->playerIdleAnimation());
    }
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
    if (slot.state == LoadState::Unrequested) {
        if (!wait) {
            return false;
        }
        requestModel(model);
    }
    if (slot.state == LoadState::CpuReady) {
        try {
            finalizeSkinnedMeshIfReady();
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
        return slot.state == LoadState::Ready || slot.state == LoadState::Failed;
    }
    if (!wait && !futureReady(slot.future)) {
        return false;
    }

    try {
        slot.prepared = slot.future.get();
        slot.state = LoadState::CpuReady;
        if (std::holds_alternative<MeshData>(*slot.prepared)) {
            slot.gpu = uploadMesh(std::get<MeshData>(*slot.prepared));
            slot.prepared.reset();
            slot.state = LoadState::Ready;
        } else {
            finalizeSkinnedMeshIfReady();
        }
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
    if (slot.state == LoadState::Unrequested) {
        if (!wait) {
            return false;
        }
        requestTexture(textureIndex);
    }
    if (!wait && !futureReady(slot.future)) {
        return false;
    }

    try {
        const ImageData image = slot.future.get();
        beginTextureUpload(
            image,
            slot.gpu.image,
            slot.gpu.sampler,
            slot.upload);
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
    if (slot.state == LoadState::Unrequested) {
        if (!wait) {
            return false;
        }
        requestAnimation(animation);
    }
    if (!wait && !futureReady(slot.future)) {
        return false;
    }

    try {
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

void VulkanModelResources::finalizeSkinnedMeshIfReady()
{
    ModelSlot& player = models_[manifest_->playerModel().index()];
    if (player.state != LoadState::CpuReady ||
        !animationController_.hasClip(manifest_->playerIdleAnimation())) {
        return;
    }
    if (!player.prepared ||
        !std::holds_alternative<SkinnedMeshData>(*player.prepared)) {
        throw std::runtime_error("Player manifest entry did not prepare a skinned mesh");
    }

    skinnedMeshUpdater_.create(
        physicalDevice_,
        device_,
        std::move(std::get<SkinnedMeshData>(*player.prepared)),
        animationController_.clip(manifest_->playerIdleAnimation()));
    player.prepared.reset();
    player.state = LoadState::Ready;
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
    for (uint32_t i = 0; i < models_.size(); ++i) {
        const RenderModel model { i + 1 };
        if (!requirements.contains(model)) {
            continue;
        }
        const AssetManifest::Model& definition = manifest_->model(model);
        if (definition.materialMode == ModelMaterialMode::SingleTexture) {
            result.at(definition.textureIndex) = true;
        } else if (definition.materialMode == ModelMaterialMode::PrimitiveTextureIndex) {
            std::fill(result.begin(), result.end(), true);
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
    const GltfAnimationClip* clip,
    float timeSeconds)
{
    animationController_.setPreview(clip, timeSeconds);
}

void VulkanModelResources::updateAnimations(const RenderFrameData& frameData)
{
    if (!skinnedMeshUpdater_.valid()) {
        return;
    }
    if (const std::optional<AnimationController::SkinningRequest> request =
            animationController_.update(frameData)) {
        skinnedMeshUpdater_.update(*request);
    }
}

VulkanModelResources::MeshView VulkanModelResources::meshForModel(
    RenderModel model) const
{
    if (model == manifest_->playerModel()) {
        if (models_[model.index()].state != LoadState::Ready) {
            throw std::runtime_error("Skinned model was used before it was ready");
        }
        return skinnedMeshUpdater_.mesh();
    }
    const GpuMesh& mesh = gpuMeshForModel(model);
    return {
        .vertexBuffer = mesh.vertexBuffer.buffer,
        .indexBuffer = mesh.indexBuffer.buffer,
        .indexCount = mesh.indexCount,
    };
}

VulkanModelResources::MaterialBinding VulkanModelResources::materialForModel(
    RenderModel model) const
{
    const AssetManifest::Model& definition = manifest_->model(model);
    return {
        .mode = definition.materialMode,
        .textureIndex = definition.textureIndex,
    };
}

std::vector<VulkanModelResources::TextureView> VulkanModelResources::textures() const
{
    // Shaders declare a fixed-size texture array (MODEL_TEXTURE_COUNT ==
    // maxModelTextures), so the view is always padded to that size with the
    // fallback texture regardless of how many textures the manifest defines.
    std::vector<TextureView> result;
    result.reserve(maxModelTextures);
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
    while (result.size() < maxModelTextures) {
        result.push_back({
            .imageView = fallbackTexture_.image.view,
            .sampler = fallbackTexture_.sampler,
        });
    }
    return result;
}

uint32_t VulkanModelResources::textureCount() const
{
    return maxModelTextures;
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
        } else if (state == LoadState::Loading ||
            state == LoadState::CpuReady ||
            state == LoadState::Uploading) {
            ++pending;
        } else if (state == LoadState::Failed) {
            ++result.failedAssets;
        }
    };
    for (const ModelSlot& model : models_) {
        countState(model.state, result.loadedModels, result.pendingModels);
    }
    for (const TextureSlot& texture : textures_) {
        countState(texture.state, result.loadedTextures, result.pendingTextures);
        if (texture.state == LoadState::Uploading) {
            ++result.uploadingTextures;
        }
    }
    for (const AnimationSlot& animation : animations_) {
        countState(animation.state, result.loadedAnimations, result.pendingAnimations);
    }
    result.textureUploadSubmissions = textureUploadSubmissions_;
    result.textureUploadCompletions = textureUploadCompletions_;
    return result;
}

VulkanModelResources::GpuMesh VulkanModelResources::uploadMesh(
    const MeshData& mesh) const
{
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::runtime_error("glTF mesh contains no geometry");
    }

    const VkDeviceSize vertexBytes = sizeof(MeshVertex) * mesh.vertices.size();
    const VkDeviceSize indexBytes = sizeof(uint32_t) * mesh.indices.size();
    GpuMesh result;
    result.vertexBuffer = createBuffer(
        vertexBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    result.indexBuffer = createBuffer(
        indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, result.vertexBuffer.memory, 0, vertexBytes, 0, &mapped),
        "vkMapMemory model vertex buffer failed");
    std::memcpy(mapped, mesh.vertices.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device_, result.vertexBuffer.memory);

    mapped = nullptr;
    vkCheck(vkMapMemory(device_, result.indexBuffer.memory, 0, indexBytes, 0, &mapped),
        "vkMapMemory model index buffer failed");
    std::memcpy(mapped, mesh.indices.data(), static_cast<std::size_t>(indexBytes));
    vkUnmapMemory(device_, result.indexBuffer.memory);

    result.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return result;
}

void VulkanModelResources::createTextureBlocking(
    const ImageData& image,
    OwnedImage& textureImage,
    VkSampler& sampler)
{
    PendingTextureUpload upload;
    try {
        beginTextureUpload(image, textureImage, sampler, upload);
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
    PendingTextureUpload& upload)
{
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        throw std::runtime_error("Texture image contains no pixels");
    }
    const VkDeviceSize imageBytes = image.rgba.size();
    upload.staging = createBuffer(
        imageBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, upload.staging.memory, 0, imageBytes, 0, &mapped),
        "vkMapMemory texture staging failed");
    std::memcpy(mapped, image.rgba.data(), image.rgba.size());
    vkUnmapMemory(device_, upload.staging.memory);

    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = { image.width, image.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &textureImage.image),
        "vkCreateImage model texture failed");

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
    vkCheck(vkBindImageMemory(device_, textureImage.image, textureImage.memory, 0),
        "vkBindImageMemory model texture failed");

    textureImage.view = createImageView(
        textureImage.image,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_ASPECT_COLOR_BIT);
    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    };
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler),
        "vkCreateSampler model texture failed");

    VkCommandBufferAllocateInfo commandBufferInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkCheck(vkAllocateCommandBuffers(
            device_, &commandBufferInfo, &upload.commandBuffer),
        "vkAllocateCommandBuffers texture upload failed");
    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(upload.commandBuffer, &beginInfo),
        "vkBeginCommandBuffer texture upload failed");

    VkImageMemoryBarrier2 toTransfer {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = textureImage.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo toTransferDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toTransfer,
    };
    vkCmdPipelineBarrier2(upload.commandBuffer, &toTransferDependency);

    VkBufferImageCopy copyRegion {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { image.width, image.height, 1 },
    };
    vkCmdCopyBufferToImage(
        upload.commandBuffer,
        upload.staging.buffer,
        textureImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    VkImageMemoryBarrier2 toRead {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = textureImage.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkDependencyInfo toReadDependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toRead,
    };
    vkCmdPipelineBarrier2(upload.commandBuffer, &toReadDependency);
    vkCheck(vkEndCommandBuffer(upload.commandBuffer),
        "vkEndCommandBuffer texture upload failed");

    VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    vkCheck(vkCreateFence(device_, &fenceInfo, nullptr, &upload.fence),
        "vkCreateFence texture upload failed");

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
    upload.submitted = true;
}

void VulkanModelResources::destroyTextureUpload(
    PendingTextureUpload& upload) const
{
    if (upload.fence) {
        vkDestroyFence(device_, upload.fence, nullptr);
    }
    if (upload.commandBuffer) {
        vkFreeCommandBuffers(
            device_, commandPool_, 1, &upload.commandBuffer);
    }
    if (upload.staging.buffer) {
        vkDestroyBuffer(device_, upload.staging.buffer, nullptr);
    }
    if (upload.staging.memory) {
        vkFreeMemory(device_, upload.staging.memory, nullptr);
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
}

void VulkanModelResources::destroyMesh(GpuMesh& mesh) const
{
    if (mesh.indexBuffer.buffer) {
        vkDestroyBuffer(device_, mesh.indexBuffer.buffer, nullptr);
    }
    if (mesh.indexBuffer.memory) {
        vkFreeMemory(device_, mesh.indexBuffer.memory, nullptr);
    }
    if (mesh.vertexBuffer.buffer) {
        vkDestroyBuffer(device_, mesh.vertexBuffer.buffer, nullptr);
    }
    if (mesh.vertexBuffer.memory) {
        vkFreeMemory(device_, mesh.vertexBuffer.memory, nullptr);
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
        !slot.gpu.vertexBuffer.buffer ||
        !slot.gpu.indexBuffer.buffer) {
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

VulkanModelResources::OwnedBuffer VulkanModelResources::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) const
{
    OwnedBuffer result;
    VkBufferCreateInfo bufferInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &result.buffer),
        "vkCreateBuffer model resource failed");

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties),
    };
    const VkResult allocationResult =
        vkAllocateMemory(device_, &allocationInfo, nullptr, &result.memory);
    if (allocationResult != VK_SUCCESS) {
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(allocationResult, "vkAllocateMemory model buffer failed");
    }
    const VkResult bindResult =
        vkBindBufferMemory(device_, result.buffer, result.memory, 0);
    if (bindResult != VK_SUCCESS) {
        vkFreeMemory(device_, result.memory, nullptr);
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(bindResult, "vkBindBufferMemory model buffer failed");
    }
    return result;
}

VkImageView VulkanModelResources::createImageView(
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask) const
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
        .subresourceRange = { aspectMask, 0, 1, 0, 1 },
    };
    VkImageView imageView = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(device_, &createInfo, nullptr, &imageView),
        "vkCreateImageView model texture failed");
    return imageView;
}

} // namespace sokoban
