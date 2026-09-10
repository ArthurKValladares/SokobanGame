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
#include <string_view>
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
    const std::filesystem::path assetRoot =
        std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) / "assets";
    const sokoban::RuntimeTextureCatalog textureCatalog =
        sokoban::collectRuntimeTextureCatalog(assetRoot, manifest);
    const sokoban::RenderModel model = manifest.modelIdByName("RetryRig");

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
    resources.requestAssets(requirements, sokoban::AssetLoadPriority::Prefetch);
    const sokoban::VulkanModelResources::LoadingStats queued =
        resources.loadingStats();
    if (queued.modelStages.queued != 1 ||
        queued.modelStages.queuedSourceBytes == 0) {
        throw std::runtime_error(
            "Queued model did not expose its cached source-byte estimate");
    }
    resources.cancelQueuedPrefetches();
    resources.requestAssets(requirements);
    const sokoban::VulkanModelResources::LoadingStats requeued =
        resources.loadingStats();
    if (requeued.modelStages.queuedSourceBytes !=
        queued.modelStages.queuedSourceBytes) {
        throw std::runtime_error(
            "Queue cancellation discarded the model source-byte estimate");
    }
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
        denied.modelStages.residencyDeferredAssets != 1 ||
        denied.modelStages.residencyDeferrals == 0 ||
        denied.skinnedPackingPasses != 0 ||
        denied.skinnedPackingAllocations != 0 ||
        denied.skinnedPackingTemporaryBytes != 0 ||
        denied.skinnedUploadBytes != 0 ||
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
        uploading.modelStages.residencyDeferredAssets != 0 ||
        uploading.modelStages.residencyDeferrals == 0 ||
        uploading.modelStages.residencyDeferredMicroseconds == 0 ||
        uploading.skinnedPackingPasses != 1 ||
        uploading.skinnedPackingAllocations != 2 ||
        uploading.skinnedPackingTemporaryBytes == 0 ||
        uploading.skinnedPackingPeakBytes !=
            uploading.skinnedPackingTemporaryBytes ||
        uploading.skinnedUploadBytes == 0 ||
        uploading.skinnedPackingTemporaryBytes <
            uploading.skinnedUploadBytes ||
        uploading.transientAssetBytes !=
            uploading.modelStages.uploadInFlightBytes ||
        uploading.transientAssetPeakBytes <
            denied.transientAssetBytes +
                uploading.modelStages.uploadInFlightBytes +
                uploading.skinnedPackingPeakBytes) {
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

struct PressureMetrics {
    uint64_t queuedModelSourceBytes = 0;
    uint64_t preparedModelBytes = 0;
    uint64_t transientPeakBytes = 0;
    uint64_t deferralAttempts = 0;
    uint64_t deferredMicroseconds = 0;
    uint64_t elapsedMicroseconds = 0;
    uint64_t preparedBudgetBytes = 0;
    uint64_t preparedBudgetDeferrals = 0;
    uint64_t packingPasses = 0;
    uint64_t packingAllocations = 0;
    uint64_t packingTemporaryBytes = 0;
    uint64_t packingPeakBytes = 0;
    uint64_t skinnedUploadBytes = 0;
    uint32_t heldReadyModels = 0;
    uint32_t heldQueuedModels = 0;
};

class ModelResidencyHold final {
public:
    ModelResidencyHold()
    {
        sokoban::VulkanModelResources::setModelResidencyDeniedForTesting(true);
    }

    ~ModelResidencyHold()
    {
        sokoban::VulkanModelResources::setModelResidencyDeniedForTesting(false);
    }

    ModelResidencyHold(const ModelResidencyHold&) = delete;
    ModelResidencyHold& operator=(const ModelResidencyHold&) = delete;
};

constexpr uint32_t pressureModelCount = 32;
constexpr uint64_t pressurePreparedBudgetBytes = 4ULL * 1024ULL * 1024ULL;

std::string pressureManifestJson()
{
    std::string result = R"json({
  "format": 1,
  "models": [
)json";
    for (uint32_t index = 0; index < pressureModelCount; ++index) {
        if (index != 0) {
            result += ",\n";
        }
        result += "    { \"name\": \"PressureRig" +
            std::to_string(index) +
            "\", \"path\": \"KayKit Adventurers 2.0/Characters/gltf/"
            "Rogue.glb\", \"geometry\": \"skinned\"";
        if (index == 0) {
            result += ", \"role\": \"player\"";
        }
        result += " }";
    }
    result += R"json(
  ],
  "animations": [
    { "name": "Idle", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb", "clip": 8, "role": "player-idle" },
    { "name": "Move", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb", "clip": 7, "role": "player-move" },
    { "name": "Push", "path": "custom/Rig_Medium_Push.glb", "clip": 1, "role": "player-push" },
    { "name": "Death", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb", "clip": 3, "role": "player-death" },
    { "name": "DeadIdle", "path": "KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_General.glb", "clip": 4, "role": "player-dead-idle" }
  ]
})json";
    return result;
}

PressureMetrics exercisePreparedAssetPressure(
    sokoban::VulkanDeviceContext& deviceContext)
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::parse(pressureManifestJson());
    const std::filesystem::path assetRoot =
        std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) / "assets";
    const sokoban::RuntimeTextureCatalog textureCatalog =
        sokoban::collectRuntimeTextureCatalog(assetRoot, manifest);

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
            .maxConcurrentCpuJobs = 2,
            .maxPublicationsPerFrame = 1,
            .preparedAssetBytes = pressurePreparedBudgetBytes,
        });

    sokoban::RenderAssetRequirements requirements;
    for (uint32_t modelIndex = 0; modelIndex < manifest.models().size();
         ++modelIndex) {
        requirements.requireModel(sokoban::RenderModel { modelIndex + 1 });
    }
    resources.requestAssets(requirements, sokoban::AssetLoadPriority::Prefetch);
    const sokoban::VulkanModelResources::LoadingStats queued =
        resources.loadingStats();
    if (queued.modelStages.queued != manifest.models().size() ||
        queued.modelStages.queuedSourceBytes == 0) {
        throw std::runtime_error(
            "Pressure workload did not queue every model with a source estimate");
    }

    const auto started = std::chrono::steady_clock::now();
    sokoban::VulkanModelResources::LoadingStats held;
    {
        ModelResidencyHold hold;
        const auto deadline = started + std::chrono::seconds(20);
        do {
            (void)resources.publishReadyAssets(1);
            held = resources.loadingStats();
            if (held.preparedBudgetDeferrals != 0 &&
                held.modelStages.cpuReady != 0 &&
                held.modelStages.queued != 0 &&
                held.modelStages.decoding == 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);

        if (held.modelStages.cpuReady == 0 ||
            held.modelStages.queued == 0 ||
            held.modelStages.cpuReadyBytes == 0 ||
            held.modelStages.residencyDeferredAssets !=
                held.modelStages.cpuReady ||
            held.preparedAssetBytes != held.modelStages.cpuReadyBytes ||
            held.activeDecodeReservationBytes != 0 ||
            held.preparedAssetBudgetBytes != pressurePreparedBudgetBytes ||
            held.preparedAssetBytes > held.preparedAssetBudgetBytes ||
            held.preparedBudgetDeferrals == 0 ||
            held.transientAssetPeakBytes < held.modelStages.cpuReadyBytes) {
            throw std::runtime_error(
                "Pressure workload did not pause decoding at its prepared "
                "budget: ready=" +
                std::to_string(held.modelStages.cpuReady) +
                ", queued=" + std::to_string(held.modelStages.queued) +
                ", decoding=" + std::to_string(held.modelStages.decoding) +
                ", prepared=" + std::to_string(held.preparedAssetBytes) +
                ", reserved=" +
                std::to_string(held.activeDecodeReservationBytes) +
                ", deferrals=" +
                std::to_string(held.preparedBudgetDeferrals));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto drainDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(20);
    while (resources.loadingStats().loadedModels != manifest.models().size() &&
        std::chrono::steady_clock::now() < drainDeadline) {
        (void)resources.publishReadyAssets(1);
        resources.retireCompletedUploads();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const sokoban::VulkanModelResources::LoadingStats drained =
        resources.loadingStats();
    if (drained.loadedModels != manifest.models().size() ||
        drained.pendingModels != 0 || drained.failedAssets != 0 ||
        drained.modelStages.cpuReadyBytes != 0 ||
        drained.modelStages.residencyDeferredAssets != 0 ||
        drained.modelStages.residencyDeferrals < held.modelStages.cpuReady ||
        drained.modelStages.residencyDeferredMicroseconds == 0 ||
        drained.skinnedPackingPasses != manifest.models().size() ||
        drained.skinnedPackingAllocations !=
            2 * manifest.models().size() ||
        drained.skinnedPackingTemporaryBytes < drained.skinnedUploadBytes ||
        drained.skinnedPackingPeakBytes == 0 ||
        drained.skinnedUploadBytes == 0) {
        throw std::runtime_error(
            "Pressure workload did not make progress after admission resumed: "
            "loaded=" + std::to_string(drained.loadedModels) +
            ", pending=" + std::to_string(drained.pendingModels) +
            ", failed=" + std::to_string(drained.failedAssets) +
            ", queued=" + std::to_string(drained.modelStages.queued) +
            ", decoding=" + std::to_string(drained.modelStages.decoding) +
            ", ready=" + std::to_string(drained.modelStages.cpuReady) +
            ", prepared deferrals=" +
            std::to_string(drained.preparedBudgetDeferrals) +
            ", residency-deferred=" +
            std::to_string(
                drained.modelStages.residencyDeferredAssets) +
            ", residency deferrals=" +
            std::to_string(drained.modelStages.residencyDeferrals) +
            ", packing passes=" +
            std::to_string(drained.skinnedPackingPasses));
    }

    return {
        .queuedModelSourceBytes = queued.modelStages.queuedSourceBytes,
        .preparedModelBytes = held.modelStages.cpuReadyBytes,
        .transientPeakBytes = drained.transientAssetPeakBytes,
        .deferralAttempts = drained.modelStages.residencyDeferrals,
        .deferredMicroseconds =
            drained.modelStages.residencyDeferredMicroseconds,
        .elapsedMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count()),
        .preparedBudgetBytes = drained.preparedAssetBudgetBytes,
        .preparedBudgetDeferrals = drained.preparedBudgetDeferrals,
        .packingPasses = drained.skinnedPackingPasses,
        .packingAllocations = drained.skinnedPackingAllocations,
        .packingTemporaryBytes = drained.skinnedPackingTemporaryBytes,
        .packingPeakBytes = drained.skinnedPackingPeakBytes,
        .skinnedUploadBytes = drained.skinnedUploadBytes,
        .heldReadyModels = held.modelStages.cpuReady,
        .heldQueuedModels = held.modelStages.queued,
    };
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const SdlVideo video;
        const SdlWindow window;
        sokoban::VulkanDeviceContext deviceContext(window.get());
        exerciseMemoryAllocator(deviceContext);
        exerciseSkinnedPublicationRetry(deviceContext);
        const PressureMetrics pressure =
            exercisePreparedAssetPressure(deviceContext);
        if (argc == 2 &&
            std::string_view(argv[1]) == "--benchmark-prepared-assets") {
            std::cout
                << "prepared_asset_pressure queued_model_source_bytes="
                << pressure.queuedModelSourceBytes
                << " prepared_model_bytes=" << pressure.preparedModelBytes
                << " transient_peak_bytes=" << pressure.transientPeakBytes
                << " deferral_attempts=" << pressure.deferralAttempts
                << " deferred_us=" << pressure.deferredMicroseconds
                << " prepared_budget_bytes="
                << pressure.preparedBudgetBytes
                << " prepared_budget_deferrals="
                << pressure.preparedBudgetDeferrals
                << " packing_passes=" << pressure.packingPasses
                << " packing_allocations=" << pressure.packingAllocations
                << " packing_temporary_bytes="
                << pressure.packingTemporaryBytes
                << " packing_peak_bytes=" << pressure.packingPeakBytes
                << " skinned_upload_bytes=" << pressure.skinnedUploadBytes
                << " held_ready_models=" << pressure.heldReadyModels
                << " held_queued_models=" << pressure.heldQueuedModels
                << " elapsed_us=" << pressure.elapsedMicroseconds << '\n';
        }
        submitNoOp(deviceContext);
        std::cout << "Vulkan hidden-surface smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkan smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
