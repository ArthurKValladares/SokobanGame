#include "ScopedTestDirectory.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/Ui.hpp"

#include <SDL3/SDL.h>
#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

using namespace sokoban;

void verify(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

ImageData render(SDL_Window* window, const std::filesystem::path& root, FontAtlas& font,
    const AssetManifest& manifest, const std::filesystem::path& cache, int scale, uint32_t icon)
{
    VulkanRenderer renderer(window, root, cache, manifest, font, AntiAliasingMode::None, scale,
        { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    const auto draw = [&](float phase) {
        ui.beginFrame({ 1600, 900 }, {}, false, false);
        ui.rect({ {}, { 1600, 900 } }, { 0, 0, 0, 1 });
        ui.text({ 24 + phase, 24 }, "Native text: n strong AV To ffi", { 1, 1, 1, 1 }, 24);
        ui.text({ 24 + phase, 70 }, "You are not strong enough", { 1, 1, 1, 1 }, 64);
        ui.text({ 24 + phase, 180 }, "strong", { 1, 1, 1, 1 }, 128, GlyphRendering::Outline);
        ui.text({ 24 + phase, 360 }, "strong", { 1, 1, 1, 1 }, 128, GlyphRendering::Coverage);
        const std::array runs { UiInlineRun { .text = "Press " }, UiInlineRun { .vectorIcon = icon },
            UiInlineRun { .text = " to continue" } };
        ui.inlineText({ 24, 540 }, runs, { 1, 1, 1, 1 }, 64);
        ui.text({ 24, 650 }, "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac  "
            "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7  "
            "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d  "
            "\xe0\xa4\xa8\xe0\xa4\xae\xe0\xa4\xb8\xe0\xa5\x8d\xe0\xa4\xa4\xe0\xa5\x87", { 1, 1, 1, 1 }, 48);
        ui.endFrame();
        renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
        ImGui::Render();
#endif
        RenderFrameData scene;
        scene.levelWidth = 1; scene.levelHeight = 1;
        // Use the docked composition path so a readback includes the UI.
        renderer.drawFrame(renderer.prepareFrame(std::move(scene)), ui.drawData(), true);
    };
    // Insert new size/position variants while both frame slots are in flight.
    for (int frame = 0; frame < 12; ++frame) draw(float(frame % 4) * 0.25f);
    draw(0);
    ImageData image = renderer.captureRenderedFrame();
    verify(image.width == uint32_t(1600 * scale / 100) && image.height == uint32_t(900 * scale / 100),
        "Capture dimensions changed with native UI composition");
    if (scale == 100) {
        uint64_t difference = 0, ink = 0;
        for (uint32_t y = 0; y < 140; ++y) {
            for (uint32_t x = 0; x < 500; ++x) {
                const auto analytic = std::to_integer<int>(image.rgba[(std::size_t(180 + y) * image.width + 24 + x) * 4]);
                const auto coverage = std::to_integer<int>(image.rgba[(std::size_t(360 + y) * image.width + 24 + x) * 4]);
                difference += static_cast<uint64_t>(std::abs(analytic - coverage));
                ink += static_cast<uint64_t>(std::max(analytic, coverage));
            }
        }
        verify(ink > 100000, "Large glyphs produced no ink");
        const double error = double(difference) / double(ink);
        std::cout << "Analytic/FreeType normalized pixel difference: " << error << '\n';
        verify(error < 0.15, "GPU outlines differ substantially from the independent FreeType rasterizer");
        verify(std::ranges::any_of(image.rgba, [](std::byte b) { const auto v = std::to_integer<int>(b); return v > 10 && v < 245; }),
            "Text has no antialiased pixels");
        const auto iconX = uint32_t(24 + ui.measureText("Press ", 64).x + 32);
        const auto iconY = uint32_t(540 + font.ascent() * 64 / font.pixelHeight() - 20);
        verify(std::to_integer<int>(image.rgba[(std::size_t(iconY) * image.width + iconX) * 4]) > 240,
            "Custom vector icon did not render through the outline pipeline");
    }
    return image;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        verify(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit { ~Quit() { SDL_Quit(); } } quit;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Text rendering regression", 1600, 900, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
        verify(bool(window), SDL_GetError());
        const std::filesystem::path root(SOKOBAN_TEXT_TEST_ASSETS);
        auto font = FontAtlas::loadDefault(root);
        const auto manifest = AssetManifest::loadFromFile(root / "manifest.json");
        using Command = IconPathCommand;
        const std::array path { Command { Command::Kind::Move, { { 0.1f, 0.1f } } },
            Command { Command::Kind::Line, { { 0.5f, 0.8f } } }, Command { Command::Kind::Line, { { 0.9f, 0.1f } } },
            Command { Command::Kind::Close } };
        const auto icon = font.registerIcon("controller.confirm", { path, 1 });
        ScopedTestDirectory temp("text-rendering");
        const auto image = render(window.get(), root, font, manifest, temp.path() / "cache.bin", 100, icon);
        (void)render(window.get(), root, font, manifest, temp.path() / "cache.bin", 50, icon);
        if (argc > 1) {
            std::vector<uint8_t> pixels(image.rgba.size());
            std::transform(image.rgba.begin(), image.rgba.end(), pixels.begin(), [](std::byte b) { return std::to_integer<uint8_t>(b); });
            writeRgbaPng(argv[1], image.width, image.height, pixels);
        }
        verify(vulkanDebug::validationErrorCount() == 0, "Vulkan validation reported a text-rendering error");
        std::cout << "Text rendering GPU regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Text rendering GPU regression failed: " << error.what() << '\n';
        return 1;
    }
}
