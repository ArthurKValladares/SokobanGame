#include "engine/render/VulkanSwapchainResources.hpp"

#include "engine/render/RenderResolution.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace sokoban {
VulkanSwapchainResources::~VulkanSwapchainResources()
{
    destroy();
}

void VulkanSwapchainResources::create(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSurfaceKHR surface,
    SDL_Window* window,
    QueueFamilies queueFamilies,
    VkSampleCountFlagBits sampleCount,
    int renderScalePercent,
    VkFormat depthFormat,
    bool vsync,
    VkSwapchainKHR oldSwapchain)
{
    destroy();
    physicalDevice_ = physicalDevice;
    device_ = device;
    surface_ = surface;
    window_ = window;
    queueFamilies_ = queueFamilies;
    sampleCount_ = sampleCount;
    renderScalePercent_ = normalizedRenderScalePercent(renderScalePercent);
    depthFormat_ = depthFormat;
    vsync_ = vsync;

    try {
        createSwapchain(oldSwapchain);
        createAttachments();
    } catch (...) {
        destroy();
        throw;
    }
}

void VulkanSwapchainResources::recreate()
{
    destroyAttachments();
    for (SwapchainImage& image : images_) {
        if (image.view) {
            vkDestroyImageView(device_, image.view, nullptr);
        }
    }
    images_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    createSwapchain(VK_NULL_HANDLE);
    createAttachments();
}

void VulkanSwapchainResources::recreateAttachments(
    VkSampleCountFlagBits sampleCount,
    int renderScalePercent)
{
    destroyAttachments();
    sampleCount_ = sampleCount;
    renderScalePercent_ = normalizedRenderScalePercent(renderScalePercent);
    createAttachments();
}

void VulkanSwapchainResources::destroy()
{
    if (device_) {
        destroyAttachments();
        for (SwapchainImage& image : images_) {
            if (image.view) {
                vkDestroyImageView(device_, image.view, nullptr);
            }
        }
        if (swapchain_) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }
    }
    images_.clear();
    swapchain_ = VK_NULL_HANDLE;
    extent_ = {};
    renderExtent_ = {};
    presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    colorFormat_ = VK_FORMAT_UNDEFINED;
    depthFormat_ = VK_FORMAT_D32_SFLOAT;
    sampleCount_ = VK_SAMPLE_COUNT_1_BIT;
    renderScalePercent_ = 100;
    queueFamilies_ = {};
    window_ = nullptr;
    surface_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
}

bool VulkanSwapchainResources::canRecreate() const
{
    if (!window_ || !physicalDevice_ || !surface_) {
        return false;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physicalDevice_, surface_, &capabilities) != VK_SUCCESS) {
        return false;
    }
    return capabilities.currentExtent.width != 0 &&
        capabilities.currentExtent.height != 0;
}

VkResult VulkanSwapchainResources::acquire(VkSemaphore available, uint32_t& imageIndex) const
{
    return vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, available, VK_NULL_HANDLE, &imageIndex);
}

