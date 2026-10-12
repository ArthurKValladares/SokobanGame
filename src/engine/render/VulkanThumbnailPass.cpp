#include "engine/render/VulkanThumbnailPass.hpp"

#include "engine/Log.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanResourceUtils.hpp"

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#endif

#include <cstring>
#include <exception>
#include <system_error>

namespace sokoban {
VulkanThumbnailPass::~VulkanThumbnailPass()
{
    destroy();
}

void VulkanThumbnailPass::create(
    VulkanMemoryAllocator& allocator,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    std::filesystem::path assetRoot)
{
    destroy();
    device_ = device;
    allocator_ = &allocator;
    commandPool_ = commandPool;
    graphicsQueue_ = graphicsQueue;
    assetRoot_ = std::move(assetRoot);
}

void VulkanThumbnailPass::destroy()
{
    invalidate();
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
    commandPool_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    assetRoot_.clear();
}

void VulkanThumbnailPass::destroyThumbnail(
    Thumbnail& thumbnail,
    bool releaseImGuiDescriptor)
{
    if (thumbnail.imguiTexture) {
#if SOKOBAN_ENABLE_DEBUG_UI
        if (releaseImGuiDescriptor) {
            ImGui_ImplVulkan_RemoveTexture(thumbnail.imguiTexture);
        }
#else
        (void)releaseImGuiDescriptor;
#endif
        thumbnail.imguiTexture = VK_NULL_HANDLE;
    }
    vulkanResources::destroyImage(*allocator_, device_, thumbnail.image);
}

void VulkanThumbnailPass::invalidate()
{
    pending_.clear();
    if (!device_) {
        cache_.clear();
        return;
    }
    // The descriptor sets may still be referenced by an in-flight frame's
    // ImGui draw data.
    //
    // Discarded for the same reason as VulkanDeviceContext::waitIdle: destroy()
    // calls this during teardown, where throwing is not an option.
    (void)vkDeviceWaitIdle(device_);
    const bool imguiBackendAvailable =
#if SOKOBAN_ENABLE_DEBUG_UI
        ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().BackendRendererUserData != nullptr;
#else
        false;
#endif
    bool reportedLateDestruction = false;
    for (auto& [tile, thumbnail] : cache_) {
        if (thumbnail.imguiTexture && !imguiBackendAvailable &&
            !reportedLateDestruction) {
            log::error(log::Category::Rendering)
                << "Level-editor thumbnail descriptors outlived the ImGui "
                   "Vulkan backend; discarding stale handles";
            reportedLateDestruction = true;
        }
        destroyThumbnail(thumbnail, imguiBackendAvailable);
    }
    cache_.clear();
}

VkDescriptorSet VulkanThumbnailPass::thumbnailFor(TileType tile)
{
#if !SOKOBAN_ENABLE_DEBUG_UI
    (void)tile;
    return VK_NULL_HANDLE;
#else
    if (!valid()) {
        return VK_NULL_HANDLE;
    }
    const auto [entry, inserted] = cache_.try_emplace(static_cast<int>(tile));
    if (inserted) {
        // The placeholder also deduplicates requests while this thumbnail is
        // pending. Drawing a palette performs no file access or GPU work.
        pending_.push_back(tile);
    }
    return entry->second.imguiTexture;
#endif
}

void VulkanThumbnailPass::servicePendingThumbnails()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (!valid() || pending_.empty()) {
        return;
    }
    const TileType tile = pending_.front();
    pending_.pop_front();
    // Retain the placeholder on success or failure, so missing files are not
    // retried by the palette every frame.
    Thumbnail& thumbnail = cache_.at(static_cast<int>(tile));
    try {
        if (!loadThumbnail(tile, thumbnail)) {
            thumbnail.missing = true;
        }
    } catch (const std::exception& error) {
        log::warning(log::Category::Assets)
            << "Could not load the baked thumbnail for "
            << tileTypeName(tile) << ": " << error.what();
        destroyThumbnail(thumbnail);
        thumbnail = {};
        thumbnail.missing = true;
    }

#endif
}

bool VulkanThumbnailPass::loadThumbnail(TileType tile, Thumbnail& target)
{
    const std::filesystem::path file =
        assetRoot_ / tileThumbnails::assetPathFor(tile);
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return false;
    }

    const ImageData image = loadRgbaImage(file);
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        return false;
    }

    // SRGB, not UNORM, and the reasoning here used to be backwards.
    //
    // The capture holds display-ready, sRGB-*encoded* bytes - that is why the
    // PNGs look right in an image viewer. The swapchain is sRGB, so the
    // hardware encodes linear->sRGB when ImGui writes. Sampling as UNORM hands
    // the shader those already-encoded bytes as if they were linear, and they
    // get encoded a second time on the way out: a 200 comes back as a 232.
    // That is the overexposed, washed-out look, and it is why the grey bed
    // read as near-white in the palette.
    //
    // SRGB makes the sampler decode on read, so the write re-encodes back to
    // the original bytes and the button matches the file on disk exactly. It
    // also matches how the game uploads its own colour textures.
    constexpr VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    const VkDeviceSize byteCount = image.rgba.size();

    VkBuffer staging = VK_NULL_HANDLE;
    VulkanAllocation stagingAllocation = nullptr;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    const auto cleanup = [&] {
        if (fence) {
            vkDestroyFence(device_, fence, nullptr);
        }
        if (commandBuffer) {
            vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
        }
        allocator_->destroyBuffer(staging, stagingAllocation);
    };

    try {
        const VkImageCreateInfo imageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { image.width, image.height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        target.image = vulkanResources::createImage(
            *allocator_,
            device_,
            imageInfo,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "Tile thumbnail render target");

        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = byteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        void* mapped = nullptr;
        allocator_->createBuffer(
            bufferInfo,
            VulkanMemoryUsage::HostSequentialWrite,
            staging,
            stagingAllocation,
            &mapped,
            "Tile thumbnail staging");
        std::memcpy(mapped, image.rgba.data(), image.rgba.size());

        commandBuffer = vulkanResources::beginOneShotCommands(
            device_, commandPool_, "thumbnail");

        vulkanResources::transitionImage(
            commandBuffer,
            target.image.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {},
            {
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            });

        const VkBufferImageCopy copyRegion {
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageExtent = { image.width, image.height, 1 },
        };
        vkCmdCopyBufferToImage(
            commandBuffer,
            staging,
            target.image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion);

        vulkanResources::transitionImage(
            commandBuffer,
            target.image.image,
            vulkanResources::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
            {
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            },
            {
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            });
        fence = vulkanResources::submitOneShotCommands(
            device_, graphicsQueue_, commandBuffer, "thumbnail");
        // The deferred main-thread service admits only one thumbnail per
        // frame, so a palette cannot accumulate a batch of these waits.
        vkCheck(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences thumbnail failed");
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    target.imguiTexture = ImGui_ImplVulkan_AddTexture(
        target.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return target.imguiTexture != VK_NULL_HANDLE;
#else
    return false;
#endif
}

} // namespace sokoban
