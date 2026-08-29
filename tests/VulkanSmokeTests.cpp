#include "engine/render/VulkanDeviceContext.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

class SdlVideo final {
public:
    SdlVideo()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(
                std::string("SDL_Init failed: ") + SDL_GetError());
        }
    }

    ~SdlVideo()
    {
        SDL_Quit();
    }

    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;
};

class SdlWindow final {
public:
    SdlWindow()
    {
        window_ = SDL_CreateWindow(
            "Sokoban Vulkan smoke test", 32, 32,
            SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window_) {
            throw std::runtime_error(
                std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
    }

    ~SdlWindow()
    {
        if (window_) {
            SDL_DestroyWindow(window_);
        }
    }

    [[nodiscard]] SDL_Window* get() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
};

void submitNoOp(sokoban::VulkanDeviceContext& deviceContext)
{
    const VkCommandBufferAllocateInfo allocationInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = deviceContext.commandPool(),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (const VkResult result = vkAllocateCommandBuffers(
            deviceContext.device(), &allocationInfo, &commandBuffer);
        result != VK_SUCCESS) {
        throw std::runtime_error(
            "vkAllocateCommandBuffers failed: " + std::to_string(result));
    }

    const VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    if (const VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        result != VK_SUCCESS) {
        vkFreeCommandBuffers(
            deviceContext.device(), deviceContext.commandPool(), 1, &commandBuffer);
        throw std::runtime_error(
            "vkBeginCommandBuffer failed: " + std::to_string(result));
    }
    if (const VkResult result = vkEndCommandBuffer(commandBuffer);
        result != VK_SUCCESS) {
        vkFreeCommandBuffers(
            deviceContext.device(), deviceContext.commandPool(), 1, &commandBuffer);
        throw std::runtime_error(
            "vkEndCommandBuffer failed: " + std::to_string(result));
    }

    const VkFenceCreateInfo fenceInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateFence(
            deviceContext.device(), &fenceInfo, nullptr, &fence);
        result != VK_SUCCESS) {
        vkFreeCommandBuffers(
            deviceContext.device(), deviceContext.commandPool(), 1, &commandBuffer);
        throw std::runtime_error(
            "vkCreateFence failed: " + std::to_string(result));
    }

    const VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    const VkResult submitResult = vkQueueSubmit(
        deviceContext.graphicsQueue(), 1, &submitInfo, fence);
    const VkResult waitResult = submitResult == VK_SUCCESS
        ? vkWaitForFences(
              deviceContext.device(), 1, &fence, VK_TRUE,
              std::chrono::seconds(5).count() * 1'000'000'000ULL)
        : submitResult;

    // A timeout still leaves the command buffer in flight. Do not release it
    // until the queue has finished, even though the test will report failure.
    if (submitResult == VK_SUCCESS && waitResult != VK_SUCCESS) {
        (void)vkQueueWaitIdle(deviceContext.graphicsQueue());
    }

    vkDestroyFence(deviceContext.device(), fence, nullptr);
    vkFreeCommandBuffers(
        deviceContext.device(), deviceContext.commandPool(), 1, &commandBuffer);
    if (waitResult != VK_SUCCESS) {
        throw std::runtime_error(
            "Vulkan no-op submission did not complete: " +
            std::to_string(waitResult));
    }
}

void exerciseMemoryAllocator(sokoban::VulkanDeviceContext& deviceContext)
{
    sokoban::VulkanMemoryAllocator& allocator =
        deviceContext.memoryAllocator();
    VkBuffer buffer = VK_NULL_HANDLE;
    sokoban::VulkanAllocation bufferAllocation = nullptr;
    VkImage image = VK_NULL_HANDLE;
    sokoban::VulkanAllocation imageAllocation = nullptr;
    try {
        const VkBufferCreateInfo bufferInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = 4096,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        void* mapped = nullptr;
        allocator.createBuffer(
            bufferInfo,
            sokoban::VulkanMemoryUsage::HostSequentialWrite,
            buffer,
            bufferAllocation,
            &mapped,
            "Vulkan smoke mapped buffer");
        if (!buffer || !bufferAllocation || !mapped) {
            throw std::runtime_error(
                "VMA did not return a mapped host buffer");
        }
        static_cast<std::byte*>(mapped)[0] = std::byte { 0x5a };

        const VkImageCreateInfo imageInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = { 16, 16, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        allocator.createDeviceImage(
            imageInfo,
            image,
            imageAllocation,
            "Vulkan smoke device image");
        const sokoban::VulkanMemoryStatistics active = allocator.statistics();
        if (!image || !imageAllocation || active.allocationCount < 2 ||
            active.allocationBytes == 0 || active.blockCount == 0) {
            throw std::runtime_error(
                "VMA allocation statistics did not include smoke resources");
        }
    } catch (...) {
        allocator.destroyImage(image, imageAllocation);
        allocator.destroyBuffer(buffer, bufferAllocation);
        throw;
    }
    allocator.destroyImage(image, imageAllocation);
    allocator.destroyBuffer(buffer, bufferAllocation);
    if (allocator.statistics().allocationCount != 0) {
        throw std::runtime_error(
            "VMA smoke allocations were not fully released");
    }
}

} // namespace

int main()
{
    try {
        const SdlVideo video;
        const SdlWindow window;
        sokoban::VulkanDeviceContext deviceContext(window.get());
        exerciseMemoryAllocator(deviceContext);
        submitNoOp(deviceContext);
        std::cout << "Vulkan hidden-surface smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkan smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