VkResult VulkanSwapchainResources::present(
    VkQueue presentQueue,
    VkSemaphore finished,
    uint32_t imageIndex) const
{
    VkPresentInfoKHR presentInfo {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &finished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &imageIndex,
    };
    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

void VulkanSwapchainResources::beginFrame(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    RenderStats& stats)
{
    (void)imageIndex;
    const auto colorAttachmentState = vulkanResources::ImageState {
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    vulkanResources::transitionImage(
        commandBuffer,
        resolvedColorImage_.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        {},
        colorAttachmentState);
    ++stats.imageBarriers;

    if (msaaEnabled()) {
        vulkanResources::transitionImage(
            commandBuffer,
            msaaColorImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {},
            colorAttachmentState);
        ++stats.imageBarriers;
    }

    if (depthImage_.image) {
        vulkanResources::transitionImage(
            commandBuffer,
            depthImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            {
                depthLayoutInitialized_
                ? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                : VK_PIPELINE_STAGE_2_NONE,
                depthLayoutInitialized_
                ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                : VK_ACCESS_2_NONE,
                depthLayoutInitialized_
                ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED,
            },
            {
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            });
        depthLayoutInitialized_ = true;
        ++stats.imageBarriers;
    }

    if (resolveDepthImage_.image) {
        vulkanResources::transitionImage(
            commandBuffer,
            resolveDepthImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            {},
            {
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            });
        ++stats.imageBarriers;
    }
}

void VulkanSwapchainResources::endFrame(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    RenderStats& stats) const
{
    vulkanResources::transitionImage(
        commandBuffer,
        image(imageIndex),
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        {
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        { VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR });
    ++stats.imageBarriers;
}

void VulkanSwapchainResources::ensureSceneColorReadable(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    if (!sceneColorImage_.image || sceneColorLayout_ != VK_IMAGE_LAYOUT_UNDEFINED) {
        return;
    }
    vulkanResources::transitionImage(
        commandBuffer,
        sceneColorImage_.image,
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        {},
        {
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
    sceneColorLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ++stats.imageBarriers;
}

void VulkanSwapchainResources::copyResolvedSceneColor(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    const std::array<VkImageMemoryBarrier2, 2> toTransfer {
        vulkanResources::imageBarrier(
            resolvedColorImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            }),
        vulkanResources::imageBarrier(
            sceneColorImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                sceneColorLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_2_NONE
                    : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                sceneColorLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_ACCESS_2_NONE
                    : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                sceneColorLayout_,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            }),
    };
    vulkanResources::transitionImages(commandBuffer, toTransfer);
    ++stats.imageBarriers;

    VkImageCopy copyRegion {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .extent = { .width = renderExtent_.width, .height = renderExtent_.height, .depth = 1 },
    };
    vkCmdCopyImage(
        commandBuffer,
        resolvedColorImage_.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        sceneColorImage_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    const std::array<VkImageMemoryBarrier2, 2> fromTransfer {
        vulkanResources::imageBarrier(
            resolvedColorImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            }),
        vulkanResources::imageBarrier(
            sceneColorImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }),
    };
    vulkanResources::transitionImages(commandBuffer, fromTransfer);
    sceneColorLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ++stats.imageBarriers;
}

void VulkanSwapchainResources::prepareSwapchainForUi(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    RenderStats& stats) const
{
    // The developer workspace owns the whole swapchain image. Discard its
    // previous presentation contents; ImGui will clear and redraw it below.
    vulkanResources::transitionImage(
        commandBuffer,
        image(imageIndex),
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        {},
        {
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        });
    ++stats.imageBarriers;
}

void VulkanSwapchainResources::copyResolvedSceneDepth(
    VkCommandBuffer commandBuffer,
    RenderStats& stats)
{
    const VkImage source = depthSourceImage();
    const std::array<VkImageMemoryBarrier2, 2> toTransfer {
        vulkanResources::imageBarrier(
            source,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            {
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            }),
        vulkanResources::imageBarrier(
            sceneDepthImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            {
                sceneDepthLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_2_NONE
                    : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                sceneDepthLayout_ == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_ACCESS_2_NONE
                    : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                sceneDepthLayout_,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            }),
    };
    vulkanResources::transitionImages(commandBuffer, toTransfer);
    ++stats.imageBarriers;

    const VkImageCopy copyRegion {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .extent = {
            .width = renderExtent_.width,
            .height = renderExtent_.height,
            .depth = 1,
        },
    };
    vkCmdCopyImage(
        commandBuffer,
        source,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        sceneDepthImage_.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);

    const std::array<VkImageMemoryBarrier2, 2> fromTransfer {
        vulkanResources::imageBarrier(
            source,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            }),
        vulkanResources::imageBarrier(
            sceneDepthImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT),
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }),
    };
    vulkanResources::transitionImages(commandBuffer, fromTransfer);
    sceneDepthLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ++stats.imageBarriers;
}

