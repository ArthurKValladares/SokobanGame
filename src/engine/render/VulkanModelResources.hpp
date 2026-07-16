#pragma once

#include "engine/render/GltfMesh.hpp"
#include "engine/render/RenderTypes.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sokoban {

// Owns all model-specific GPU and animation resources. The renderer provides
// the Vulkan context, then consumes lightweight buffer/texture views.
class VulkanModelResources {
public:
    struct MeshView {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    struct TextureView {
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        [[nodiscard]] bool valid() const { return imageView && sampler; }
    };

    VulkanModelResources() = default;
    ~VulkanModelResources();

    VulkanModelResources(const VulkanModelResources&) = delete;
    VulkanModelResources& operator=(const VulkanModelResources&) = delete;

    void create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        std::filesystem::path assetRoot);
    void destroy();

    void setAnimationPreview(const GltfAnimationClip* clip, float timeSeconds);
    void updateAnimations(const RenderFrameData& frameData);

    [[nodiscard]] MeshView meshForModel(RenderModel model) const;
    [[nodiscard]] TextureView rogueTexture() const;
    [[nodiscard]] TextureView platformerTexture() const;
    [[nodiscard]] TextureView platformerThreadTexture() const;

private:
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
        uint32_t vertexCount = 0;
    };

    void createModels();
    void createTextures();
    [[nodiscard]] GpuMesh uploadMesh(const MeshData& mesh) const;
    void updateMeshVertices(const GpuMesh& gpuMesh, const std::vector<MeshVertex>& vertices) const;
    void createTexture(const std::filesystem::path& path, OwnedImage& image, VkSampler& sampler);
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

    GpuMesh bricksAMesh_ {};
    GpuMesh stoneMesh_ {};
    GpuMesh waterMesh_ {};
    GpuMesh glassMesh_ {};
    GpuMesh conveyorMesh_ {};
    GpuMesh rogueMesh_ {};
    SkinnedMeshData rogueSkinnedMesh_ {};
    GltfAnimationClip rogueIdleAnimation_ {};
    GltfAnimationClip rogueMovementAnimation_ {};
    GltfAnimationClip roguePushAnimation_ {};

    RenderAnimation activeRogueAnimation_ = RenderAnimation::None;
    float activeRogueAnimationTime_ = -1.0f;
    RenderAnimation rogueFadeFromAnimation_ = RenderAnimation::None;
    float rogueFadeFromTime_ = 0.0f;
    float rogueFadeElapsed_ = 0.0f;
    const GltfAnimationClip* previewClip_ = nullptr;
    float previewTimeSeconds_ = 0.0f;
    const GltfAnimationClip* activePreviewClip_ = nullptr;
    float activePreviewTime_ = -1.0f;

    OwnedImage rogueTextureImage_ {};
    VkSampler rogueTextureSampler_ = VK_NULL_HANDLE;
    OwnedImage platformerTextureImage_ {};
    VkSampler platformerTextureSampler_ = VK_NULL_HANDLE;
    OwnedImage platformerThreadTextureImage_ {};
    VkSampler platformerThreadTextureSampler_ = VK_NULL_HANDLE;
};

} // namespace sokoban
