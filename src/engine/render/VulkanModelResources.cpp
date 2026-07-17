#include "engine/render/VulkanModelResources.hpp"

#include "engine/TaskSystem.hpp"
#include "engine/render/GeneratedAssetCatalog.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
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

template <typename Enum>
std::size_t enumIndex(Enum value)
{
    return static_cast<std::size_t>(value);
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
    std::filesystem::path assetRoot)
{
    destroy();
    physicalDevice_ = physicalDevice;
    device_ = device;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;
    assetRoot_ = std::move(assetRoot);
    textures_.resize(assetCatalog::textures.size());

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
        createTexture(fallback, fallbackTexture_.image, fallbackTexture_.sampler);
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanModelResources::destroy()
{
    if (device_) {
        skinnedMeshUpdater_.destroy();
        for (auto texture = textures_.rbegin(); texture != textures_.rend(); ++texture) {
            destroyTexture(texture->gpu.image, texture->gpu.sampler);
        }
        destroyTexture(fallbackTexture_.image, fallbackTexture_.sampler);
        for (auto model = models_.rbegin(); model != models_.rend(); ++model) {
            destroyMesh(model->gpu);
        }
    }

    for (ModelSlot& model : models_) {
        model = {};
    }
    for (AnimationSlot& animation : animations_) {
        animation = {};
    }
    textures_.clear();
    fallbackTexture_ = {};
    animationController_.clear();
    assetRoot_.clear();
    graphicsQueue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
}

void VulkanModelResources::requestAssets(const RenderAssetRequirements& requirements)
{
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (requirements.contains(definition.model)) {
            requestModel(definition.model);
        }
    }
    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        if (requirements.contains(definition.animation)) {
            requestAnimation(definition.animation);
        }
    }
}

bool VulkanModelResources::ensureAssets(const RenderAssetRequirements& requirements)
{
    requestAssets(requirements);

    if (requirements.contains(RenderModel::Rogue)) {
        (void)publishAnimation(RenderAnimation::RogueIdle, true);
    }
    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        if (requirements.contains(definition.animation)) {
            (void)publishAnimation(definition.animation, true);
        }
    }
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (requirements.contains(definition.model)) {
            (void)publishModel(definition.model, true);
        }
    }

    bool descriptorsChanged = false;
    const std::vector<bool> textureRequirements = requiredTextures(requirements);
    for (std::size_t i = 0; i < textureRequirements.size(); ++i) {
        if (!textureRequirements[i]) {
            continue;
        }
        const bool wasReady = textures_[i].state == LoadState::Ready;
        (void)publishTexture(i, true);
        descriptorsChanged = descriptorsChanged ||
            (!wasReady && textures_[i].state == LoadState::Ready);
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

    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        if (publications >= maxPublications) {
            return descriptorsChanged;
        }
        AnimationSlot& slot = animations_[enumIndex(definition.animation)];
        if (slot.state == LoadState::Loading && futureReady(slot.future) &&
            publishAnimation(definition.animation, false)) {
            ++publications;
        }
    }

    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (publications >= maxPublications) {
            return descriptorsChanged;
        }
        ModelSlot& slot = models_[enumIndex(definition.model)];
        const bool canPublish =
            (slot.state == LoadState::Loading && futureReady(slot.future)) ||
            (slot.state == LoadState::CpuReady &&
                animationController_.hasClip(RenderAnimation::RogueIdle));
        if (canPublish && publishModel(definition.model, false)) {
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
        const bool wasReady = slot.state == LoadState::Ready;
        if (publishTexture(i, false)) {
            ++publications;
            descriptorsChanged = descriptorsChanged ||
                (!wasReady && slot.state == LoadState::Ready);
        }
    }
    return descriptorsChanged;
}