void VulkanSwapchainResources::upscaleSceneToSwapchain(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    RenderStats& stats) const
{
    const std::array<VkImageMemoryBarrier2, 2> toTransfer {
        vulkanResources::imageBarrier(
            resolvedColorImage_.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            }),
        vulkanResources::imageBarrier(
            image(imageIndex),
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {},
            {
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            }),
    };
    vulkanResources::transitionImages(commandBuffer, toTransfer);
    stats.imageBarriers += static_cast<uint32_t>(toTransfer.size());

    VkImageBlit2 region {
        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcOffsets = {
            VkOffset3D { 0, 0, 0 },
            VkOffset3D {
                static_cast<int32_t>(renderExtent_.width),
                static_cast<int32_t>(renderExtent_.height),
                1,
            },
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstOffsets = {
            VkOffset3D { 0, 0, 0 },
            VkOffset3D {
                static_cast<int32_t>(extent_.width),
                static_cast<int32_t>(extent_.height),
                1,
            },
        },
    };
    VkBlitImageInfo2 blitInfo {
        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
        .srcImage = resolvedColorImage_.image,
        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .dstImage = image(imageIndex),
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount = 1,
        .pRegions = &region,
        .filter = VK_FILTER_LINEAR,
    };
    vkCmdBlitImage2(commandBuffer, &blitInfo);

    vulkanResources::transitionImage(
        commandBuffer,
        image(imageIndex),
        vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
        {
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        },
        {
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        });
    ++stats.imageBarriers;
}

VkImage VulkanSwapchainResources::image(uint32_t index) const
{
    return images_.at(index).image;
}

VkImageView VulkanSwapchainResources::imageView(uint32_t index) const
{
    return images_.at(index).view;
}

VkImageView VulkanSwapchainResources::sampledDepthView() const
{
    return resolveDepthImage_.view ? resolveDepthImage_.view : depthImage_.view;
}

VkImage VulkanSwapchainResources::depthSourceImage() const
{
    return resolveDepthImage_.image ? resolveDepthImage_.image : depthImage_.image;
}

VkImageView VulkanSwapchainResources::renderColorView() const
{
    return msaaEnabled() ? msaaColorImage_.view : resolvedColorImage_.view;
}

VkImageView VulkanSwapchainResources::resolveColorView() const
{
    return msaaEnabled() ? resolvedColorImage_.view : VK_NULL_HANDLE;
}

