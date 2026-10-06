#include "ScopedTestDirectory.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/Ui.hpp"

#include <SDL3/SDL.h>
#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace sokoban;

void verify(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

RenderFrameData scene(Vec4 floorColor, bool water, bool mainWorld = false)
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 4;
    frame.levelHeight = 4;
    frame.levelDepth = 2;
    frame.cameraPitchDegrees = 60.0f;
    frame.lighting.ambient = { .color = { 1.0f, 1.0f, 1.0f }, .intensity = 1.0f };
    frame.tiles.push_back({
        .position = mainWorld ? Vec2 { -30.0f, -30.0f } : Vec2 { 0.0f, 0.0f },
        .size = mainWorld ? Vec2 { 64.0f, 64.0f } : Vec2 { 4.0f, 4.0f },
        .color = floorColor,
        .height = 1.0f,
        .showGrid = false,
        .affectsCameraFit = !mainWorld,
    });
    if (water) {
        frame.waterSurfaces.push_back({
            .position = { 0.0f, 0.0f },
            .size = { 4.0f, 4.0f },
            .color = { 0.1f, 0.4f, 0.6f, 0.5f },
            .elevation = 1.82f,
        });
        frame.waterGridBounds = { .width = 4, .height = 4 };
        frame.waterRendering.reflectionStrength = 0.0f;
        frame.waterRendering.refractionStrength = 0.0f;
        frame.waterRendering.underwaterCausticStrength = 0.0f;
        // Show the sampled floor directly to make the refraction source
        // independent of artistic water tuning.
        frame.waterRendering.visualizeCausticsOnly = true;
    }
    return frame;
}

double difference(const ImageData& left, const ImageData& right,
    uint32_t xBegin, uint32_t yBegin, uint32_t xEnd, uint32_t yEnd)
{
    uint64_t total = 0;
    for (uint32_t y = yBegin; y < yEnd; ++y) {
        for (uint32_t x = xBegin; x < xEnd; ++x) {
            const std::size_t pixel = (std::size_t(y) * left.width + x) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                total += static_cast<uint64_t>(std::abs(
                    std::to_integer<int>(left.rgba[pixel + channel]) -
                    std::to_integer<int>(right.rgba[pixel + channel])));
            }
        }
    }
    return double(total) / (double(xEnd - xBegin) * double(yEnd - yBegin) * 3.0);
}

ImageData verifyPreview(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache,
    AntiAliasingMode samples, int scale)
{
    VulkanRenderer renderer(window, root, cache, manifest, font, samples, scale,
        { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    const auto capture = [&](Vec4 mainColor, Vec4 previewColor, bool water,
                             bool causticsOnly = true) {
        // Exercise both descriptor frame slots and retained image layouts.
        for (int frame = 0; frame < 3; ++frame) {
            ui.beginFrame({ 640, 360 }, {}, false, false);
            ScreenPreviewOverlay::draw(ui, { 640, 360 });
            ui.endFrame();
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            auto preview = scene(previewColor, water);
            preview.waterRendering.visualizeCausticsOnly = causticsOnly;
            if (!causticsOnly) {
                preview.waterRendering.refractionStrength =
                    RenderFrameData::defaultWaterRendering().refractionStrength;
                preview.waterRendering.reflectionStrength =
                    RenderFrameData::defaultWaterRendering().reflectionStrength;
                preview.waterRendering.underwaterCausticStrength =
                    RenderFrameData::defaultWaterRendering().underwaterCausticStrength;
            }
            renderer.drawFrame(renderer.prepareFrame(
                scene(mainColor, false, true), std::move(preview)), ui.drawData(), true);
        }
        return renderer.captureRenderedFrame();
    };
    constexpr Vec4 red { 0.9f, 0.05f, 0.05f, 1.0f };
    constexpr Vec4 green { 0.05f, 0.9f, 0.05f, 1.0f };
    constexpr Vec4 blue { 0.05f, 0.05f, 0.9f, 1.0f };
    const ImageData redWorld = capture(red, blue, true);
    const ImageData greenWorld = capture(green, blue, true);
    const ImageData greenFloor = capture(green, green, true);
    const uint32_t xBegin = redWorld.width * 2 / 5;
    const uint32_t xEnd = redWorld.width * 3 / 5;
    const uint32_t yBegin = redWorld.height * 2 / 5;
    const uint32_t yEnd = redWorld.height * 3 / 5;
    const double worldLeak = difference(redWorld, greenWorld, xBegin, yBegin, xEnd, yEnd);
    const double floorChange = difference(greenWorld, greenFloor, xBegin, yBegin, xEnd, yEnd);
    // This band sits at the feathered inset perimeter, where the main view
    // must still be visible through the UI's preserved snapshot.
    const double featherChange = difference(redWorld, greenWorld,
        redWorld.width / 8, redWorld.height / 4,
        redWorld.width / 8 + redWorld.width / 50, redWorld.height * 3 / 4);
    std::cout << "Preview scale " << scale << ": world leak=" << worldLeak
              << ", floor change=" << floorChange << ", feather change=" << featherChange << '\n';
    verify(worldLeak < 0.1, "Preview water sampled the main world instead of its opaque floor");
    verify(floorChange > 20.0, "Preview water did not show changes to its solid floor");
    verify(featherChange > 5.0, "Preview feather lost the preserved main view");
    const ImageData tintedRedWorld = capture(red, blue, true, false);
    const ImageData tintedGreenWorld = capture(green, blue, true, false);
    verify(difference(tintedRedWorld, tintedGreenWorld, xBegin, yBegin, xEnd, yEnd) < 0.1,
        "Tinted refracting preview water leaked the main world");
    // Return to an opaque preview after sampling depth, then back to water.
    (void)capture(red, blue, false);
    return capture(red, blue, true, false);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        verify(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Preview rendering regression", 640, 360,
                SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        verify(bool(window), SDL_GetError());
        const std::filesystem::path root(SOKOBAN_PREVIEW_TEST_ASSETS);
        auto font = FontAtlas::loadDefault(root);
        const auto manifest = AssetManifest::loadFromFile(root / "manifest.json");
        ScopedTestDirectory temp("preview-rendering");
        const auto image = verifyPreview(window.get(), root, font, manifest,
            temp.path() / "cache.bin", AntiAliasingMode::None, 100);
        (void)verifyPreview(window.get(), root, font, manifest,
            temp.path() / "cache.bin", AntiAliasingMode::Msaa4x, 50);
        if (argc > 1) {
            std::vector<uint8_t> pixels(image.rgba.size());
            std::transform(image.rgba.begin(), image.rgba.end(), pixels.begin(),
                [](std::byte b) { return std::to_integer<uint8_t>(b); });
            writeRgbaPng(argv[1], image.width, image.height, pixels);
        }
        verify(vulkanDebug::validationErrorCount() == 0,
            "Vulkan validation reported a preview-rendering error");
        std::cout << "Preview rendering GPU regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Preview rendering GPU regression failed: " << error.what() << '\n';
        return 1;
    }
}
