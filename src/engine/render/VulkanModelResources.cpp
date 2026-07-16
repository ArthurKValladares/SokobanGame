#include "engine/render/VulkanModelResources.hpp"

#include "engine/Config.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/render/GeneratedAssetCatalog.hpp"
#include "engine/render/ImageData.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <optional>
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

    try {
        createModels();
        createTextures();
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanModelResources::destroy()
{
    if (device_) {
        for (auto texture = textures_.rbegin(); texture != textures_.rend(); ++texture) {
            destroyTexture(texture->image, texture->sampler);
        }
        for (auto mesh = meshes_.rbegin(); mesh != meshes_.rend(); ++mesh) {
            destroyMesh(*mesh);
        }
    }

    textures_.clear();
    rogueSkinnedMesh_ = {};
    animationClips_ = {};
    activeRogueAnimation_ = RenderAnimation::None;
    activeRogueAnimationTime_ = -1.0f;
    rogueFadeFromAnimation_ = RenderAnimation::None;
    rogueFadeFromTime_ = 0.0f;
    rogueFadeElapsed_ = 0.0f;
    previewClip_ = nullptr;
    previewTimeSeconds_ = 0.0f;
    activePreviewClip_ = nullptr;
    activePreviewTime_ = -1.0f;
    assetRoot_.clear();
    graphicsQueue_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
}

void VulkanModelResources::createModels()
{
    struct PendingStaticModel {
        const ModelAssetDefinition* definition = nullptr;
        std::future<MeshData> data;
    };
    struct PendingSkinnedModel {
        const ModelAssetDefinition* definition = nullptr;
        std::future<SkinnedMeshData> data;
    };
    struct PendingAnimation {
        const AnimationAssetDefinition* definition = nullptr;
        std::future<GltfAnimationClip> data;
    };

    std::vector<PendingStaticModel> staticModels;
    std::vector<PendingSkinnedModel> skinnedModels;
    std::vector<PendingAnimation> animations;
    staticModels.reserve(assetCatalog::models.size());
    skinnedModels.reserve(assetCatalog::models.size());
    animations.reserve(assetCatalog::animations.size());

    // CPU-side parsing is independent per file. Capturing paths and options by
    // value keeps pending tasks independent from this object's lifetime.
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        const std::filesystem::path path = assetRoot_ / definition.path;
        const GltfMeshLoadOptions options = definition.loadOptions;
        if (definition.geometry == ModelGeometry::Skinned) {
            skinnedModels.push_back({
                .definition = &definition,
                .data = taskSystem().enqueue([path, options] {
                    return loadGltfSkinnedMesh(path, options);
                }),
            });
        } else {
            staticModels.push_back({
                .definition = &definition,
                .data = taskSystem().enqueue([path, options] {
                    return loadGltfMesh(path, options);
                }),
            });
        }
    }

    for (const AnimationAssetDefinition& definition : assetCatalog::animations) {
        const std::filesystem::path path = assetRoot_ / definition.path;
        const uint32_t animationIndex = animationIndexFromUserNumber(definition.animationNumber);
        animations.push_back({
            .definition = &definition,
            .data = taskSystem().enqueue([path, animationIndex] {
                return loadGltfAnimationClip(path, animationIndex);
            }),
        });
    }

    for (PendingStaticModel& pending : staticModels) {
        meshes_[enumIndex(pending.definition->model)] = uploadMesh(pending.data.get());
    }
    for (PendingSkinnedModel& pending : skinnedModels) {
        if (pending.definition->model != RenderModel::Rogue) {
            throw std::runtime_error("Unsupported skinned model in asset catalog");
        }
        rogueSkinnedMesh_ = pending.data.get();
    }
    for (PendingAnimation& pending : animations) {
        animationClips_[enumIndex(pending.definition->animation)] = pending.data.get();
    }

    const GltfAnimationClip& idle = animationClips_[enumIndex(RenderAnimation::RogueIdle)];
    if (rogueSkinnedMesh_.vertices.empty() || idle.channels.empty()) {
        throw std::runtime_error("Asset catalog must provide the Rogue mesh and idle animation");
    }
    meshes_[enumIndex(RenderModel::Rogue)] = uploadMesh(skinGltfMesh(rogueSkinnedMesh_, idle, 0.0f));
}

void VulkanModelResources::createTextures()
{
    textures_.resize(assetCatalog::textures.size());
    for (std::size_t i = 0; i < assetCatalog::textures.size(); ++i) {
        createTexture(
            assetRoot_ / assetCatalog::textures[i].path,
            textures_[i].image,
            textures_[i].sampler);
    }
}

void VulkanModelResources::setAnimationPreview(const GltfAnimationClip* clip, float timeSeconds)
{
    previewClip_ = clip;
    previewTimeSeconds_ = timeSeconds;
}

