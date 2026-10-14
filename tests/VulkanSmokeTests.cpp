#include "ScopedTestDirectory.hpp"
#include "GroundGpuFixture.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/AnimationCatalog.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/FrameRetirementQueue.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/VulkanDeviceContext.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanGpuProfiler.hpp"
#include "engine/render/VulkanGeometryArena.hpp"
#include "engine/render/VulkanGroundChunkCache.hpp"
#include "engine/render/VulkanMemoryAllocator.hpp"
#include "engine/render/VulkanModelResources.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/render/VulkanThumbnailPass.hpp"
#include "engine/render/VulkanTextureUploader.hpp"
#include "engine/ui/FontAtlas.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
    explicit SdlWindow(int width = 32, int height = 32)
    {
        window_ = SDL_CreateWindow(
            "Sokoban Vulkan smoke test", width, height,
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

using Clock = std::chrono::steady_clock;

[[nodiscard]] uint64_t elapsedMicroseconds(Clock::time_point started)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - started).count());
}

class ResidencyHold final {
public:
    ResidencyHold()
    {
        sokoban::VulkanModelResources::setModelResidencyDeniedForTesting(true);
        sokoban::VulkanModelResources::setTextureResidencyDeniedForTesting(true);
    }

    ~ResidencyHold()
    {
        release();
    }

    ResidencyHold(const ResidencyHold&) = delete;
    ResidencyHold& operator=(const ResidencyHold&) = delete;

    void release()
    {
        if (!held_) {
            return;
        }
        sokoban::VulkanModelResources::setTextureResidencyDeniedForTesting(false);
        sokoban::VulkanModelResources::setModelResidencyDeniedForTesting(false);
        held_ = false;
    }

private:
    bool held_ = true;
};

struct StartupMetrics {
    uint64_t packageValidationMicroseconds = 0;
    uint64_t manifestInspectionMicroseconds = 0;
    uint64_t requirementDiscoveryMicroseconds = 0;
    uint64_t gltfDependencyDiscoveryMicroseconds = 0;
    uint64_t deviceSetupMicroseconds = 0;
    uint64_t resourceSetupMicroseconds = 0;
    uint64_t decodeMicroseconds = 0;
    uint64_t uploadMicroseconds = 0;
    uint64_t readyMicroseconds = 0;
    uint32_t requestedModels = 0;
    uint32_t requestedTextures = 0;
    uint32_t requestedAnimations = 0;
};

[[nodiscard]] sokoban::RenderAssetRequirements startupAssetRequirements(
    const std::filesystem::path& assetRoot,
    const sokoban::AssetManifest& manifest,
    const sokoban::AnimationCatalog& animations)
{
    sokoban::RenderAssetRequirements requirements =
        sokoban::renderAssetRequirementsForLevel(
            sokoban::OverworldMap::load(
                assetRoot / "levels" / "overworld").level(),
            manifest);

    const std::filesystem::path levelDirectory =
        sokoban::levelDirectoryPath(assetRoot / "levels", 0);
    for (int screen = 0;; ++screen) {
        const std::filesystem::path path =
            sokoban::screenFilePath(levelDirectory, screen);
        if (!std::filesystem::exists(path)) {
            break;
        }
        requirements.merge(sokoban::renderAssetRequirementsForLevel(
            sokoban::Level::loadFromFile(path),
            manifest,
            sokoban::LevelLocation { .level = 0, .screen = screen },
            &animations));
    }
    return requirements;
}

