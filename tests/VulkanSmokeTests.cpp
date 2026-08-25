#include "engine/render/VulkanDeviceContext.hpp"

#include <SDL3/SDL.h>

#include <chrono>
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

} // namespace

int main()
{
    try {
        const SdlVideo video;
        const SdlWindow window;
        sokoban::VulkanDeviceContext deviceContext(window.get());
        submitNoOp(deviceContext);
        std::cout << "Vulkan hidden-surface smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkan smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