void VulkanModelResources::requestModel(RenderModel model)
{
    const ModelAssetDefinition& definition = modelDefinition(model);
    ModelSlot& slot = models_[enumIndex(model)];
    if (slot.state != LoadState::Unrequested) {
        requestModelDependencies(definition);
        return;
    }

    const std::filesystem::path path = assetRoot_ / definition.path;
    const GltfMeshLoadOptions options = definition.loadOptions;
    const ModelGeometry geometry = definition.geometry;
    slot.future = taskSystem().enqueue([path, options, geometry]() -> PreparedModel {
        if (geometry == ModelGeometry::Skinned) {
            return loadGltfSkinnedMesh(path, options);
        }
        return loadGltfMesh(path, options);
    });
    slot.state = LoadState::Loading;
    requestModelDependencies(definition);
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
        assetRoot_ / assetCatalog::textures[textureIndex].path;
    slot.future = taskSystem().enqueue([path] {
        return loadRgbaImage(path);
    });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::requestAnimation(RenderAnimation animation)
{
    const AnimationAssetDefinition& definition = animationDefinition(animation);
    AnimationSlot& slot = animations_[enumIndex(animation)];
    if (slot.state != LoadState::Unrequested) {
        return;
    }

    const std::filesystem::path path = assetRoot_ / definition.path;
    const uint32_t animationIndex =
        animationIndexFromUserNumber(definition.animationNumber);
    slot.future = taskSystem().enqueue([path, animationIndex] {
        return loadGltfAnimationClip(path, animationIndex);
    });
    slot.state = LoadState::Loading;
}

void VulkanModelResources::requestModelDependencies(
    const ModelAssetDefinition& definition)
{
    if (definition.materialMode == ModelMaterialMode::SingleTexture) {
        requestTexture(definition.textureIndex);
    } else if (definition.materialMode == ModelMaterialMode::PrimitiveTextureIndex) {
        // Primitive material indices address the catalog descriptor array.
        // Until the manifest records a narrower mask, every slot is a real
        // dependency of this material mode.
        for (std::size_t i = 0; i < textures_.size(); ++i) {
            requestTexture(i);
        }
    }
    if (definition.model == RenderModel::Rogue) {
        requestAnimation(RenderAnimation::RogueIdle);
    }
}

bool VulkanModelResources::publishModel(RenderModel model, bool wait)
{
    ModelSlot& slot = models_[enumIndex(model)];
    const ModelAssetDefinition& definition = modelDefinition(model);
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
            std::cerr << "Background model publication failed: "
                      << (assetRoot_ / definition.path).string() << '\n';
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
        std::cerr << "Background model publication failed: "
                  << (assetRoot_ / definition.path).string() << '\n';
    }
    return true;
}

bool VulkanModelResources::publishTexture(std::size_t textureIndex, bool wait)
{
    TextureSlot& slot = textures_.at(textureIndex);
    const std::filesystem::path path =
        assetRoot_ / assetCatalog::textures[textureIndex].path;
    if (slot.state == LoadState::Ready) {
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
        createTexture(image, slot.gpu.image, slot.gpu.sampler);
        slot.state = LoadState::Ready;
    } catch (...) {
        slot.failure = std::current_exception();
        slot.state = LoadState::Failed;
        if (wait) {
            throwIfFailed(slot.state, slot.failure, path, "texture");
        }
        std::cerr << "Background texture publication failed: "
                  << path.string() << '\n';
    }
    return true;
}

bool VulkanModelResources::publishAnimation(RenderAnimation animation, bool wait)
{
    AnimationSlot& slot = animations_[enumIndex(animation)];
    const AnimationAssetDefinition& definition = animationDefinition(animation);
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
        std::cerr << "Background animation publication failed: "
                  << path.string() << '\n';
    }
    return true;
}

void VulkanModelResources::finalizeSkinnedMeshIfReady()
{
    ModelSlot& rogue = models_[enumIndex(RenderModel::Rogue)];
    if (rogue.state != LoadState::CpuReady ||
        !animationController_.hasClip(RenderAnimation::RogueIdle)) {
        return;
    }
    if (!rogue.prepared ||
        !std::holds_alternative<SkinnedMeshData>(*rogue.prepared)) {
        throw std::runtime_error("Rogue catalog entry did not prepare a skinned mesh");
    }

    skinnedMeshUpdater_.create(
        physicalDevice_,
        device_,
        std::move(std::get<SkinnedMeshData>(*rogue.prepared)),
        animationController_.clip(RenderAnimation::RogueIdle));
    rogue.prepared.reset();
    rogue.state = LoadState::Ready;
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

const ModelAssetDefinition& VulkanModelResources::modelDefinition(RenderModel model) const
{
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (definition.model == model) {
            return definition;
        }
    }
    throw std::invalid_argument("Render model does not have a catalog asset");
}

const AnimationAssetDefinition& VulkanModelResources::animationDefinition(
    RenderAnimation animation) const
{
    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        if (definition.animation == animation) {
            return definition;
        }
    }
    throw std::invalid_argument("Render animation does not have a catalog asset");
}