[[nodiscard]] StartupMetrics benchmarkStartup(SDL_Window* window)
{
    StartupMetrics result;
    const Clock::time_point benchmarkStarted = Clock::now();
    const std::filesystem::path assetRoot =
        std::filesystem::path(SOKOBAN_VULKAN_TEST_ASSETS);

    Clock::time_point phaseStarted = Clock::now();
    sokoban::validateContentPackage(assetRoot, SOKOBAN_GAME_VERSION);
    result.packageValidationMicroseconds = elapsedMicroseconds(phaseStarted);

    phaseStarted = Clock::now();
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(assetRoot / "manifest.json");
    const sokoban::AnimationCatalog animations =
        sokoban::AnimationCatalog::loadFromFile(
            assetRoot / "animation_catalog.json", manifest);
    result.manifestInspectionMicroseconds = elapsedMicroseconds(phaseStarted);

    phaseStarted = Clock::now();
    const sokoban::RenderAssetRequirements requirements =
        startupAssetRequirements(assetRoot, manifest, animations);
    result.requirementDiscoveryMicroseconds = elapsedMicroseconds(phaseStarted);

    phaseStarted = Clock::now();
    const sokoban::RuntimeTextureCatalog textureCatalog =
        sokoban::collectRuntimeTextureCatalog(assetRoot, manifest);
    result.gltfDependencyDiscoveryMicroseconds =
        elapsedMicroseconds(phaseStarted);

    phaseStarted = Clock::now();
    sokoban::VulkanDeviceContext deviceContext(
        window, textureCatalog.textures().size());
    result.deviceSetupMicroseconds = elapsedMicroseconds(phaseStarted);

    phaseStarted = Clock::now();
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
        deviceContext.textureDescriptorCapacity(),
        deviceContext.maxSamplerAnisotropy(),
        {
            .maxConcurrentCpuJobs = 2,
            .maxPublicationsPerFrame = 1,
            .preparedAssetBytes = 1024ULL * 1024ULL * 1024ULL,
        });
    result.resourceSetupMicroseconds = elapsedMicroseconds(phaseStarted);

    ResidencyHold hold;
    phaseStarted = Clock::now();
    resources.requestAssets(requirements);
    sokoban::VulkanModelResources::LoadingStats stats;
    const Clock::time_point decodeDeadline =
        phaseStarted + std::chrono::seconds(30);
    do {
        (void)resources.publishReadyAssets(
            std::numeric_limits<std::size_t>::max());
        stats = resources.loadingStats();
        const uint32_t preparedAssets =
            stats.modelStages.cpuReady + stats.textureStages.cpuReady +
            stats.animationStages.resident;
        if (stats.queuedAssets == 0 && stats.activeCpuJobs == 0 &&
            preparedAssets == stats.requestedAssets) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (Clock::now() < decodeDeadline);
    result.decodeMicroseconds = elapsedMicroseconds(phaseStarted);
    if (stats.failedAssets != 0 || stats.queuedAssets != 0 ||
        stats.activeCpuJobs != 0 ||
        stats.modelStages.cpuReady + stats.textureStages.cpuReady +
                stats.animationStages.resident !=
            stats.requestedAssets) {
        throw std::runtime_error(
            "Startup benchmark did not reach the CPU-ready boundary");
    }
    result.requestedModels = stats.modelStages.cpuReady;
    result.requestedTextures = stats.textureStages.cpuReady;
    result.requestedAnimations = stats.animationStages.resident;

    hold.release();
    phaseStarted = Clock::now();
    const Clock::time_point uploadDeadline =
        phaseStarted + std::chrono::seconds(30);
    do {
        resources.retireCompletedUploads();
        (void)resources.publishReadyAssets(1);
        stats = resources.loadingStats();
        if (stats.readyRequestedAssets == stats.requestedAssets) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (Clock::now() < uploadDeadline);
    result.uploadMicroseconds = elapsedMicroseconds(phaseStarted);
    result.readyMicroseconds = elapsedMicroseconds(benchmarkStarted);
    if (stats.failedAssets != 0 ||
        stats.readyRequestedAssets != stats.requestedAssets) {
        throw std::runtime_error(
            "Startup benchmark did not make every requested asset resident");
    }
    return result;
}

void submitNoOp(sokoban::VulkanDeviceContext& deviceContext)
{
    sokoban::VulkanGpuProfiler profiler;
    profiler.create(deviceContext.device(), deviceContext.timestampPeriodNanoseconds(),
        deviceContext.graphicsTimestampValidBits(), 1);
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
    // Deliberately omit all optional phases. One unwritten query previously
    // made the entire pool return NOT_READY and erased every GPU measurement.
    profiler.beginFrame(commandBuffer, 0);
    profiler.beginPhase(commandBuffer, 0, sokoban::VulkanGpuPhase::Scene);
    profiler.endPhase(commandBuffer, 0, sokoban::VulkanGpuPhase::Scene);
    profiler.endFrame(commandBuffer, 0);
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
    if (profiler.supported()) {
        profiler.markSubmitted(0);
        profiler.collectCompletedFrame(0);
        const auto frame = profiler.frameTimeSummary();
        const auto scene = profiler.phaseTimeSummary(sokoban::VulkanGpuPhase::Scene);
        const auto absent = profiler.phaseTimeSummary(sokoban::VulkanGpuPhase::AtmosphereVolumes);
        if (frame.sampleCount != 1 || scene.sampleCount != 1 ||
            absent.sampleCount != 1 || absent.maximumMilliseconds != 0.0) {
            throw std::runtime_error("Optional GPU phases suppressed completed frame timestamps");
        }
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

void exerciseGeometryArena(sokoban::VulkanDeviceContext& deviceContext)
{
    using namespace sokoban;
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    constexpr VkDeviceSize mebibyte = 1024ULL * 1024ULL;
    VulkanMemoryAllocator& allocator = deviceContext.memoryAllocator();
    const VulkanMemoryStatistics before = allocator.statistics();
    const uint64_t validationErrors = vulkanDebug::validationErrorCount();
    {
        VulkanUploadRing ring;
        ring.create(allocator, deviceContext.device(), 512);
        const VulkanMemoryStatistics ringOnly = allocator.statistics();
        VulkanGeometryArena arena;
        arena.create(allocator, deviceContext.device(), deviceContext.commandPool(),
            deviceContext.graphicsQueue(), ring);
        const auto requireEmpty = [&] {
            const auto geometry = arena.statistics();
            const auto memory = allocator.statistics();
            require(geometry.usedBytes() == 0 && geometry.reservedBytes() == 0 &&
                    geometry.blockCount() == 0 &&
                    memory.bufferBytes == ringOnly.bufferBytes &&
                    memory.bufferCount == ringOnly.bufferCount,
                "Empty geometry arena retained backing Vulkan allocations");
        };
        requireEmpty();

        // Each request exceeds every previously released vertex block. Keeping
        // empty blocks used to retain 154 MiB after this sequence, with no
        // live slices left. Check the actual VMA buffer allocations as well as
        // the arena's own counters, so hiding an empty block cannot pass.
        for (VkDeviceSize vertexMebibytes : { 33ULL, 34ULL, 35ULL, 36ULL }) {
            auto allocation = arena.allocate(vertexMebibytes * mebibyte, 4);
            const auto active = arena.statistics();
            require(active.vertex.usedBytes == vertexMebibytes * mebibyte &&
                    active.vertex.reservedBytes == vertexMebibytes * mebibyte &&
                    active.vertex.blockCount == 1 && active.index.usedBytes == 4 &&
                    active.index.reservedBytes == 16 * mebibyte &&
                    active.index.blockCount == 1 && active.blockCount() == 2,
                "Geometry statistics did not distinguish live payload and backing capacity");
            const auto backing = allocator.statistics();
            require(backing.bufferBytes - ringOnly.bufferBytes >= active.reservedBytes() &&
                    backing.bufferCount - ringOnly.bufferCount == active.blockCount(),
                "Geometry reservation statistics disagreed with live Vulkan buffers");
            arena.release(allocation);
            require(!allocation.valid(), "Released geometry allocation remained valid");
            requireEmpty();
            std::cout << "geometry_reclamation vertex_request_mib="
                      << vertexMebibytes << " live_slice_bytes=0 retained_buffer_bytes="
                      << allocator.statistics().bufferBytes - ringOnly.bufferBytes << '\n';
        }

        // Index slices have alignment gaps; live bytes count requested data,
        // rather than interpreting either padding or capacity as resident data.
        auto first = arena.allocate(64, 12);
        auto peer = arena.allocate(80, 20);
        const VkBuffer peerVertex = arena.vertexBuffer(peer);
        const VkBuffer peerIndex = arena.indexBuffer(peer);
        require(first.vertexBlock == peer.vertexBlock && first.indexBlock == peer.indexBlock &&
                arena.statistics().vertex.usedBytes == 144 &&
                arena.statistics().index.usedBytes == 32 &&
                arena.statistics().reservedBytes() == 48 * mebibyte,
            "Shared geometry blocks or exact live-payload accounting changed");
        arena.release(first);
        require(arena.statistics().usedBytes() == 100 &&
                arena.statistics().reservedBytes() == 48 * mebibyte &&
                arena.vertexBuffer(peer) == peerVertex && arena.indexBuffer(peer) == peerIndex,
            "Releasing one slice destroyed a block still used by its live peer");
        arena.release(peer);
        requireEmpty();

        // Occupy two complete block pairs, then free the first pair. Later
        // allocations must reuse its vacant slots without renumbering the
        // second pair, whose handles may already be recorded in commands.
        first = arena.allocate(32 * mebibyte, 16 * mebibyte);
        peer = arena.allocate(32 * mebibyte, 16 * mebibyte);
        const uint32_t vacantVertex = first.vertexBlock;
        const uint32_t vacantIndex = first.indexBlock;
        const uint32_t stableVertex = peer.vertexBlock;
        const uint32_t stableIndex = peer.indexBlock;
        const VkBuffer stableVertexBuffer = arena.vertexBuffer(peer);
        const VkBuffer stableIndexBuffer = arena.indexBuffer(peer);
        arena.release(first);
        require(arena.statistics().reservedBytes() == 48 * mebibyte &&
                peer.vertexBlock == stableVertex && peer.indexBlock == stableIndex &&
                arena.vertexBuffer(peer) == stableVertexBuffer &&
                arena.indexBuffer(peer) == stableIndexBuffer,
            "Freeing a lower geometry block invalidated a surviving allocation");
        auto replacement = arena.allocate(64, 16);
        require(replacement.vertexBlock == vacantVertex && replacement.indexBlock == vacantIndex &&
                arena.vertexBuffer(peer) == stableVertexBuffer &&
                arena.indexBuffer(peer) == stableIndexBuffer &&
                arena.statistics().blockCount() == 4,
            "New geometry blocks did not reuse vacant slots safely");
        arena.release(replacement);
        arena.release(peer);
        requireEmpty();

        // Geometry release follows the renderer's retirement decision. A
        // completed unrelated frame, or only one relevant frame, cannot free
        // the backing buffers while another recorded frame still uses them.
        FrameRetirementQueue<VulkanGeometryArena::Allocation> retired;
        auto pending = arena.allocate(64, 16);
        retired.retire(pending, 0b101);
        pending = {};
        const auto drain = [&] {
            retired.drainCompleted([&](auto& allocation) { arena.release(allocation); });
        };
        drain();
        retired.completeFrame(1);
        drain();
        require(retired.size() == 1 && arena.statistics().usedBytes() == 80 &&
                arena.statistics().reservedBytes() == 48 * mebibyte,
            "An unrelated frame completion reclaimed pending geometry");
        retired.completeFrame(0);
        drain();
        require(retired.size() == 1 && arena.statistics().blockCount() == 2,
            "Geometry backing buffers were released before every pending frame completed");
        retired.completeFrame(2);
        drain();
        require(retired.empty(), "Completed geometry retirement remained queued");
        requireEmpty();

        // Exercise the real transfer submission and wait for its fence before
        // releasing either staging storage or the geometry destination.
        const std::array<uint32_t, 16> vertices {};
        const std::array<uint32_t, 4> indices {};
        auto uploaded = arena.allocate(sizeof(vertices), sizeof(indices));
        auto upload = arena.beginUpload(uploaded, vertices.data(), sizeof(vertices),
            indices.data(), sizeof(indices));
        try {
            require(upload.submitted && upload.fence && ring.usedBytes() == 80,
                "Geometry upload did not retain its staging reservation");
            require(vkWaitForFences(deviceContext.device(), 1, &upload.fence, VK_TRUE,
                    5'000'000'000ULL) == VK_SUCCESS && arena.uploadComplete(upload),
                "Geometry transfer did not complete before reclamation");
            require(arena.statistics().usedBytes() == 80 &&
                    arena.statistics().blockCount() == 2,
                "Completing an upload released geometry still owned by its caller");
        } catch (...) {
            deviceContext.waitIdle();
            arena.destroyUpload(upload);
            throw;
        }
        arena.destroyUpload(upload);
        require(!upload.submitted && !upload.staging.valid() && ring.usedBytes() == 0,
            "Completed geometry upload did not release its staging storage");
        arena.release(uploaded);
        requireEmpty();

        // A full upload ring fails before submission. The caller rolls back
        // its geometry allocation while preserving the unrelated reservation.
        const auto heldStaging = ring.reserve(ring.capacity(), 16);
        require(heldStaging.has_value(), "Could not fill geometry test upload ring");
        auto failedUpload = arena.allocate(sizeof(vertices), sizeof(indices));
        bool uploadRejected = false;
        try {
            const auto unexpected = arena.beginUpload(failedUpload, vertices.data(),
                sizeof(vertices), indices.data(), sizeof(indices));
            deviceContext.waitIdle();
            auto cleanup = unexpected;
            arena.destroyUpload(cleanup);
        } catch (const std::runtime_error&) {
            uploadRejected = true;
        }
        require(uploadRejected && ring.usedBytes() == ring.capacity(),
            "Failed geometry upload consumed or released unrelated staging storage");
        arena.release(failedUpload);
        requireEmpty();
        ring.abandon(*heldStaging);
        require(ring.usedBytes() == 0, "Abandoned test reservation remained in upload ring");

        // Inject the second-half failure at the arena seam, never by requesting
        // unsafe GPU sizes or relying on device-specific out-of-memory behavior.
        arena.failNextIndexBlockCreationForTesting();
        bool indexRejected = false;
        try {
            auto unexpected = arena.allocate(64, 16);
            arena.release(unexpected);
        } catch (const std::runtime_error&) {
            indexRejected = true;
        }
        require(indexRejected, "Geometry index failure injection did not run");
        requireEmpty();

        peer = arena.allocate(64, 16 * mebibyte);
        const VkBuffer rollbackPeerVertex = arena.vertexBuffer(peer);
        const VkBuffer rollbackPeerIndex = arena.indexBuffer(peer);
        arena.failNextIndexBlockCreationForTesting();
        indexRejected = false;
        try {
            auto unexpected = arena.allocate(32, 16);
            arena.release(unexpected);
        } catch (const std::runtime_error&) {
            indexRejected = true;
        }
        require(indexRejected && arena.statistics().vertex.usedBytes == 64 &&
                arena.statistics().index.usedBytes == 16 * mebibyte &&
                arena.statistics().reservedBytes() == 48 * mebibyte &&
                arena.vertexBuffer(peer) == rollbackPeerVertex &&
                arena.indexBuffer(peer) == rollbackPeerIndex,
            "Failed index allocation damaged a shared live vertex block");
        arena.release(peer);
        requireEmpty();

        first = arena.allocate(32 * mebibyte, 16 * mebibyte);
        peer = arena.allocate(64, 16);
        arena.destroy();
        requireEmpty();
        arena.destroy();
        requireEmpty();
        arena.create(allocator, deviceContext.device(), deviceContext.commandPool(),
            deviceContext.graphicsQueue(), ring);
        auto recreated = arena.allocate(16, 4);
        arena.release(recreated);
        arena.release(recreated);
        requireEmpty();
    }
    require(allocator.statistics().bufferBytes == before.bufferBytes &&
            allocator.statistics().bufferCount == before.bufferCount,
        "Geometry arena fixture leaked Vulkan buffers");
    require(vulkanDebug::validationErrorCount() == validationErrors,
        "Vulkan validation reported a geometry arena lifecycle error");
    std::cout << "Geometry arena reclamation, shared owners, stable slots, frame retirement, "
                 "transfer completion, rollback and teardown passed; validation_active="
              << vulkanDebug::validationActive() << '\n';
}

void exerciseGroundChunkCacheLifecycle(sokoban::VulkanDeviceContext& deviceContext)
{
    using namespace sokoban;
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto geometryAt = [](int origin) {
        std::array<RenderFrameData::Tile, 2> tiles;
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            auto& tile = tiles[index];
            tile.cell = { origin + static_cast<int>(index) * groundChunkCellWidth, 0, 0 };
            tile.position = { static_cast<float>(tile.cell.x), 0 };
            tile.size = { 1, 1 };
            tile.color = { 1, 1, 1, 1 };
            tile.height = 1;
            tile.model = { 1 };
            tile.effect = RenderSurfaceEffect::GroundSplat;
            tile.groundTop = true;
            tile.groundGeometryEligible = true;
        }
        return std::make_shared<const GroundChunkGeometry>(
            compileGroundChunkGeometry(tiles, nullptr, false));
    };

    VulkanMemoryAllocator& allocator = deviceContext.memoryAllocator();
    const auto baseline = allocator.statistics();
    VulkanGroundChunkCache cache;
    cache.create(allocator, deviceContext.device(), deviceContext.commandPool(),
        deviceContext.graphicsQueue());
    std::array<VkCommandBuffer, 2> referencingFrames {};
    std::array<VkFence, 2> referencingFences {};
    const auto cleanup = [&] {
        deviceContext.waitIdle();
        for (std::size_t index = 0; index < referencingFrames.size(); ++index) {
            if (referencingFences[index]) {
                vkDestroyFence(deviceContext.device(), referencingFences[index], nullptr);
                referencingFences[index] = VK_NULL_HANDLE;
            }
            if (referencingFrames[index]) {
                vkFreeCommandBuffers(deviceContext.device(), deviceContext.commandPool(),
                    1, &referencingFrames[index]);
                referencingFrames[index] = VK_NULL_HANDLE;
            }
        }
        cache.destroy();
    };
    try {
        const auto first = geometryAt(0);
        const auto second = geometryAt(-16);
        require(first->chunks.size() == 2, "Chunk lifecycle fixture did not create two chunks");
        require(!cache.update(first, 0) && !cache.readyFor(first.get()) &&
                cache.mesh(first.get(), 0).buffer == VK_NULL_HANDLE && cache.uploadCount() == 1 &&
                cache.stagingBytes() == first->geometryBytes,
            "Ground chunk geometry published before its upload fence was collected");
        deviceContext.waitIdle();
        require(!cache.update({}, 0) && cache.readyFor(first.get()) && cache.stagingBytes() == 0,
            "Disabling chunks left a completed upload's staging allocation retained");
        require(cache.update(first, 0) && cache.readyFor(first.get()),
            "Ground chunk upload did not publish after queue completion");
        const auto firstMesh = cache.mesh(first.get(), 0);
        const auto secondMesh = cache.mesh(first.get(), 1);
        const uint64_t firstBytes = first->geometryBytes;
        require(firstMesh.buffer != VK_NULL_HANDLE && firstMesh.buffer == secondMesh.buffer &&
                firstMesh.vertexOffset == 0 && firstMesh.indexCount == 6 &&
                firstMesh.indexOffset == first->chunks[0].vertices.size() * sizeof(GroundChunkVertex) &&
                secondMesh.vertexOffset == firstMesh.indexOffset + 6 * sizeof(uint32_t) &&
                secondMesh.indexOffset == secondMesh.vertexOffset +
                    first->chunks[1].vertices.size() * sizeof(GroundChunkVertex) &&
                secondMesh.indexCount == 6 &&
                cache.mesh(first.get(), 2).buffer == VK_NULL_HANDLE &&
                cache.mesh(second.get(), 0).buffer == VK_NULL_HANDLE &&
                cache.residentBytes() == firstBytes &&
                allocator.statistics().bufferCount == baseline.bufferCount + 1,
            "Published ground chunk ranges or staging retirement are incorrect");
        const auto warmAllocations = allocator.statistics().lifetimeAllocations;
        for (int index = 0; index < 8; ++index) {
            require(cache.update(first, 0) && cache.uploadCount() == 1,
                "Warm ground chunk lookup submitted another upload");
        }
        require(allocator.statistics().lifetimeAllocations == warmAllocations,
            "Warm ground chunk lookup allocated another GPU buffer");

        // Two logical frame slots reference the first buffer. The commands
        // use its actual vertex/index ranges, without a render pass or draw.
        // Completion of one slot must not release the other slot's resources.
        for (std::size_t index = 0; index < referencingFrames.size(); ++index) {
            const VkCommandBufferAllocateInfo commandInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = deviceContext.commandPool(),
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            require(vkAllocateCommandBuffers(deviceContext.device(), &commandInfo,
                        &referencingFrames[index]) == VK_SUCCESS,
                "Ground chunk lifecycle frame command allocation failed");
            const VkCommandBufferBeginInfo beginInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            require(vkBeginCommandBuffer(referencingFrames[index], &beginInfo) == VK_SUCCESS,
                "Ground chunk lifecycle frame command begin failed");
            vkCmdBindVertexBuffers(referencingFrames[index], 0, 1,
                &firstMesh.buffer, &firstMesh.vertexOffset);
            vkCmdBindIndexBuffer(referencingFrames[index], firstMesh.buffer,
                firstMesh.indexOffset, VK_INDEX_TYPE_UINT32);
            require(vkEndCommandBuffer(referencingFrames[index]) == VK_SUCCESS,
                "Ground chunk lifecycle frame command end failed");
            const VkFenceCreateInfo fenceInfo { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            require(vkCreateFence(deviceContext.device(), &fenceInfo, nullptr,
                        &referencingFences[index]) == VK_SUCCESS,
                "Ground chunk lifecycle frame fence creation failed");
            const VkSubmitInfo submit {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &referencingFrames[index],
            };
            require(vkQueueSubmit(deviceContext.graphicsQueue(), 1, &submit,
                        referencingFences[index]) == VK_SUCCESS,
                "Ground chunk lifecycle frame submission failed");
        }
        require(!cache.update(second, (1U << 0) | (1U << 1)) && cache.uploadCount() == 2 &&
                !cache.readyFor(first.get()) && !cache.readyFor(second.get()) &&
                cache.residentBytes() == firstBytes + second->geometryBytes,
            "Ground chunk replacement dropped the referenced previous geometry");
        deviceContext.waitIdle();
        require(cache.update(second, 0) && allocator.statistics().bufferCount == baseline.bufferCount + 2,
            "Ground chunk replacement did not retain the previous frame-owned buffer");
        require(vkGetFenceStatus(deviceContext.device(), referencingFences[0]) == VK_SUCCESS,
            "Ground chunk lifecycle first frame fence did not complete");
        cache.completeFrame(0);
        VkMemoryRequirements retainedRequirements {};
        vkGetBufferMemoryRequirements(deviceContext.device(), firstMesh.buffer, &retainedRequirements);
        require(retainedRequirements.size >= firstBytes &&
                allocator.statistics().bufferCount == baseline.bufferCount + 2 &&
                cache.residentBytes() == firstBytes + second->geometryBytes,
            "Completing one frame released geometry still referenced by another frame");
        require(vkGetFenceStatus(deviceContext.device(), referencingFences[1]) == VK_SUCCESS,
            "Ground chunk lifecycle second frame fence did not complete");
        cache.completeFrame(1);
        require(allocator.statistics().bufferCount == baseline.bufferCount + 1 &&
                cache.residentBytes() == second->geometryBytes && cache.readyFor(second.get()),
            "Last ground chunk frame completion did not release retired geometry");

        auto oversized = std::make_shared<GroundChunkGeometry>();
        oversized->chunks.emplace_back().vertices.resize(
            (8ULL * 1024 * 1024) / sizeof(GroundChunkVertex) + 1);
        require(!cache.update(oversized, 0) && cache.uploadCount() == 2 &&
                cache.readyFor(second.get()) && cache.residentBytes() == second->geometryBytes,
            "Rejected oversized ground geometry replaced a valid resident entry");

        // Teardown also covers an upload whose fence has never been polled.
        const auto third = geometryAt(32);
        require(!cache.update(third, 0) && cache.uploadCount() == 3 && !cache.readyFor(third.get()),
            "Ground chunk uncollected-upload teardown fixture did not submit");
        cleanup();
        const auto final = allocator.statistics();
        require(cache.residentBytes() == 0 && cache.stagingBytes() == 0 && !cache.readyFor(third.get()) &&
                final.bufferCount == baseline.bufferCount && final.bufferBytes == baseline.bufferBytes,
            "Ground chunk destroy leaked device or staging buffers");
        cache.destroy();
        std::cout << "Ground chunk uploads publish once, retain pending-frame geometry, and release all buffers\n";
    } catch (...) {
        cleanup();
        throw;
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

void exerciseBlockingAdmissionFailure(
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
            .maxConcurrentCpuJobs = 1,
            .maxPublicationsPerFrame = 1,
            .preparedAssetBytes = 1,
            .modelResidencyBytes = 1,
        });

    sokoban::RenderAssetRequirements requirements;
    requirements.requireModel(sokoban::RenderModel { 1 });
    requirements.requireModel(sokoban::RenderModel { 2 });

    const Clock::time_point started = Clock::now();
    std::string failure;
    try {
        (void)resources.waitForAssets(requirements);
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    const auto elapsed = Clock::now() - started;
    const sokoban::VulkanModelResources::LoadingStats stats =
        resources.loadingStats();
    if (failure.find("cannot make progress") == std::string::npos ||
        elapsed >= std::chrono::seconds(10) ||
        stats.modelStages.cpuReady == 0 ||
        stats.modelStages.queued == 0 ||
        stats.activeCpuJobs != 0 ||
        stats.residencyOversizedBlocks == 0) {
        throw std::runtime_error(
            "Blocking asset admission did not report its terminal state: " +
            failure);
    }
}

void exerciseTextureReplacementRetry(
    sokoban::VulkanDeviceContext& deviceContext)
{
    const std::filesystem::path assetRoot =
        std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) / "assets";
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(assetRoot / "manifest.json");
    const sokoban::RuntimeTextureCatalog textureCatalog =
        sokoban::collectRuntimeTextureCatalog(assetRoot, manifest);
    const sokoban::RenderTexture texture =
        manifest.textureIdByName("GroundSplatMap");
    const sokoban::ImageData source = sokoban::loadRgbaImage(
        assetRoot / "custom/textures/ground_splat.png");
    sokoban::ImageData replacement {
        .width = source.width + 1,
        .height = source.height,
    };
    replacement.rgba.assign(
        static_cast<std::size_t>(replacement.width) * replacement.height * 4,
        std::byte { 0x7f });

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
        deviceContext.textureDescriptorCapacity(),
        deviceContext.maxSamplerAnisotropy());

    const auto unpublished = resources.updateTexture(texture, replacement);
    if (unpublished.updated || unpublished.descriptorsChanged) {
        throw std::runtime_error(
            "An unpublished painted texture reported a completed update");
    }

    sokoban::RenderAssetRequirements requirements;
    requirements.requireTexture(texture);
    if (!resources.waitForAssets(requirements)) {
        throw std::runtime_error(
            "Blocking texture upload did not request a descriptor refresh");
    }
    if (resources.waitForAssets(requirements)) {
        throw std::runtime_error(
            "Already resident texture unnecessarily requested a descriptor refresh");
    }
    if (vkDeviceWaitIdle(deviceContext.device()) != VK_SUCCESS) {
        throw std::runtime_error(
            "Texture replacement test could not finish its initial upload");
    }
    resources.retireCompletedUploads();

    const auto beforeView = resources.textures().at(texture.index());
    const auto beforeStats = resources.loadingStats();
    const sokoban::VulkanMemoryStatistics beforeMemory =
        deviceContext.memoryAllocator().statistics();
    if (!beforeView.valid() || beforeStats.loadedTextures != 1) {
        throw std::runtime_error(
            "Texture replacement test did not publish its initial texture");
    }

    sokoban::VulkanTextureUploader::failNextTextureUploadForTesting();
    std::string failure;
    try {
        (void)resources.updateTexture(texture, replacement);
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    const auto failedView = resources.textures().at(texture.index());
    const auto failedStats = resources.loadingStats();
    const sokoban::VulkanMemoryStatistics failedMemory =
        deviceContext.memoryAllocator().statistics();
    if (failure.find("test-injected texture upload failure") ==
            std::string::npos ||
        failedView.imageView != beforeView.imageView ||
        failedView.sampler != beforeView.sampler ||
        failedStats.textureResidencyBytes !=
            beforeStats.textureResidencyBytes ||
        failedStats.retiringTextures != beforeStats.retiringTextures ||
        failedMemory.allocationCount != beforeMemory.allocationCount ||
        failedMemory.allocationBytes != beforeMemory.allocationBytes) {
        throw std::runtime_error(
            "Failed texture replacement changed the published texture or "
            "leaked its temporary Vulkan resources");
    }

    const auto retried = resources.updateTexture(texture, replacement);
    const auto retriedView = resources.textures().at(texture.index());
    const auto retriedStats = resources.loadingStats();
    if (!retried.updated || !retried.descriptorsChanged ||
        retriedView.imageView == beforeView.imageView ||
        retriedView.sampler == beforeView.sampler ||
        retriedStats.loadedTextures != 1 ||
        retriedStats.retiringTextures != 1 ||
        retriedStats.retiringTextureBytes !=
            beforeStats.textureResidencyBytes) {
        throw std::runtime_error(
            "Texture replacement did not succeed cleanly after retry");
    }
    resources.completeFrame(0);
    if (resources.loadingStats().retiringTextures != 0) {
        throw std::runtime_error(
            "Replaced texture was not released after frame retirement");
    }
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

void exerciseRendererAssetPublicationBudget(SDL_Window* window)
{
    using namespace sokoban;
    const auto assetRoot = std::filesystem::path(SOKOBAN_VULKAN_TEST_ASSETS);
    const auto manifest = AssetManifest::loadFromFile(assetRoot / "manifest.json");
    auto font = FontAtlas::loadDefault(assetRoot);
    ScopedTestDirectory temporary("asset-publication-budget");
    VulkanRenderer renderer(window, assetRoot, temporary.path() / "cache.bin",
        manifest, font, AntiAliasingMode::None, 100,
        { .vsync = false },
        { .maxConcurrentCpuJobs = 4, .maxPublicationsPerFrame = 2,
            .preparedAssetBytes = 128ULL * 1024ULL * 1024ULL },
        false, true, true, false);
    RenderAssetRequirements requirements;
    for (const char* name : { "BricksA", "WallStone01", "WallStone02" }) {
        requirements.requireModel(manifest.modelIdByName(name));
    }
    renderer.preloadAssets(requirements);
    UiDrawData ui;
    ui.viewportSize = { 32, 32 };
    const auto draw = [&] {
        renderer.beginDebugUiFrame();
        const auto frame = renderer.prepareFrame(RenderFrameData {});
        renderer.drawFrame(frame, ui);
        if (renderer.hasFatalFailure()) {
            throw std::runtime_error(std::string(renderer.fatalFailureMessage()));
        }
    };

    // Hold all three models at the same publication boundary so the test
    // measures the per-frame budget independently of decode completion order.
    ResidencyHold hold;
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (renderer.assetLoadingStats().modelStages.cpuReady != 3 &&
        Clock::now() < deadline) {
        draw();
        if (renderer.assetLoadingStats().failedAssets != 0) {
            throw std::runtime_error("Publication budget fixture failed to decode");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (renderer.assetLoadingStats().modelStages.cpuReady != 3) {
        throw std::runtime_error("Publication budget fixture did not become CPU-ready");
    }
    const uint64_t before = renderer.renderStats().assetPublications;
    hold.release();
    draw();
    const auto after = renderer.assetLoadingStats();
    if (renderer.renderStats().assetPublications - before != 2 ||
        after.modelStages.cpuReady != 1 || after.modelStages.uploading != 2) {
        throw std::runtime_error(
            "Renderer did not publish exactly the configured two assets in one frame");
    }
    renderer.waitIdle();
    std::cout << "Renderer honored the configured two-asset publication budget\n";
}

int exerciseThumbnailLoadingBudget(SDL_Window* window)
{
#if SOKOBAN_ENABLE_DEBUG_UI
    using namespace sokoban;
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto assetRoot = std::filesystem::path(SOKOBAN_VULKAN_TEST_ASSETS);
    constexpr TileType first = TileType::Ground;
    constexpr TileType second = TileType::Wall;
    constexpr TileType missing = TileType::Air;
    require(std::filesystem::is_regular_file(
                assetRoot / tileThumbnails::assetPathFor(first)) &&
            std::filesystem::is_regular_file(
                assetRoot / tileThumbnails::assetPathFor(second)),
        "Thumbnail budget fixture needs baked Ground and Wall thumbnails");
    require(!std::filesystem::exists(
                assetRoot / tileThumbnails::assetPathFor(missing)),
        "Thumbnail cached-miss fixture unexpectedly has an Air thumbnail");
    const auto manifest = AssetManifest::loadFromFile(assetRoot / "manifest.json");
    auto font = FontAtlas::loadDefault(assetRoot);
    ScopedTestDirectory temporary("thumbnail-loading-budget");
    VulkanRenderer renderer(window, assetRoot, temporary.path() / "cache.bin",
        manifest, font, AntiAliasingMode::None, 100,
        { .vsync = false }, {}, false, true, true, false);
    UiDrawData ui;
    ui.viewportSize = { 32, 32 };
    const auto draw = [&] {
        renderer.beginDebugUiFrame();
        const auto frame = renderer.prepareFrame(RenderFrameData {});
        renderer.drawFrame(frame, ui);
        require(!renderer.hasFatalFailure(), "Thumbnail budget frame failed to render");
    };

    require(renderer.tileThumbnail(TileType::Count) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(TileType::Count) == VK_NULL_HANDLE,
        "An invalid tile type must not acquire a thumbnail or enter the service queue");
    require(renderer.tileThumbnail(first) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(first) == VK_NULL_HANDLE,
        "Initial thumbnail requests must only queue deduplicated work");
    draw();
    const VkDescriptorSet firstHandle = renderer.tileThumbnail(first);
    require(firstHandle != VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) == VK_NULL_HANDLE,
        "The first service frame must load exactly one queued thumbnail");
    draw();
    require(renderer.tileThumbnail(first) == firstHandle &&
            renderer.tileThumbnail(second) != VK_NULL_HANDLE,
        "Duplicate requests must not delay the second thumbnail or replace a cached handle");

    renderer.invalidateTileThumbnails();
    require(renderer.tileThumbnail(missing) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(missing) == VK_NULL_HANDLE,
        "A missing thumbnail must queue one request and use its fallback");
    draw();
    // A cached miss must not consume another service slot before these two
    // real files. The same requests also prove invalidation dropped both
    // previously loaded handles and lets them load progressively again.
    require(renderer.tileThumbnail(missing) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(first) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) == VK_NULL_HANDLE,
        "Invalidation must clear loaded handles while caching a serviced miss");
    draw();
    require(renderer.tileThumbnail(first) != VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) == VK_NULL_HANDLE,
        "A cached missing thumbnail must not delay the first reloaded thumbnail");
    draw();
    require(renderer.tileThumbnail(first) != VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) != VK_NULL_HANDLE &&
            renderer.tileThumbnail(missing) == VK_NULL_HANDLE,
        "Invalidated thumbnails must reload one per frame and preserve cached misses");

    std::vector<TileType> tiles;
    for (const auto& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) continue;
        require(std::filesystem::is_regular_file(
                assetRoot / tileThumbnails::assetPathFor(definition.type)),
            "Complete thumbnail fixture is missing a baked tile image");
        tiles.push_back(definition.type);
    }
    require(tiles.size() > 64,
        "Complete thumbnail fixture must exceed the former 64-image descriptor pool");
    const auto loadCompleteCatalog = [&] {
        renderer.invalidateTileThumbnails();
        std::vector<VkDescriptorSet> handles(tiles.size(), VK_NULL_HANDLE);
        for (const TileType tile : tiles) {
            require(renderer.tileThumbnail(tile) == VK_NULL_HANDLE &&
                    renderer.tileThumbnail(tile) == VK_NULL_HANDLE,
                "Complete catalog requests must only queue deduplicated work");
        }
        for (std::size_t frame = 0; frame < tiles.size(); ++frame) {
            draw();
            for (std::size_t index = 0; index < tiles.size(); ++index) {
                const auto handle = renderer.tileThumbnail(tiles[index]);
                if (index > frame) {
                    require(handle == VK_NULL_HANDLE,
                        "Complete catalog uploaded more than one thumbnail in one frame");
                } else if (handles[index] != VK_NULL_HANDLE) {
                    require(handle == handles[index],
                        "Complete catalog replaced a previously loaded thumbnail handle");
                } else {
                    require(index == frame && handle != VK_NULL_HANDLE,
                        "Complete catalog did not load its next thumbnail in one frame");
                    require(std::find(handles.begin(), handles.end(), handle) == handles.end(),
                        "Different tile thumbnails share the same descriptor handle");
                    handles[index] = handle;
                }
            }
        }
        return handles;
    };
    (void)loadCompleteCatalog();
    const auto catalogHandles = loadCompleteCatalog();

    // Replacing a viewport briefly needs both its new descriptor and those
    // retained by in-flight frames, while every palette image remains live.
    for (const int scale : { 75, 100, 50, 100 }) {
        const auto before = renderer.renderStats().renderResourceReconfigurations;
        renderer.setRenderScalePercent(scale);
        draw();
        // Reconfiguration occurs after recording, so the next recorded frame
        // publishes its counter in renderStats().
        draw();
        require(renderer.renderStats().renderResourceReconfigurations >= before + 1 &&
                renderer.gameViewportTexture() != 0,
            "Resident thumbnails prevented viewport resource reconfiguration");
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            require(renderer.tileThumbnail(tiles[index]) == catalogHandles[index],
                "Viewport resource reconfiguration changed a resident thumbnail handle");
        }
    }

#ifdef SOKOBAN_ENABLE_TEST_HOOKS
    // Leave both frame slots submitted, then fail acquisition repeatedly
    // without advancing the current slot. Retired viewport generations must
    // be reclaimed before they exhaust the reserved ImGui descriptors.
    for (int frame = 0; frame < 3; ++frame) draw();
    const auto beforeAcquisitionFailures = renderer.renderStats();
    auto earlyPeakImages = beforeAcquisitionFailures.gpuImageAllocationCount;
    auto earlyPeakAllocations = beforeAcquisitionFailures.gpuMemoryAllocationCount;
    for (int failure = 0; failure < 20; ++failure) {
        VulkanRenderer::failNextSwapchainAcquisitionForTesting();
        draw();
        require(renderer.gameViewportTexture() != 0,
            "Repeated out-of-date acquisitions lost the viewport descriptor");
        for (std::size_t index = 0; index < tiles.size(); ++index) {
            require(renderer.tileThumbnail(tiles[index]) == catalogHandles[index],
                "Viewport descriptor reserve pressure changed a resident thumbnail handle");
        }
        if (failure < 4) {
            const auto current = renderer.renderStats();
            earlyPeakImages = std::max(earlyPeakImages, current.gpuImageAllocationCount);
            earlyPeakAllocations = std::max(earlyPeakAllocations, current.gpuMemoryAllocationCount);
        }
    }
    const auto afterAcquisitionFailures = renderer.renderStats();
    require(earlyPeakImages > beforeAcquisitionFailures.gpuImageAllocationCount,
        "Out-of-date acquisition fixture did not retain any viewport generations");
    require(afterAcquisitionFailures.gpuImageAllocationCount <= earlyPeakImages &&
            afterAcquisitionFailures.gpuMemoryAllocationCount <= earlyPeakAllocations,
        "Repeated out-of-date acquisitions accumulated unbounded viewport allocations");
    for (int frame = 0; frame < 3; ++frame) draw();
    require(renderer.renderStats().swapchainRecreations >=
            beforeAcquisitionFailures.swapchainRecreations + 20,
        "Out-of-date acquisition fixture did not perform all requested viewport replacements");

    renderer.invalidateTileThumbnails();
    // Settle both frame slots and retired viewport resources before measuring
    // the temporary image and staging allocation of a failed registration.
    for (int frame = 0; frame < 3; ++frame) draw();
    renderer.waitIdle();
    const auto beforeFailure = renderer.renderStats();
    require(renderer.tileThumbnail(first) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) == VK_NULL_HANDLE,
        "Transient failure fixture must start with two queued thumbnails");
    VulkanThumbnailPass::failNextTextureRegistrationForTesting();
    draw();
    const auto afterFailure = renderer.renderStats();
    require(renderer.tileThumbnail(first) == VK_NULL_HANDLE &&
            renderer.tileThumbnail(second) == VK_NULL_HANDLE,
        "A failed registration must keep its fallback and consume only one service attempt");
    require(afterFailure.gpuImageAllocationCount == beforeFailure.gpuImageAllocationCount &&
            afterFailure.gpuMemoryAllocationCount == beforeFailure.gpuMemoryAllocationCount,
        "A failed thumbnail registration leaked its temporary Vulkan allocations");
    draw();
    const auto peerHandle = renderer.tileThumbnail(second);
    require(peerHandle != VK_NULL_HANDLE && renderer.tileThumbnail(first) == VK_NULL_HANDLE,
        "A thumbnail waiting to retry must not block a different queued thumbnail");

    VkDescriptorSet retriedHandle = VK_NULL_HANDLE;
    const auto retryDeadline = Clock::now() + std::chrono::seconds(3);
    while (retriedHandle == VK_NULL_HANDLE && Clock::now() < retryDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        draw();
        require(renderer.tileThumbnail(second) == peerHandle,
            "Retrying a failed registration replaced a resident peer thumbnail");
        retriedHandle = renderer.tileThumbnail(first);
    }
    require(retriedHandle != VK_NULL_HANDLE && retriedHandle != peerHandle,
        "A transient thumbnail registration failure did not recover without invalidation");
#endif
    renderer.waitIdle();
    std::cout << "Thumbnail loading deferred and deduplicated requests, loaded all "
              << tiles.size() << " baked tiles one per frame twice, preserved handles across "
                 "viewport reconfiguration and reserve pressure, and retained cached misses "
                 "and transient retries\n";
    return 0;
#else
    (void)window;
    std::cout << "Thumbnail budget check requires developer tools\n";
    return 77;
#endif
}

