#include "engine/render/VulkanTextureUploader.hpp"

#include "engine/render/TextureUploadPlan.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace sokoban {
namespace {

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

} // namespace

void VulkanTextureUploader::create(
    VkPhysicalDevice physicalDevice,
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VulkanUploadRing& uploadRing,
    float maxSamplerAnisotropy)
{
    physicalDevice_ = physicalDevice;
    allocator_ = &allocator;
    device_ = device;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;
    uploadRing_ = &uploadRing;
    maxSamplerAnisotropy_ = maxSamplerAnisotropy;
}

void VulkanTextureUploader::destroy()
{
    physicalDevice_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    uploadRing_ = nullptr;
    maxSamplerAnisotropy_ = 1.0f;
}

void VulkanTextureUploader::createTextureBlocking(
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

void VulkanTextureUploader::createTextureImageAndSampler(
    const TextureImagePlan& plan,
    TextureInterpretation sampling,
    OwnedImage& textureImage,
    VkSampler& sampler) const
{
    textureImage.mipLevels = plan.mipLevels;
    const VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = plan.format,
        .extent = plan.extent,
        .mipLevels = textureImage.mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = plan.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    allocator_->createDeviceImage(
        imageInfo,
        textureImage.image,
        textureImage.allocation,
        plan.imageName);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE, textureImage.image, plan.imageName);

    textureImage.view = createImageView(
        textureImage.image,
        plan.format,
        VK_IMAGE_ASPECT_COLOR_BIT,
        textureImage.mipLevels);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_IMAGE_VIEW, textureImage.view, plan.viewName);

    const VkFilter minFilter = vulkanMinificationFilter(sampling.minFilter);
    // Anisotropy only earns its cost where a mip chain exists and the surface
    // is viewed obliquely - the splatted ground being the case that motivated
    // it. Point-sampled atlases keep their crisp texels.
    const float anisotropy =
        plan.anisotropyAllowed && minFilter == VK_FILTER_LINEAR
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
        "vkCreateSampler", plan.failureLabel);
    vulkanDebug::setObjectName(
        device_, VK_OBJECT_TYPE_SAMPLER, sampler, plan.samplerName);
}

void VulkanTextureUploader::beginTextureUpload(
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
    createTextureImageAndSampler(
        {
            .format = textureFormat,
            .extent = { image.width, image.height, 1 },
            .mipLevels = textureUploadPlan::uncompressedMipLevels(
                image.width,
                image.height,
                sampling.minFilter,
                formatProperties.optimalTilingFeatures),
            // The chain is generated here by blitting down the levels, so the
            // image is a transfer source as well as a destination.
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            // Not the same question as "more than one level" - see the note on
            // the plan. A 1x1 image generates no chain and still answers yes.
            .anisotropyAllowed = textureUploadPlan::generatesMipmaps(
                sampling.minFilter, formatProperties.optimalTilingFeatures),
            .imageName = "Model texture",
            .viewName = "Model texture view",
            .samplerName = "Model texture sampler",
            .failureLabel = "model texture",
        },
        sampling,
        textureImage,
        sampler);

    recordTextureCopy(image, textureImage, upload);
}

void VulkanTextureUploader::beginTextureUpload(
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
    const uint32_t mipLevels = textureUploadPlan::compressedMipLevels(
        texture.mips.size(), sourceBaseMip);
    createTextureImageAndSampler(
        {
            .format = textureFormat,
            .extent = { residentBase.width, residentBase.height, 1 },
            .mipLevels = mipLevels,
            // Blocks are uploaded, never blitted, so this one is a transfer
            // destination only.
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            .anisotropyAllowed = mipLevels > 1U,
            .imageName = "BC7 model texture",
            .viewName = "BC7 model texture view",
            .samplerName = "BC7 model texture sampler",
            .failureLabel = "BC7 model texture",
        },
        sampling,
        textureImage,
        sampler);

    recordTextureCopy(texture, sourceBaseMip, textureImage, upload);
}

void VulkanTextureUploader::recordTextureCopy(
    const ImageData& image,
    OwnedImage& textureImage,
    PendingTextureUpload& upload)
{
    const VkDeviceSize imageBytes = image.rgba.size();
    const auto staging = uploadRing_->reserve(imageBytes, 4);
    if (!staging) {
        throw std::runtime_error("Shared upload ring is full for texture upload");
    }
    upload.staging = *staging;
    uploadRing_->write(upload.staging, 0, image.rgba.data(), image.rgba.size());

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
        uploadRing_->buffer(),
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
    uploadRing_->commit(upload.staging);
    upload.submitted = true;
}

void VulkanTextureUploader::recordTextureCopy(
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
    const auto staging = uploadRing_->reserve(uploadBytes, 16);
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
        uploadRing_->write(
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
        uploadRing_->buffer(),
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
    uploadRing_->commit(upload.staging);
    upload.submitted = true;
}

void VulkanTextureUploader::destroyTextureUpload(
    PendingTextureUpload& upload)
{
    if (upload.staging.valid()) {
        if (upload.submitted) {
            uploadRing_->complete(upload.staging);
        } else {
            uploadRing_->abandon(upload.staging);
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

void VulkanTextureUploader::destroyTexture(
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

VkImageView VulkanTextureUploader::createImageView(
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
