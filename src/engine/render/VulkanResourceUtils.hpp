#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

struct VmaAllocation_T;

namespace sokoban {

class VulkanMemoryAllocator;

class VulkanError final : public std::runtime_error {
public:
    VulkanError(VkResult result, std::string message)
        : std::runtime_error(std::move(message))
        , result_(result)
    {
    }

    [[nodiscard]] VkResult result() const { return result_; }

private:
    VkResult result_ = VK_SUCCESS;
};

void vkCheck(VkResult result, const char* message);

// The same check, for a message assembled from a call name and a caller's
// label: "<call> <label> failed". Composing on the success path would cost an
// allocation per upload, so the string is only built when it throws.
void vkCheck(VkResult result, const char* call, std::string_view label);

namespace vulkanResources {

// ------------------------------------------------------- one-shot submits
//
// Six places record a single command buffer, submit it once and throw it
// away: geometry upload, texture upload, compressed texture upload, frame
// capture, tile thumbnails and UI image upload. They open and close it with
// the same twenty-odd lines, and every one of them already spelled its
// diagnostics "<vkFunction> <label> failed" - so `label` reproduces the
// existing text exactly rather than approximating it. Pass "geometry upload"
// and a failed allocation still says "vkAllocateCommandBuffers geometry
// upload failed".
//
// What the recorded command stream contains stays with the caller; only the
// prologue and the epilogue live here.

// Allocates a primary command buffer from `pool`, names it when `debugName`
// is given, and begins it with ONE_TIME_SUBMIT. Nothing is left allocated if
// either call fails - the caller cannot free a handle it was never given, so
// this frees it before rethrowing.
[[nodiscard]] VkCommandBuffer beginOneShotCommands(
    VkDevice device,
    VkCommandPool pool,
    std::string_view label,
    const char* debugName = nullptr);

// Ends `commandBuffer`, creates a fence, names it when `debugFenceName` is
// given, and submits against that fence. The fence is returned and belongs to
// the caller: wait on it here, or keep it and poll it later. As above,
// nothing survives a throw - a failed submit destroys the fence it just
// created rather than handing back a handle the caller never received.
[[nodiscard]] VkFence submitOneShotCommands(
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer commandBuffer,
    std::string_view label,
    const char* debugFenceName = nullptr);

struct ImageState {
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

[[nodiscard]] constexpr VkImageSubresourceRange subresourceRange(
    VkImageAspectFlags aspectMask)
{
    return {
        .aspectMask = aspectMask,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] VkImageMemoryBarrier2 imageBarrier(
    VkImage image,
    VkImageSubresourceRange range,
    ImageState from,
    ImageState to);
void transitionImages(
    VkCommandBuffer commandBuffer,
    std::span<const VkImageMemoryBarrier2> barriers,
    VkDependencyFlags dependencyFlags = 0);
void transitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageSubresourceRange range,
    ImageState from,
    ImageState to,
    VkDependencyFlags dependencyFlags = 0);

struct OwnedImage {
    VkImage image = VK_NULL_HANDLE;
    ::VmaAllocation_T* allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
};
[[nodiscard]] VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask,
    std::string_view debugName = {});
[[nodiscard]] OwnedImage createImage(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    const VkImageCreateInfo& imageInfo,
    VkImageAspectFlags aspectMask,
    std::string_view debugName = {});
void destroyImage(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    OwnedImage& image);

} // namespace vulkanResources
} // namespace sokoban