void exerciseGroundRimDrawBudgetFallback(SDL_Window* window)
{
    using namespace sokoban;
    const auto assetRoot = std::filesystem::path(SOKOBAN_VULKAN_TEST_ASSETS);
    const auto manifest = AssetManifest::loadFromFile(assetRoot / "manifest.json");
    auto font = FontAtlas::loadDefault(assetRoot);
    const LegacyGroundGpuFixture fixture(assetRoot, manifest);
    ScopedTestDirectory temporary("ground-rim-budget");
    VulkanRenderer renderer(window, fixture.root(), temporary.path() / "cache.bin",
        fixture.manifest(), font, AntiAliasingMode::None, 100,
        { .vsync = false }, {}, false, true, true, false);
    renderer.setFrustumCullingEnabled(false);
    const auto ground = fixture.manifest().modelIdByName("GroundRock01");
    RenderAssetRequirements requirements;
    requirements.requireModel(ground);
    renderer.waitForAssets(requirements);

    constexpr uint32_t tileCount = 5000;
    const auto scene = [&](bool requestedRim) {
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = 300;
        frame.levelHeight = 200;
        // Keep one inspectable tile in view. The other legal, isolated cells
        // still participate in preparation with frustum culling disabled.
        frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, 4, 4, 1 };
        frame.lighting.shadows.enabled = false;
        frame.requestedGroundRimWidth = requestedRim ? 0.12f : 0.0f;
        frame.requestedGroundRimDepth = requestedRim ? 0.10f : 0.0f;
        for (uint32_t i = 0; i < tileCount; ++i) {
            const int x = i == 0 ? 1 : 100 + static_cast<int>(i % 100) * 2;
            const int y = i == 0 ? 1 : 100 + static_cast<int>(i / 100) * 2;
            RenderFrameData::Tile tile {
                .cell = { x, y, 0 },
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .color = { 1, 1, 1, 1 }, .height = 1,
                .showGrid = false, .model = ground,
                .effect = RenderSurfaceEffect::GroundSplat,
                .groundTop = true,
            };
            tile.groundGeometryEligible = true;
            tile.groundSideMask = groundAllSides;
            tile.groundRimSides = groundAllSides;
            tile.groundRimWidth = frame.requestedGroundRimWidth;
            tile.groundRimDepth = frame.requestedGroundRimDepth;
            frame.tiles.push_back(tile);
        }
        return frame;
    };
    UiDrawData ui;
    ui.viewportSize = { 32, 32 };
    renderer.beginDebugUiFrame();
    const auto fallback = renderer.prepareFrame(scene(true));
    renderer.drawFrame(fallback, ui);
    if (renderer.hasFatalFailure()) {
        throw std::runtime_error(std::string(renderer.fatalFailureMessage()));
    }
    const auto fallbackStats = renderer.renderStats();
    if (!fallbackStats.groundRimBudgetFallback ||
        fallbackStats.resolvedGroundRimTiles != 0 ||
        fallbackStats.unavailableModels != 0 ||
        fallbackStats.groundTrianglesBeforeProcessing == 0 ||
        fallbackStats.groundTrianglesAfterProcessing !=
            fallbackStats.groundTrianglesBeforeProcessing ||
        renderer.assetLoadingStats().droppedDrawInstances != 0) {
        throw std::runtime_error(
            "Ground rim budget fallback did not preserve loaded flat ground without dropped draws");
    }
    const auto pixel = renderer.projectToPixels(fallback, { 1.8f, 1.8f, 1.0f });
    const auto picked = pixel ? renderer.pickIsoGroundPoint(fallback, *pixel) : std::nullopt;
    if (!picked || std::abs(picked->z - 1.0f) > 0.0001f) {
        throw std::runtime_error("Ground rim budget fallback left a lowered paintable cap");
    }
    const auto fallbackImage = renderer.captureRenderedFrame();
    renderer.beginDebugUiFrame();
    const auto flat = renderer.prepareFrame(scene(false));
    renderer.drawFrame(flat, ui);
    const auto flatImage = renderer.captureRenderedFrame();
    if (renderer.hasFatalFailure() || renderer.renderStats().groundRimBudgetFallback ||
        renderer.renderStats().resolvedGroundRimTiles != 0 ||
        renderer.assetLoadingStats().droppedDrawInstances != 0 ||
        fallbackImage.width != flatImage.width || fallbackImage.height != flatImage.height ||
        fallbackImage.rgba != flatImage.rgba) {
        throw std::runtime_error(
            "Ground rim budget fallback caps and body differ from an explicitly flat frame");
    }
    renderer.waitIdle();
    std::cout << "Legacy ground rim budget fallback preserved " << tileCount
              << " loaded ground bodies and flat caps without dropped draw instances\n";
}