void VulkanModelResources::updateAnimations(const RenderFrameData& frameData)
{
    GpuMesh& rogueMesh = meshes_[enumIndex(RenderModel::Rogue)];
    if (previewClip_ != nullptr) {
        if (rogueSkinnedMesh_.vertices.empty() || !rogueMesh.vertexBuffer.memory) {
            return;
        }
        if (previewClip_ == activePreviewClip_ &&
            std::abs(previewTimeSeconds_ - activePreviewTime_) < 0.0001f) {
            return;
        }
        const MeshData skinnedMesh = skinGltfMesh(rogueSkinnedMesh_, *previewClip_, previewTimeSeconds_);
        updateMeshVertices(rogueMesh, skinnedMesh.vertices);
        activePreviewClip_ = previewClip_;
        activePreviewTime_ = previewTimeSeconds_;
        activeRogueAnimation_ = RenderAnimation::None;
        rogueFadeFromAnimation_ = RenderAnimation::None;
        return;
    }
    activePreviewClip_ = nullptr;

    RenderAnimation requestedAnimation = RenderAnimation::None;
    float requestedTime = 0.0f;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (tile.model == RenderModel::Rogue && tile.animation != RenderAnimation::None) {
            requestedAnimation = tile.animation;
            requestedTime = tile.animationTimeSeconds;
            break;
        }
    }

    if (requestedAnimation == RenderAnimation::None ||
        rogueSkinnedMesh_.vertices.empty() ||
        !rogueMesh.vertexBuffer.memory) {
        return;
    }

    auto clipFor = [this](RenderAnimation animation) -> const GltfAnimationClip& {
        switch (animation) {
        case RenderAnimation::RoguePush:
        case RenderAnimation::RogueMovement:
        case RenderAnimation::RogueIdle:
            return animationClips_[enumIndex(animation)];
        case RenderAnimation::None:
        case RenderAnimation::Count:
            return animationClips_[enumIndex(RenderAnimation::RogueIdle)];
        }
        return animationClips_[enumIndex(RenderAnimation::RogueIdle)];
    };

    const float timeDelta = activeRogueAnimation_ == RenderAnimation::None
        ? 0.0f
        : requestedTime - activeRogueAnimationTime_;

    if (requestedAnimation != activeRogueAnimation_ &&
        activeRogueAnimation_ != RenderAnimation::None) {
        rogueFadeFromAnimation_ = activeRogueAnimation_;
        rogueFadeFromTime_ = activeRogueAnimationTime_;
        rogueFadeElapsed_ = 0.0f;
    }

    if (rogueFadeFromAnimation_ == RenderAnimation::None &&
        requestedAnimation == activeRogueAnimation_ &&
        std::abs(requestedTime - activeRogueAnimationTime_) < 0.0001f) {
        return;
    }

    MeshData skinnedMesh;
    if (rogueFadeFromAnimation_ != RenderAnimation::None) {
        rogueFadeFromTime_ += timeDelta;
        rogueFadeElapsed_ += std::abs(timeDelta);
        constexpr float fadeSeconds = config::playerAnimationFadeSeconds;
        if (fadeSeconds <= 0.0f || rogueFadeElapsed_ >= fadeSeconds) {
            rogueFadeFromAnimation_ = RenderAnimation::None;
            skinnedMesh = skinGltfMesh(rogueSkinnedMesh_, clipFor(requestedAnimation), requestedTime);
        } else {
            float blend = rogueFadeElapsed_ / fadeSeconds;
            blend = blend * blend * (3.0f - 2.0f * blend);
            skinnedMesh = skinGltfMeshBlended(
                rogueSkinnedMesh_,
                clipFor(rogueFadeFromAnimation_),
                rogueFadeFromTime_,
                clipFor(requestedAnimation),
                requestedTime,
                blend);
        }
    } else {
        skinnedMesh = skinGltfMesh(rogueSkinnedMesh_, clipFor(requestedAnimation), requestedTime);
    }
    updateMeshVertices(rogueMesh, skinnedMesh.vertices);
    activeRogueAnimation_ = requestedAnimation;
    activeRogueAnimationTime_ = requestedTime;
}

VulkanModelResources::MeshView VulkanModelResources::meshForModel(RenderModel model) const
{
    const GpuMesh& mesh = gpuMeshForModel(model);
    return {
        .vertexBuffer = mesh.vertexBuffer.buffer,
        .indexBuffer = mesh.indexBuffer.buffer,
        .indexCount = mesh.indexCount,
    };
}

VulkanModelResources::MaterialBinding VulkanModelResources::materialForModel(RenderModel model) const
{
    for (const ModelAssetDefinition& definition : assetCatalog::models) {
        if (definition.model == model) {
            return {
                .mode = definition.materialMode,
                .textureIndex = definition.textureIndex,
            };
        }
    }
    return {};
}

