#include "engine/AssetManifest.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/VulkanDeviceContext.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanModelResources.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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

void exerciseSkinnedPublicationRetry(
    sokoban::VulkanDeviceContext& deviceContext)
{
    constexpr std::string_view manifestJson = R"json({
  "format": 1,
  "models": [
    {
      "name": "RetryRig",
      "path": "KayKit Adventurers 2.0/Characters/gltf/Rogue.glb",
      "geometry": "skinned",
      "role": "player"
    }
  ],
  "animations": [
    { "name": "Idle", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb", "clip": 8, "role": "player-idle" },
    { "name": "Move", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb", "clip": 7, "role": "player-move" },
    { "name": "Push", "path": "custom/Rig_Medium_Push.glb", "clip": 1, "role": "player-push" },
    { "name": "Death", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb", "clip": 3, "role": "player-death" },
    { "name": "DeadIdle", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb", "clip": 4, "role": "player-dead-idle" }
  ]
})json";
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::parse(manifestJson);
    const sokoban::RuntimeTextureCatalog textureCatalog =
        sokoban::buildRuntimeTextureCatalog(manifest, {});
    const sokoban::RenderModel model = manifest.modelIdByName("RetryRig");
    const std::filesystem::path assetRoot =
        std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) / "assets";

    sokoban::VulkanModelResources resources;
    resources.create(
        deviceContext.physicalDevice(),
        deviceContext.memoryAllocator(),
        deviceContext.device(),
        deviceContext.commandPool(),
        deviceContext.graphicsQueue(),
        assetRoot,
        manifest,
        textureCatalog,
        1,
        1.0f,
        {
            .maxConcurrentCpuJobs = 1,
            .maxPublicationsPerFrame = 1,
        });

    sokoban::RenderAssetRequirements requirements;
    requirements.requireModel(model);
    resources.requestAssets(requirements);
    sokoban::VulkanModelResources::denyNextModelResidencyForTesting();

    const auto admissionDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (sokoban::VulkanModelResources::
            modelResidencyDenialPendingForTesting() &&
        std::chrono::steady_clock::now() < admissionDeadline) {
        (void)resources.publishReadyAssets(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (sokoban::VulkanModelResources::
            modelResidencyDenialPendingForTesting()) {
        throw std::runtime_error(
            "Timed out before the skinned model reached residency admission");
    }

    const sokoban::VulkanModelResources::LoadingStats denied =
        resources.loadingStats();
    if (resources.modelReady(model) || denied.failedAssets != 0 ||
        denied.pendingModels != 1 || denied.modelResidencyBytes != 0 ||
        denied.modelStages.cpuReady != 1 ||
        denied.modelStages.cpuReadyBytes == 0 ||
        denied.transientAssetBytes != denied.modelStages.cpuReadyBytes ||
        denied.transientAssetPeakBytes < denied.transientAssetBytes) {
        throw std::runtime_error(
            "Residency denial did not preserve a retryable CPU-ready model");
    }

    (void)resources.publishReadyAssets(1);
    const sokoban::VulkanModelResources::LoadingStats uploading =
        resources.loadingStats();
    if (uploading.modelStages.cpuReady != 0 ||
        uploading.modelStages.cpuReadyBytes != 0 ||
        uploading.modelStages.uploading != 1 ||
        uploading.modelStages.uploadInFlightBytes == 0 ||
        uploading.transientAssetBytes !=
            uploading.modelStages.uploadInFlightBytes ||
        uploading.transientAssetPeakBytes <
            denied.transientAssetBytes +
                uploading.modelStages.uploadInFlightBytes) {
        throw std::runtime_error(
            "Model publication did not transfer CPU-ready bytes into upload staging");
    }

    const auto retryDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    while (!resources.modelReady(model) &&
        std::chrono::steady_clock::now() < retryDeadline) {
        (void)resources.publishReadyAssets(1);
        resources.retireCompletedUploads();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const sokoban::VulkanModelResources::LoadingStats retried =
        resources.loadingStats();
    if (!resources.modelReady(model) || retried.failedAssets != 0 ||
        retried.loadedModels != 1 || retried.pendingModels != 0 ||
        retried.modelResidencyBytes == 0) {
        throw std::runtime_error(
            "Retried skinned model did not publish its prepared geometry");
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
        exerciseSkinnedPublicationRetry(deviceContext);
        submitNoOp(deviceContext);
        std::cout << "Vulkan hidden-surface smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkan smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