void exerciseNativeGroundBypassesLegacyProcessing(SDL_Window* window)
{
    using namespace sokoban;
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto assetRoot = std::filesystem::path(SOKOBAN_VULKAN_TEST_ASSETS);
    const auto manifest = AssetManifest::loadFromFile(assetRoot / "manifest.json");
    auto font = FontAtlas::loadDefault(assetRoot);
    ScopedTestDirectory temporary("native-ground-gpu");
    VulkanRenderer renderer(window, assetRoot, temporary.path() / "cache.bin",
        manifest, font, AntiAliasingMode::None, 100,
        { .vsync = false }, {}, false, true, true, false);
    renderer.setFrustumCullingEnabled(false);

    constexpr Vec3 cameraPosition { 6, 6, 9 };
    const auto makeFrame = [&](bool legacySettings) {
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = frame.levelHeight = 3;
        frame.levelDepth = 2;
        frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, -1, 3, 3, 3 };
        frame.cameraOverride = RenderFrameData::CameraOverride {
            .position = cameraPosition,
            .forward = Vec3 { 1.5f, 1.5f, 1.0f } - cameraPosition,
        };
        frame.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 1 };
        frame.lighting.shadows.enabled = false;
        frame.groundSplat = groundSplatTexturesForScreen(
            [&](std::string_view name) { return manifest.findTextureIdByName(name); },
            std::nullopt);
        require(frame.groundSplat.valid(), "Native ground fixture lacks its painted surface textures");
        frame.requestedGroundRimWidth = legacySettings ? .12f : 0.0f;
        frame.requestedGroundRimDepth = legacySettings ? .10f : 0.0f;
        frame.groundChunksRequested = legacySettings;
        frame.groundChunkMeshoptimizer = legacySettings;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                auto tile = tileVisual((x + y) % 2 == 0 ? TileType::Ground : TileType::GroundRock02,
                    { x, y, 0 }, manifest, {});
                tile.showGrid = false;
                frame.tiles.push_back(tile);
            }
        }
        auto raised = tileVisual(TileType::GroundRock03, { 1, 1, 1 }, manifest, {});
        raised.showGrid = false;
        frame.tiles.push_back(raised);
        // Use the gameplay/editor selector: the lower connected cohort uses
        // square floating-bottom bodies, while the supported raised tile keeps
        // its regular chamfered body and literal authored cap.
        processGroundTileGeometry(frame.tiles, manifest);
        require(std::ranges::all_of(frame.tiles, [&](const auto& tile) {
            return tile.groundModule && !tile.groundGeometryEligible &&
                tile.groundRimWidth == 0 && tile.groundRimDepth == 0 &&
                authoredGroundTileVariant(manifest.model(tile.model)).has_value() &&
                !isProcessableGroundRockModel(manifest.model(tile.model));
        }), "Native ground fixture entered the legacy mesh processor");
        require(std::ranges::count_if(frame.tiles, [](const auto& tile) {
            return tile.groundModuleSquare && tile.groundModuleBottom;
        }) == 9 && !frame.tiles.back().groundModuleSquare && !frame.tiles.back().groundModuleBottom,
            "Native fixture did not select both stacked square bottoms and a supported chamfered top");
        return frame;
    };
    const auto initial = makeFrame(true);
    const auto requirements = renderAssetRequirementsForFrame(initial);
    require(requirements.modelCount() == 4,
        "Mixed native ground fixture did not select its four distinct authored bodies");
    renderer.waitForAssets(requirements);
    uint32_t expectedCapPatches = 0;
    uint32_t expectedVisibleCapPatches = 0;
    for (const auto& tile : initial.tiles) {
        const auto surface = groundTileTopSurface(tile);
        expectedCapPatches += static_cast<uint32_t>(surface.count);
        for (const auto& patch : surface.faces()) {
            Vec3 center {};
            for (const auto vertex : patch.vertices) center += vertex * .25f;
            if (dot(patch.normal, cameraPosition - center) > 0) ++expectedVisibleCapPatches;
        }
    }
    require(expectedCapPatches > initial.tiles.size() && expectedVisibleCapPatches > initial.tiles.size(),
        "Native fixture lost the raised tile's authored chamfer facets");
    // This camera sees the east, south and top logical faces of every tile.
    // appendIsoFace also culls authored cap patches that face away from it.
    const auto expectedFaces = initial.tiles.size() * 3 + expectedVisibleCapPatches;

    UiDrawData ui;
    ui.viewportSize = { 32, 32 };
    const auto capture = [&](bool legacySettings) {
        renderer.beginDebugUiFrame();
        const auto prepared = renderer.prepareFrame(makeFrame(legacySettings));
        renderer.drawFrame(prepared, ui);
        require(!renderer.hasFatalFailure(), "Native ground fixture failed to render");
        const auto stats = renderer.renderStats();
        if (!(stats.unavailableModels == 0 && stats.preparedModels == initial.tiles.size() &&
                stats.preparedIsoFaces == expectedFaces &&
                stats.drawCalls > 0 && stats.triangles > expectedCapPatches)) {
            throw std::runtime_error("Native ground did not draw its resident authored bodies and cap patches: models=" +
                std::to_string(stats.preparedModels) + ", faces=" + std::to_string(stats.preparedIsoFaces) +
                ", expectedFaces=" + std::to_string(expectedFaces) +
                ", authoredPatches=" + std::to_string(expectedCapPatches) +
                ", visiblePatches=" + std::to_string(expectedVisibleCapPatches) +
                ", draws=" + std::to_string(stats.drawCalls) + ", triangles=" + std::to_string(stats.triangles) +
                ", unavailable=" + std::to_string(stats.unavailableModels));
        }
        require(stats.groundTrianglesBeforeProcessing == 0 && stats.groundTrianglesAfterProcessing == 0 &&
                stats.groundShadowTrianglesRemoved == 0 && stats.resolvedGroundRimTiles == 0 &&
                !stats.groundRimBudgetFallback && stats.generatedGroundRimSurfaces == 0 &&
                stats.groundRimSurfaceCacheBytes == 0 && stats.groundRimArtifactImports == 0 &&
                stats.groundChunkDraws == 0 && stats.groundChunkTiles == 0 &&
                stats.groundChunkUploads == 0 && stats.groundChunkGeometryBytes == 0 &&
                stats.groundChunkOriginalBytes == 0 && stats.groundChunkInputVertices == 0 &&
                stats.groundChunkOutputVertices == 0 &&
                stats.groundChunkResidentBytes == 0 && stats.groundChunkStagingBytes == 0,
            "Native ground unexpectedly generated legacy side variants, rims or chunks");
        // The enabled empty candidate set has one cache bookkeeping generation;
        // it must never become geometry or a GPU upload, including after disabling.
        require(stats.groundChunkCacheRebuilds == 1,
            "Native ground changed the empty legacy chunk cache generation");
        require(stats.groundChunksRequested == legacySettings && stats.groundChunkMeshoptimizer == legacySettings,
            "Native ground fixture did not exercise both legacy toggle settings");
        const auto assets = renderer.assetLoadingStats();
        require(assets.loadedModels == requirements.modelCount() && assets.failedAssets == 0 &&
                assets.modelResidencyBytes > 0 && assets.textureResidencyBytes > 0 &&
                assets.residencyBudgetBlocks == 0 && assets.droppedDrawInstances == 0 &&
                assets.droppedSkinningInstances == 0,
            "Native ground rendering lost resident resources or exceeded its draw budget");
        const auto pixel = renderer.projectToPixels(prepared, { 1.5f, 1.5f, 2.0f });
        const auto picked = pixel ? renderer.pickIsoGroundPoint(prepared, *pixel) : std::nullopt;
        require(picked && std::abs(picked->z - 2.0f) < .0001f,
            "Raised native cap did not retain its authored paintable surface height");
        return renderer.captureRenderedFrame();
    };
    const auto enabled = capture(true);
    const auto disabled = capture(false);
    require(enabled.width == disabled.width && enabled.height == disabled.height && enabled.rgba == disabled.rgba,
        "Legacy rim/chunk settings changed the current native ground image");
    renderer.waitIdle();
    require(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a native ground rendering error");
    std::cout << "Native ground drew four mixed authored bodies and " << expectedCapPatches
              << " cap patches with identical legacy settings on/off and no processed ground resources\n";
}

