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
#include <new>
#include <system_error>

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
#include <atomic>
#endif

namespace sokoban {
namespace {
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
std::atomic_bool failNextTextureRegistration = false;
#endif
#if SOKOBAN_ENABLE_DEBUG_UI
constexpr auto thumbnailRetryDelay = std::chrono::seconds(1);
#endif
} // namespace

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
    if (!valid() || static_cast<std::size_t>(tile) >= tileTypeCount) {
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
    const auto now = std::chrono::steady_clock::now();
    const auto queuedCount = pending_.size();
    for (std::size_t i = 0; i < queuedCount; ++i) {
        const TileType tile = pending_.front();
        pending_.pop_front();
        Thumbnail& thumbnail = cache_.at(static_cast<int>(tile));
        if (now < thumbnail.retryAfter) {
            pending_.push_back(tile);
            continue;
        }
        const auto retry = [&](const std::exception& error) {
            destroyThumbnail(thumbnail);
            if (!thumbnail.failureReported) {
                log::warning(log::Category::Assets)
                    << "Could not load the baked thumbnail for "
                    << tileTypeName(tile) << ": " << error.what()
                    << "; retrying later";
                thumbnail.failureReported = true;
            }
            thumbnail.retryAfter =
                std::chrono::steady_clock::now() + thumbnailRetryDelay;
            pending_.push_back(tile);
        };
        try {
            // Missing or corrupt files retain their placeholder until a bake
            // invalidates the cache. Only transient failures return to the queue.
            loadThumbnail(tile, thumbnail);
            thumbnail.failureReported = false;
        } catch (const VulkanError& error) {
            if (error.result() == VK_ERROR_DEVICE_LOST) {
                destroyThumbnail(thumbnail);
                throw;
            }
            retry(error);
        } catch (const std::exception& error) {
            retry(error);
        }
        // Rotating delayed retries must not turn into multiple uploads per frame.
        break;
    }

#endif
}

void VulkanThumbnailPass::loadThumbnail(TileType tile, Thumbnail& target)
{
    const std::filesystem::path file =
        assetRoot_ / tileThumbnails::assetPathFor(tile);
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error) {
        throw std::filesystem::filesystem_error("Inspecting thumbnail", file, error);
    }
    if (!exists) {
        return;
    }

    ImageData image;
    try {
        image = loadRgbaImage(file);
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& decodeError) {
        log::warning(log::Category::Assets)
            << "Ignoring the unreadable baked thumbnail for "
            << tileTypeName(tile) << ": " << decodeError.what();
        return;
    }
    if (image.width == 0 || image.height == 0 || image.rgba.empty()) {
        return;
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
#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    if (!failNextTextureRegistration.exchange(false))
#endif
    target.imguiTexture = ImGui_ImplVulkan_AddTexture(
        target.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!target.imguiTexture) {
        throw std::runtime_error("ImGui thumbnail texture registration failed");
    }
#endif
}

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
void VulkanThumbnailPass::failNextTextureRegistrationForTesting()
{
    failNextTextureRegistration.store(true);
}
#endif

} // namespace sokoban