std::vector<VulkanModelResources::TextureView> VulkanModelResources::textures() const
{
    std::vector<TextureView> result;
    result.reserve(textures_.size());
    for (const TextureResource& texture : textures_) {
        result.push_back({
            .imageView = texture.image.view,
            .sampler = texture.sampler,
        });
    }
    return result;
}

uint32_t VulkanModelResources::textureCount() const
{
    return static_cast<uint32_t>(textures_.size());
}

VulkanModelResources::GpuMesh VulkanModelResources::uploadMesh(const MeshData& mesh) const
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
    vkCheck(vkMapMemory(device_, result.vertexBuffer.memory, 0, vertexBytes, 0, &mapped), "vkMapMemory model vertex buffer failed");
    std::memcpy(mapped, mesh.vertices.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device_, result.vertexBuffer.memory);

    mapped = nullptr;
    vkCheck(vkMapMemory(device_, result.indexBuffer.memory, 0, indexBytes, 0, &mapped), "vkMapMemory model index buffer failed");
    std::memcpy(mapped, mesh.indices.data(), static_cast<std::size_t>(indexBytes));
    vkUnmapMemory(device_, result.indexBuffer.memory);

    result.indexCount = static_cast<uint32_t>(mesh.indices.size());
    result.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    return result;
}

void VulkanModelResources::updateMeshVertices(
    const GpuMesh& gpuMesh,
    const std::vector<MeshVertex>& vertices) const
{
    if (!gpuMesh.vertexBuffer.memory || vertices.empty() || vertices.size() != gpuMesh.vertexCount) {
        return;
    }

    const VkDeviceSize vertexBytes = sizeof(MeshVertex) * vertices.size();
    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, gpuMesh.vertexBuffer.memory, 0, vertexBytes, 0, &mapped), "vkMapMemory animated vertex buffer failed");
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(vertexBytes));
    vkUnmapMemory(device_, gpuMesh.vertexBuffer.memory);
}

void VulkanModelResources::createTexture(
    const std::filesystem::path& path,
    OwnedImage& textureImage,
    VkSampler& sampler)
{
    const ImageData image = loadRgbaImage(path);
    const VkDeviceSize imageBytes = image.rgba.size();
    OwnedBuffer staging = createBuffer(
        imageBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped = nullptr;
    vkCheck(vkMapMemory(device_, staging.memory, 0, imageBytes, 0, &mapped), "vkMapMemory texture staging failed");
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
    vkCheck(vkCreateImage(device_, &imageInfo, nullptr, &textureImage.image), "vkCreateImage model texture failed");

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device_, textureImage.image, &requirements);
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkCheck(vkAllocateMemory(device_, &allocationInfo, nullptr, &textureImage.memory), "vkAllocateMemory model texture failed");
    vkCheck(vkBindImageMemory(device_, textureImage.image, textureImage.memory, 0), "vkBindImageMemory model texture failed");

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandBufferInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkCheck(vkAllocateCommandBuffers(device_, &commandBufferInfo, &commandBuffer), "vkAllocateCommandBuffers texture upload failed");
    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer texture upload failed");

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
    vkCheck(vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2 texture upload failed");
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
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler), "vkCreateSampler model texture failed");
}

void VulkanModelResources::destroyTexture(OwnedImage& textureImage, VkSampler& sampler)
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

const VulkanModelResources::GpuMesh& VulkanModelResources::gpuMeshForModel(RenderModel model) const
{
    if (model == RenderModel::Cube || model == RenderModel::Count) {
        throw std::runtime_error("Render model does not have a GPU mesh");
    }
    const GpuMesh& mesh = meshes_[enumIndex(model)];
    if (!mesh.vertexBuffer.buffer || !mesh.indexBuffer.buffer) {
        throw std::runtime_error("Render model mesh was not loaded from the asset catalog");
    }
    return mesh;
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
    vkCheck(vkCreateBuffer(device_, &bufferInfo, nullptr, &result.buffer), "vkCreateBuffer model resource failed");

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
    VkMemoryAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties),
    };
    const VkResult allocationResult = vkAllocateMemory(device_, &allocationInfo, nullptr, &result.memory);
    if (allocationResult != VK_SUCCESS) {
        vkDestroyBuffer(device_, result.buffer, nullptr);
        vkCheck(allocationResult, "vkAllocateMemory model buffer failed");
    }
    const VkResult bindResult = vkBindBufferMemory(device_, result.buffer, result.memory, 0);
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
    vkCheck(vkCreateImageView(device_, &createInfo, nullptr, &imageView), "vkCreateImageView model texture failed");
    return imageView;
}

} // namespace sokoban