void exerciseAnimatedSkyClouds()
{
    using namespace sokoban;
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto assetRoot = std::filesystem::path(SOKOBAN_VULKAN_TEST_ASSETS);
    const auto manifest = AssetManifest::loadFromFile(assetRoot / "manifest.json");
    auto font = FontAtlas::loadDefault(assetRoot);
    ScopedTestDirectory temporary("animated-sky-clouds");
    const SdlWindow window(1024, 1024);
    VulkanRenderer renderer(window.get(), assetRoot, temporary.path() / "cache.bin",
        manifest, font, AntiAliasingMode::None, 100,
        { .vsync = false }, {}, false, true, true, false);
    UiDrawData ui;
    ui.viewportSize = { 1024, 1024 };
    std::optional<Vec2> solidPixel;
    const auto capture = [&](bool enabled, float seconds,
                             RenderViewMode mode = RenderViewMode::Isometric3D,
                             bool solid = false, float pitch = 60.0f,
                             float yaw = 0.0f,
                             std::optional<RenderFrameData::CameraOverride> camera = std::nullopt) {
        RenderFrameData frame;
        frame.viewMode = mode;
        frame.levelWidth = 8;
        frame.levelHeight = 8;
        frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, 8, 8, 1 };
        frame.cameraPitchDegrees = pitch;
        frame.cameraYawDegrees = yaw;
        frame.cameraOverride = camera;
        frame.skyCloudsEnabled = enabled;
        frame.effectAnimationTimeSeconds = seconds;
        frame.lighting.shadows.enabled = false;
        frame.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 1.0f };
        if (solid) {
            // Cover the view so bloom from bright background clouds cannot
            // change a surface sample despite correct opaque depth/coverage.
            frame.tiles.push_back({
                .cell = { -32, -32, 0 }, .position = { -32, -32 }, .size = { 64, 64 },
                .color = { 0.12f, 0.7f, 0.18f, 1.0f }, .height = 1.0f,
                .showGrid = false, .affectsCameraFit = false,
            });
        }
        renderer.beginDebugUiFrame();
        const auto prepared = renderer.prepareFrame(std::move(frame));
        if (solid) {
            solidPixel = renderer.projectToPixels(prepared, { 3, 3, 1 });
        }
        renderer.drawFrame(prepared, ui);
        require(!renderer.hasFatalFailure(), "Sky cloud frame failed to render");
        auto image = renderer.captureRenderedFrame();
        require(image.width != 0 && image.height != 0 &&
                image.rgba.size() == std::size_t(image.width) * image.height * 4,
            "Sky cloud capture was empty or incomplete");
        return image;
    };
    const auto difference = [&](const ImageData& left, const ImageData& right) {
        require(left.width == right.width && left.height == right.height,
            "Sky cloud capture dimensions changed between identical views");
        uint64_t total = 0;
        for (std::size_t pixel = 0; pixel < left.rgba.size(); pixel += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                total += static_cast<uint64_t>(std::abs(
                    std::to_integer<int>(left.rgba[pixel + channel]) -
                    std::to_integer<int>(right.rgba[pixel + channel])));
            }
        }
        return double(total) / (double(left.width) * double(left.height) * 3.0);
    };

    const auto initial = capture(true, 0.0f);
    const auto repeated = capture(true, 0.0f);
    const auto nearby = capture(true, 1.0f);
    const auto later = capture(true, 45.0f);
    require(initial.rgba == repeated.rgba,
        "Sky clouds changed when presentation time and camera were unchanged");
    int minimumBrightness = 3 * 255;
    int maximumBrightness = 0;
    for (std::size_t pixel = 0; pixel < initial.rgba.size(); pixel += 4) {
        const int brightness = std::to_integer<int>(initial.rgba[pixel]) +
            std::to_integer<int>(initial.rgba[pixel + 1]) +
            std::to_integer<int>(initial.rgba[pixel + 2]);
        minimumBrightness = std::min(minimumBrightness, brightness);
        maximumBrightness = std::max(maximumBrightness, brightness);
    }
    require(maximumBrightness - minimumBrightness > 18,
        "Sky cloud background has no visible spatial variation");
    const double nearbyChange = difference(initial, nearby);
    const double laterChange = difference(initial, later);
    require(laterChange > 0.05 && laterChange > nearbyChange && nearbyChange < 6.0,
        "Sky cloud presentation clock did not produce a gentle visible drift");

    const auto disabled = capture(false, 45.0f);
    const auto topDown = capture(true, 45.0f, RenderViewMode::TopDown2D);
    require(disabled.rgba == topDown.rgba,
        "Sky clouds changed the existing top-down background");
    for (std::size_t pixel = 4; pixel < disabled.rgba.size(); ++pixel) {
        require(disabled.rgba[pixel] == disabled.rgba[pixel % 4],
            "Disabling sky clouds did not preserve the uniform clear background");
    }

    const auto vertical = capture(true, 0.0f, RenderViewMode::Isometric3D, false, 0.0f);
    const auto rotated = capture(true, 0.0f, RenderViewMode::Isometric3D, false, 60.0f, 75.0f);
    const auto horizontal = capture(true, 0.0f, RenderViewMode::Isometric3D, false,
        60.0f, 0.0f, RenderFrameData::CameraOverride {
            .position = { 4, -4, 8 }, .forward = { 0, 1, -0.001f },
        });
    const auto upward = capture(true, 0.0f, RenderViewMode::Isometric3D, false,
        60.0f, 0.0f, RenderFrameData::CameraOverride {
            .position = { 4, -4, 8 }, .forward = { 0, 1, 1 },
        });
    for (const auto* image : { &vertical, &rotated, &horizontal, &upward }) {
        require(difference(*image, disabled) > 10.0,
            "Sky clouds disappeared for an authored or detached camera angle");
        for (std::size_t pixel = 0; pixel < image->rgba.size(); pixel += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const int value = std::to_integer<int>(image->rgba[pixel + channel]);
                require(value > 0 && value < 255,
                    "Sky cloud camera reconstruction produced a black or saturated pixel");
            }
        }
    }
    require(difference(initial, rotated) > 0.05,
        "Rotating the camera did not change its world-space cloud view");

    const auto solidDisabled = capture(false, 0.0f, RenderViewMode::Isometric3D, true);
    const auto solidEnabled = capture(true, 0.0f, RenderViewMode::Isometric3D, true);
    require(solidPixel && std::isfinite(solidPixel->x) && std::isfinite(solidPixel->y) &&
            solidPixel->x >= 0 && solidPixel->y >= 0 &&
            solidPixel->x < static_cast<float>(solidEnabled.width) &&
            solidPixel->y < static_cast<float>(solidEnabled.height),
        "Sky cloud fixture could not project its opaque tile");
    const auto pixelOffset = (std::size_t(solidPixel->y) * solidEnabled.width +
        std::size_t(solidPixel->x)) * 4;
    bool solidVisible = false;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        require(solidEnabled.rgba[pixelOffset + channel] ==
                solidDisabled.rgba[pixelOffset + channel],
            "Sky clouds overwrote or changed an opaque surface");
        solidVisible = solidVisible || solidDisabled.rgba[pixelOffset + channel] !=
            disabled.rgba[pixelOffset + channel];
    }
    require(solidVisible, "Sky cloud fixture did not draw its opaque tile");

    // A 32-pixel fixture rarely samples a moving noise-cell boundary. At
    // this resolution, several boundaries cross pixel centers in one
    // millisecond. Continuous cloud density and analytic lighting can change
    // by at most a couple of display steps over that interval; mismatched
    // hashes for a shared lattice corner produce abrupt, much larger pops.
    int maximumTemporalChange = 0;
    for (float seconds : { 0.0f, 45.0f, 120.0f }) {
        const auto before = capture(true, seconds, RenderViewMode::Isometric3D,
            false, 60.0f, 0.0f);
        const auto after = capture(true, seconds + 0.001f, RenderViewMode::Isometric3D,
            false, 60.0f, 0.0f);
        require(before.width >= 1024 && before.height >= 1024,
            "Sky cloud continuity fixture did not render at its requested resolution");
        require(before.width == after.width && before.height == after.height,
            "Sky cloud continuity capture dimensions changed between identical views");
        for (std::size_t pixel = 0; pixel < before.rgba.size(); pixel += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                maximumTemporalChange = std::max(maximumTemporalChange, std::abs(
                    std::to_integer<int>(before.rgba[pixel + channel]) -
                    std::to_integer<int>(after.rgba[pixel + channel])));
            }
        }
    }
    if (maximumTemporalChange > 2) {
        throw std::runtime_error(
            "Sky cloud animation has a discontinuity at a moving noise-cell boundary: "
            "maximum millisecond RGB change=" + std::to_string(maximumTemporalChange));
    }
    renderer.waitIdle();
    std::cout << "Sky clouds vary across the view and drift deterministically: one-second change="
              << nearbyChange << ", 45-second change=" << laterChange
              << "; maximum millisecond change=" << maximumTemporalChange
              << "; disabled, top-down and opaque geometry remain intact\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const SdlVideo video;
        const SdlWindow window;
        if (argc == 2 && std::string_view(argv[1]) == "--sky-clouds") {
            exerciseAnimatedSkyClouds();
            return 0;
        }
        if (argc == 2 &&
            std::string_view(argv[1]) == "--asset-publication-budget") {
            exerciseRendererAssetPublicationBudget(window.get());
            return 0;
        }
        if (argc == 2 &&
            std::string_view(argv[1]) == "--thumbnail-loading-budget") {
            return exerciseThumbnailLoadingBudget(window.get());
        }
        if (argc == 2 &&
            std::string_view(argv[1]) == "--benchmark-startup") {
            const StartupMetrics metrics = benchmarkStartup(window.get());
            std::cout
                << "startup_phases package_validation_us="
                << metrics.packageValidationMicroseconds
                << " manifest_inspection_us="
                << metrics.manifestInspectionMicroseconds
                << " requirement_discovery_us="
                << metrics.requirementDiscoveryMicroseconds
                << " gltf_dependency_discovery_us="
                << metrics.gltfDependencyDiscoveryMicroseconds
                << " device_setup_us=" << metrics.deviceSetupMicroseconds
                << " resource_setup_us=" << metrics.resourceSetupMicroseconds
                << " decode_us=" << metrics.decodeMicroseconds
                << " upload_us=" << metrics.uploadMicroseconds
                << " ready_us=" << metrics.readyMicroseconds
                << " requested_models=" << metrics.requestedModels
                << " requested_textures=" << metrics.requestedTextures
                << " requested_animations=" << metrics.requestedAnimations
                << '\n';
            return 0;
        }
        sokoban::VulkanDeviceContext deviceContext(window.get());
        if (argc == 2 &&
            std::string_view(argv[1]) == "--geometry-arena") {
            exerciseGeometryArena(deviceContext);
            return 0;
        }
        // Cover the live device-query policy as well as the pure selector:
        // a sampled D32 attachment must not be downgraded to D16, whose
        // precision loses floor markers with the detached camera.
        VkFormatProperties depthProperties {};
        vkGetPhysicalDeviceFormatProperties(
            deviceContext.physicalDevice(),
            VK_FORMAT_D32_SFLOAT,
            &depthProperties);
        VkImageFormatProperties depthImageProperties {};
        const VkResult depthImageResult = vkGetPhysicalDeviceImageFormatProperties(
            deviceContext.physicalDevice(),
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            0,
            &depthImageProperties);
        constexpr VkFormatFeatureFlags depthFeatures =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((depthProperties.optimalTilingFeatures & depthFeatures) ==
                depthFeatures &&
            depthImageResult == VK_SUCCESS &&
            (depthImageProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) != 0 &&
            deviceContext.sceneDepthFormat() != VK_FORMAT_D32_SFLOAT) {
            throw std::runtime_error(
                "Scene depth lost detached-camera precision despite D32 support");
        }
        exerciseMemoryAllocator(deviceContext);
        exerciseGroundChunkCacheLifecycle(deviceContext);
        exerciseSkinnedPublicationRetry(deviceContext);
        exerciseBlockingAdmissionFailure(deviceContext);
        exerciseTextureReplacementRetry(deviceContext);
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
        exerciseRendererAssetPublicationBudget(window.get());
        exerciseGroundRimDrawBudgetFallback(window.get());
        exerciseNativeGroundBypassesLegacyProcessing(window.get());
        exerciseAnimatedSkyClouds();
        std::cout << "Vulkan hidden-surface smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Vulkan smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
