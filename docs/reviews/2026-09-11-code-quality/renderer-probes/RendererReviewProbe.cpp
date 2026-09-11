#include "engine/AssetManifest.hpp"
#include "engine/render/RuntimeTextureCatalog.hpp"
#include "engine/render/VulkanDeviceContext.hpp"
#include "engine/render/VulkanModelResources.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv)
{
    using namespace sokoban;
    if (argc != 3) return 2;
    VulkanDeviceFeatureSupport support {};
    support.maxPerStageDescriptorSampledImages = 160;
    support.maxDescriptorSetSampledImages = 128;
    const auto heap = chooseVulkanTextureHeapCapacity(support, 70, 8, 16, 256, 8);
    std::cout << "heap=" << heap.capacity << " supported=" << heap.supported
              << " aggregateSampledImages=" << heap.capacity + 8
              << " maxDescriptorSetSampledImages=" << support.maxDescriptorSetSampledImages
              << std::endl;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 3;
    SDL_Window* window = SDL_CreateWindow("Renderer review probe", 32, 32,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) return 4;
    try {
        const std::filesystem::path root = argv[1];
        const auto manifest = AssetManifest::loadFromFile(root / "manifest.json");
        const auto catalog = collectRuntimeTextureCatalog(root, manifest);
        VulkanDeviceContext device(window, catalog.textures().size());
        VulkanModelResources resources;
        const bool repaint = std::string_view(argv[2]) == "repaint";
        const AssetLoadingBudget budget = repaint ? AssetLoadingBudget {} :
            AssetLoadingBudget { .maxConcurrentCpuJobs = 1, .maxPublicationsPerFrame = 1,
                .preparedAssetBytes = 1, .modelResidencyBytes = 1 };
        resources.create(device.physicalDevice(), device.memoryAllocator(), device.device(),
            device.commandPool(), device.graphicsQueue(), root, manifest, catalog,
            device.textureDescriptorCapacity(), device.maxSamplerAnisotropy(),
            budget);
        RenderAssetRequirements requirements;
        if (repaint) {
            requirements.requireTexture(RenderTexture { 1 });
            (void)resources.waitForAssets(requirements);
            const auto before = resources.textures()[0];
            ImageData image;
            image.width = 4097;
            image.height = 4097;
            image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4, std::byte { 255 });
            try {
                (void)resources.updateTexture(RenderTexture { 1 }, image);
                std::cout << "REPAINT_UNEXPECTED_SUCCESS" << std::endl;
            } catch (const std::exception& error) {
                const auto after = resources.textures()[0];
                std::cout << "REPAINT_FAILURE " << error.what()
                          << " beforeValid=" << before.valid()
                          << " afterValid=" << after.valid()
                          << " loadedTextures=" << resources.loadingStats().loadedTextures
                          << std::endl;
                (void)resources.waitForAssets(requirements);
                std::cout << "WAIT_AFTER_FAILURE_RETURNED" << std::endl;
            }
            device.waitIdle();
        } else {
        for (uint32_t i = 0; i < manifest.models().size() && requirements.modelCount() < 2; ++i) {
            if (manifest.models()[i].geometry != ModelGeometry::Skinned) {
                requirements.requireModel(RenderModel { i + 1 });
            }
        }
        if (std::string_view(argv[2]) == "wait") {
            std::cout << "WAIT_ENTER models=" << requirements.modelCount() << std::endl;
            (void)resources.waitForAssets(requirements);
            std::cout << "WAIT_RETURNED" << std::endl;
        } else {
            resources.requestAssets(requirements);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                (void)resources.publishReadyAssets(1);
                resources.retireCompletedUploads();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto state = resources.loadingStats();
            std::cout << "queued=" << state.queuedAssets << " active=" << state.activeCpuJobs
                      << " cpuReadyModels=" << state.modelStages.cpuReady
                      << " prepared=" << state.preparedAssetBytes
                      << " preparedLimit=" << state.preparedAssetBudgetBytes
                      << " oversizedResidencyBlocks=" << state.residencyOversizedBlocks
                      << " failed=" << state.failedAssets << std::endl;
        }
        device.waitIdle();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
}