void VulkanSwapchainResources::createSwapchain(
    VkSwapchainKHR oldSwapchain)
{
    VkSurfaceCapabilitiesKHR capabilities {};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");

    uint32_t formatCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice_, surface_, &formatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR failed");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice_, surface_, &formatCount, formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR failed");

    uint32_t presentModeCount = 0;
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physicalDevice_, surface_, &presentModeCount, nullptr),
        "vkGetPhysicalDeviceSurfacePresentModesKHR failed");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physicalDevice_, surface_, &presentModeCount, presentModes.data()),
        "vkGetPhysicalDeviceSurfacePresentModesKHR failed");

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        throw std::runtime_error(
            "Swapchain images do not support transfer destination usage required for render scaling");
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    std::array familyIndices { queueFamilies_.graphics, queueFamilies_.present };
    const bool sharedQueues = queueFamilies_.graphics != queueFamilies_.present;
    extent_ = chooseExtent(capabilities);
    colorFormat_ = surfaceFormat.format;
    presentMode_ = choosePresentMode(presentModes);

    VkSwapchainCreateInfoKHR createInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface_,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = sharedQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = sharedQueues ? static_cast<uint32_t>(familyIndices.size()) : 0U,
        .pQueueFamilyIndices = sharedQueues ? familyIndices.data() : nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode_,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain,
    };
    vkCheck(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
        "vkCreateSwapchainKHR failed");

    uint32_t actualImageCount = 0;
    vkCheck(vkGetSwapchainImagesKHR(device_, swapchain_, &actualImageCount, nullptr),
        "vkGetSwapchainImagesKHR failed");
    std::vector<VkImage> rawImages(actualImageCount);
    vkCheck(vkGetSwapchainImagesKHR(device_, swapchain_, &actualImageCount, rawImages.data()),
        "vkGetSwapchainImagesKHR failed");
    images_.resize(rawImages.size());
    for (std::size_t i = 0; i < rawImages.size(); ++i) {
        images_[i].image = rawImages[i];
        images_[i].view = vulkanResources::createImageView(
            device_, rawImages[i], colorFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanSwapchainResources::createAttachments()
{
    const PixelExtent scaled = scaledRenderExtent(
        { extent_.width, extent_.height },
        renderScalePercent_);
    renderExtent_ = { scaled.width, scaled.height };
    depthLayoutInitialized_ = false;
    sceneColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    sceneDepthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    createResolvedColor();
    createMsaaColor();
    createDepth();
    createSceneColor();
    createSceneDepth();
}

void VulkanSwapchainResources::createResolvedColor()
{
    if (renderExtent_.width == 0 || renderExtent_.height == 0) {
        return;
    }
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = colorFormat_,
        .extent = { .width = renderExtent_.width, .height = renderExtent_.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    resolvedColorImage_ = vulkanResources::createImage(
        physicalDevice_, device_, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanSwapchainResources::createMsaaColor()
{
    if (!msaaEnabled() || renderExtent_.width == 0 || renderExtent_.height == 0) {
        return;
    }
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = colorFormat_,
        .extent = { .width = renderExtent_.width, .height = renderExtent_.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = sampleCount_,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    msaaColorImage_ = vulkanResources::createImage(
        physicalDevice_, device_, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanSwapchainResources::createDepth()
{
    if (renderExtent_.width == 0 || renderExtent_.height == 0) {
        return;
    }
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat_,
        .extent = { .width = renderExtent_.width, .height = renderExtent_.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = sampleCount_,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    depthImage_ = vulkanResources::createImage(
        physicalDevice_, device_, imageInfo, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (msaaEnabled()) {
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        resolveDepthImage_ = vulkanResources::createImage(
            physicalDevice_, device_, imageInfo, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}

void VulkanSwapchainResources::createSceneColor()
{
    if (renderExtent_.width == 0 || renderExtent_.height == 0) {
        return;
    }
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = colorFormat_,
        .extent = { .width = renderExtent_.width, .height = renderExtent_.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    sceneColorImage_ = vulkanResources::createImage(
        physicalDevice_, device_, imageInfo, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    vkCheck(vkCreateSampler(device_, &samplerInfo, nullptr, &sceneColorSampler_),
        "vkCreateSampler scene color failed");
}

void VulkanSwapchainResources::createSceneDepth()
{
    if (renderExtent_.width == 0 || renderExtent_.height == 0) {
        return;
    }
    const VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat_,
        .extent = {
            .width = renderExtent_.width,
            .height = renderExtent_.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    sceneDepthImage_ = vulkanResources::createImage(
        physicalDevice_, device_, imageInfo, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanSwapchainResources::destroyAttachments()
{
    if (!device_) {
        return;
    }
    if (sceneColorSampler_) {
        vkDestroySampler(device_, sceneColorSampler_, nullptr);
    }
    sceneColorSampler_ = VK_NULL_HANDLE;
    vulkanResources::destroyImage(device_, sceneDepthImage_);
    vulkanResources::destroyImage(device_, sceneColorImage_);
    vulkanResources::destroyImage(device_, resolveDepthImage_);
    vulkanResources::destroyImage(device_, depthImage_);
    vulkanResources::destroyImage(device_, msaaColorImage_);
    vulkanResources::destroyImage(device_, resolvedColorImage_);
    renderExtent_ = {};
    sceneColorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    sceneDepthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    depthLayoutInitialized_ = false;
}

VkSurfaceFormatKHR VulkanSwapchainResources::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR VulkanSwapchainResources::choosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const
{
    if (vsync_) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    for (VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    for (VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchainResources::chooseExtent(
    const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    return {
        .width = std::clamp(
            static_cast<uint32_t>(std::max(width, 0)),
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width),
        .height = std::clamp(
            static_cast<uint32_t>(std::max(height, 0)),
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height),
    };
}

} // namespace sokoban
