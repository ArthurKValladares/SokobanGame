#pragma once

#include "engine/TextureSource.hpp"
#include "engine/render/CompressedTextureArtifact.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanUploadRing.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace sokoban {

// Everything between "here are some pixels" and "there is a sampled image on
// the device": format choice, image and view creation, sampler policy, the
// staging copy, mip generation or block upload, and the fence that says when
// it is done.
//
// Lifted out of VulkanModelResources, which covered eight jobs in 3,200 lines.
// This is the one of them with no opinion about *which* texture it is
// handling - no slot, no load state, no descriptor index, no residency budget,
// no scheduler - which is exactly why it could leave: the measurement that
// picked this boundary was that these nine functions touch seven device
// handles between them and not one piece of catalog or residency state.
//
// It owns nothing. The allocator and the upload ring are borrowed; the ring in
// particular is shared with the geometry arena, so it could not have moved in
// here even if the textures were its only user.
class VulkanTextureUploader {
public:
    // A texture image and the levels it was created with.
    //
    // vulkanResources::OwnedImage is the same three handles without the level
    // count, because the passes using it create single-level images. Widening
    // that struct would touch five passes for a field only this one needs, so
    // the two stay apart until something else wants the count.
    struct OwnedImage {
        VkImage image = VK_NULL_HANDLE;
        VulkanAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t mipLevels = 1;
    };

    // One upload in flight: its staging reservation, the command buffer that
    // records it, and the fence that says the copy has landed.
    struct PendingTextureUpload {
        VulkanUploadRing::Reservation staging {};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool submitted = false;
    };

    void create(
        VkPhysicalDevice physicalDevice,
        VulkanMemoryAllocator& allocator,
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VulkanUploadRing& uploadRing,
        float maxSamplerAnisotropy);
    // Drops the borrowed handles. Owns nothing, so there is nothing to free -
    // this exists so that after the owner's destroy() a stray call here fails
    // on a null handle exactly as it did when these were the owner's members.
    void destroy();

    // Creates the image and waits for the copy. Used for the fallback texture
    // and for editor repaints, where there is no frame to hide the wait in.
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
    void beginTextureUpload(
        const CompressedTextureArtifact& texture,
        uint32_t sourceBaseMip,
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
    void recordTextureCopy(
        const CompressedTextureArtifact& texture,
        uint32_t sourceBaseMip,
        OwnedImage& gpuImage,
        PendingTextureUpload& upload);
    void destroyTextureUpload(PendingTextureUpload& upload);
    void destroyTexture(OwnedImage& image, VkSampler& sampler);

private:
    // What the two upload paths disagree about. Everything after this - the
    // image, its view, its sampler and the four debug strings - is the same
    // work, and was written out twice in full.
    struct TextureImagePlan {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent3D extent {};
        uint32_t mipLevels = 1;
        VkImageUsageFlags usage = 0;
        // Whether this upload may filter anisotropically. The two paths ask
        // different questions and both are kept: an uncompressed upload asks
        // whether the format can generate a chain at all, a compressed one
        // whether the chain it was handed has more than one level. The two
        // disagree only for a 1x1 uncompressed image, where the answer cannot
        // matter - but they are not the same question, so they are not
        // written as if they were.
        bool anisotropyAllowed = false;
        const char* imageName = "";
        const char* viewName = "";
        const char* samplerName = "";
        // Composed into "vkCreateSampler <label> failed" only on failure.
        const char* failureLabel = "";
    };

    // Creates the image, its view and its sampler, and names all three.
    void createTextureImageAndSampler(
        const TextureImagePlan& plan,
        TextureInterpretation sampling,
        OwnedImage& textureImage,
        VkSampler& sampler) const;
    [[nodiscard]] VkImageView createImageView(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectMask,
        uint32_t mipLevels) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VulkanMemoryAllocator* allocator_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    // Shared with the geometry arena, so borrowed rather than owned.
    VulkanUploadRing* uploadRing_ = nullptr;
    float maxSamplerAnisotropy_ = 1.0f;
};

} // namespace sokoban