std::vector<bool> VulkanModelResources::requiredTextures(
    const RenderAssetRequirements& requirements) const
{
    std::vector<bool> result(textures_.size(), false);
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (!requirements.contains(definition.model)) {
            continue;
        }
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
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (requirements.contains(definition.model) &&
            models_[enumIndex(definition.model)].state != LoadState::Ready) {
            return false;
        }
    }
    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        if (requirements.contains(definition.animation) &&
            animations_[enumIndex(definition.animation)].state != LoadState::Ready) {
            return false;
        }
    }
    const std::vector<bool> textureRequirements = requiredTextures(requirements);
    for (std::size_t i = 0; i < textureRequirements.size(); ++i) {
        if (textureRequirements[i] && textures_[i].state != LoadState::Ready) {
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
    if (model == RenderModel::Rogue) {
        if (models_[enumIndex(model)].state != LoadState::Ready) {
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
    const ModelAssetDefinition& definition = modelDefinition(model);
    return {
        .mode = definition.materialMode,
        .textureIndex = definition.textureIndex,
    };
}

std::vector<VulkanModelResources::TextureView> VulkanModelResources::textures() const
{
    std::vector<TextureView> result;
    result.reserve(textures_.size());
    for (const TextureSlot& texture : textures_) {
        const TextureResource& resource = texture.state == LoadState::Ready
            ? texture.gpu
            : fallbackTexture_;
        result.push_back({
            .imageView = resource.image.view,
            .sampler = resource.sampler,
        });
    }
    return result;
}

uint32_t VulkanModelResources::textureCount() const
{
    return static_cast<uint32_t>(textures_.size());
}

VulkanModelResources::LoadingStats VulkanModelResources::loadingStats() const
{
    LoadingStats result {
        .totalModels = static_cast<uint32_t>(assetCatalog::models.size()),
        .totalTextures = static_cast<uint32_t>(assetCatalog::textures.size()),
        .totalAnimations = static_cast<uint32_t>(assetCatalog::animations.size()),
    };
    auto countState = [&result](LoadState state, uint32_t& loaded, uint32_t& pending) {
        if (state == LoadState::Ready) {
            ++loaded;
        } else if (state == LoadState::Loading || state == LoadState::CpuReady) {
            ++pending;
        } else if (state == LoadState::Failed) {
            ++result.failedAssets;
        }
    };
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        countState(
            models_[enumIndex(definition.model)].state,
            result.loadedModels,
            result.pendingModels);
    }
    for (const TextureSlot& texture : textures_) {
        countState(texture.state, result.loadedTextures, result.pendingTextures);
    }
    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        countState(
            animations_[enumIndex(definition.animation)].state,
            result.loadedAnimations,
            result.pendingAnimations);
    }
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

void VulkanModelResources::createTexture(
    const ImageData& image,
    OwnedImage& textureImage,
    VkSampler& sampler)
{
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        throw std::runtime_error("Texture image contains no pixels");
    }
    const VkDeviceSize imageBytes = image.rgba.size();
    OwnedBuffer staging = createBuffer(
        imageBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, staging.memory, 0, imageBytes, 0, &mapped),
        "vkMapMemory texture staging failed");
    std::memcpy(mapped, image.rgba.data(), image.rgba.size());
    vkUnmapMemory(device_, staging.memory);

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

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandBufferInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkCheck(vkAllocateCommandBuffers(device_, &commandBufferInfo, &commandBuffer),
        "vkAllocateCommandBuffers texture upload failed");
    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo),
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
    vkCmdPipelineBarrier2(commandBuffer, &toTransferDependency);

    VkBufferImageCopy copyRegion {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { image.width, image.height, 1 },
    };
    vkCmdCopyBufferToImage(
        commandBuffer,
        staging.buffer,
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
    vkCmdPipelineBarrier2(commandBuffer, &toReadDependency);
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer texture upload failed");

    VkCommandBufferSubmitInfo commandBufferSubmit {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commandBuffer,
    };
    VkSubmitInfo2 submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmit,
    };
    vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE),
        "vkQueueSubmit2 texture upload failed");
    vkCheck(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle texture upload failed");
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);

    vkDestroyBuffer(device_, staging.buffer, nullptr);
    vkFreeMemory(device_, staging.memory, nullptr);
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
    if (model == RenderModel::Cube ||
        model == RenderModel::Rogue ||
        model == RenderModel::Count) {
        throw std::runtime_error("Render model does not have a static GPU mesh");
    }
    const ModelSlot& slot = models_[enumIndex(model)];
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
