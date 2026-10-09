#include "ScopedTestDirectory.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
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
#include <optional>
#include <stdexcept>
#include <string_view>
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

struct RippleImageDifference {
    double weight = 0.0;
    double radiusPixels = 0.0;
    double peak = 0.0;
};

RippleImageDifference rippleImageDifference(const ImageData& baseline,
    const ImageData& image)
{
    double weight = 0.0;
    double weightedX = 0.0;
    double weightedY = 0.0;
    double weightedRadiusSquared = 0.0;
    double peak = 0.0;
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            const std::size_t pixel = (std::size_t(y) * image.width + x) * 4;
            double pixelDifference = 0.0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                pixelDifference += std::abs(
                    std::to_integer<int>(baseline.rgba[pixel + channel]) -
                    std::to_integer<int>(image.rgba[pixel + channel]));
            }
            pixelDifference /= 3.0;
            weight += pixelDifference;
            weightedX += pixelDifference * x;
            weightedY += pixelDifference * y;
            weightedRadiusSquared += pixelDifference * (double(x) * x + double(y) * y);
            peak = std::max(peak, pixelDifference);
        }
    }
    double radius = 0.0;
    if (weight > 0.0) {
        const double centerX = weightedX / weight;
        const double centerY = weightedY / weight;
        radius = std::sqrt(std::max(0.0,
            weightedRadiusSquared / weight - centerX * centerX - centerY * centerY));
    }
    return { weight, radius, peak };
}

void saveRippleEvidence(const std::filesystem::path& path, const ImageData& image)
{
    std::vector<uint8_t> pixels(image.rgba.size());
    std::transform(image.rgba.begin(), image.rgba.end(), pixels.begin(),
        [](std::byte b) { return std::to_integer<uint8_t>(b); });
    writeRgbaPng(path, image.width, image.height, pixels);
}

void verifyWaterEntryRipples(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    const auto makeFrame = [](std::optional<float> rippleAge, bool water = true,
                              float rippleElevation = 1.82f) {
        RenderFrameData frame = scene({ 0.16f, 0.18f, 0.22f, 1.0f }, water);
        frame.levelWidth = 8;
        frame.levelHeight = 8;
        // A vertical view makes the radial spread measurable without assuming
        // a particular perspective camera's world-to-screen projection.
        frame.cameraPitchDegrees = 0.0f;
        frame.tiles.front().size = { 8.0f, 8.0f };
        frame.waterAnimationTimeSeconds = 0.7f;
        if (water) {
            frame.waterSurfaces.front().size = { 8.0f, 8.0f };
            frame.waterGridBounds = { .width = 8, .height = 8 };
            frame.waterRendering.visualizeCausticsOnly = false;
            frame.waterRendering.primaryRippleOpacity = 0.0f;
            frame.waterRendering.secondaryRippleOpacity = 0.0f;
        }
        if (rippleAge) {
            frame.waterRipples[0] = {
                .position = { 4.0f, 4.0f, rippleElevation },
                .ageSeconds = *rippleAge,
            };
            frame.waterRippleCount = 1;
        }
        return frame;
    };
    const auto capture = [&](const RenderFrameData& main,
                             const std::optional<RenderFrameData>& preview = std::nullopt) {
        // Repeat across both frame slots: per-view ripple uniforms must not
        // depend on whichever descriptor slot was used in the prior capture.
        for (int frame = 0; frame < 3; ++frame) {
            ui.beginFrame({ 640, 360 }, {}, false, false);
            if (preview) {
                ScreenPreviewOverlay::draw(ui, { 640, 360 });
            }
            ui.endFrame();
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(main, preview), ui.drawData(), bool(preview));
            verify(!renderer.hasFatalFailure(), "Water-entry ripple fixture failed to render");
        }
        return renderer.captureRenderedFrame();
    };

    const ImageData baseline = capture(makeFrame(std::nullopt));
    const ImageData early = capture(makeFrame(0.16f));
    const ImageData expanded = capture(makeFrame(0.55f));
    const ImageData fading = capture(makeFrame(1.35f));
    const ImageData expired = capture(makeFrame(1.5f));
    const auto earlyDifference = rippleImageDifference(baseline, early);
    const auto expandedDifference = rippleImageDifference(baseline, expanded);
    const auto fadingDifference = rippleImageDifference(baseline, fading);
    std::cout << "Water-entry ripple: early radius=" << earlyDifference.radiusPixels
              << ", expanded radius=" << expandedDifference.radiusPixels
              << ", expanded peak=" << expandedDifference.peak
              << ", fading peak=" << fadingDifference.peak << '\n';

    // Keep snapshots on failures as well as passes, under the ignored output
    // directory, so the actual shaded ring can be reviewed independently.
    const std::filesystem::path evidence = std::filesystem::current_path() /
        "out" / "water-ripple-evidence";
    std::filesystem::create_directories(evidence);
    saveRippleEvidence(evidence / "baseline.png", baseline);
    saveRippleEvidence(evidence / "early.png", early);
    saveRippleEvidence(evidence / "expanded.png", expanded);
    saveRippleEvidence(evidence / "fading.png", fading);
    saveRippleEvidence(evidence / "expired.png", expired);

    verify(earlyDifference.weight > 200.0 && earlyDifference.peak > 5.0,
        "Water-entry ripple did not visibly alter the shaded water");
    verify(expandedDifference.radiusPixels > earlyDifference.radiusPixels * 1.5,
        "Water-entry ripple did not expand away from its impact point");
    verify(expandedDifference.peak > 5.0 &&
            fadingDifference.peak < expandedDifference.peak * 0.6,
        "Water-entry ripple did not fade before its lifetime ended");
    verify(rippleImageDifference(baseline, expired).weight == 0.0,
        "Expired water-entry ripple still changed the water image");
    verify(rippleImageDifference(baseline,
            capture(makeFrame(0.55f, true, 2.82f))).weight == 0.0,
        "Water-entry ripple leaked onto water at another elevation");
    const ImageData land = capture(makeFrame(std::nullopt, false));
    verify(rippleImageDifference(land, capture(makeFrame(0.55f, false))).weight == 0.0,
        "Water-entry ripple changed an opaque land-only frame");

    const RenderFrameData quiet = makeFrame(std::nullopt);
    const RenderFrameData active = makeFrame(0.16f);
    const ImageData previewBaseline = capture(quiet, quiet);
    const ImageData mainRipple = capture(active, quiet);
    const ImageData previewRipple = capture(quiet, active);
    const uint32_t xBegin = previewBaseline.width * 2 / 5;
    const uint32_t xEnd = previewBaseline.width * 3 / 5;
    const uint32_t yBegin = previewBaseline.height * 2 / 5;
    const uint32_t yEnd = previewBaseline.height * 3 / 5;
    verify(difference(previewBaseline, mainRipple, xBegin, yBegin, xEnd, yEnd) == 0.0,
        "Main-view water-entry ripple leaked into the preview's water uniforms");
    verify(difference(previewBaseline, previewRipple, xBegin, yBegin, xEnd, yEnd) > 0.1,
        "Preview water did not render its own water-entry ripple");
    saveRippleEvidence(evidence / "preview.png", previewRipple);

    // Match a .15 s sample of the default .25 s move: one horizontal unit,
    // then a .18 unit sink. The block has moved .708 units and the effect is
    // .044 s old after its leading edge first touched water. A completed-step
    // trigger would hide the entire effect in this common one-tile pool case.
    constexpr float moveDurationSeconds = 0.25f;
    constexpr float pathDistance = 1.18f;
    constexpr float sampleTimeSeconds = 0.15f;
    constexpr float blockPositionX = sampleTimeSeconds / moveDurationSeconds * pathDistance;
    constexpr float contactSeconds = moveDurationSeconds * 0.5f / pathDistance;
    RenderFrameData smallPool = makeFrame(std::nullopt);
    smallPool.levelWidth = 3;
    smallPool.levelHeight = 3;
    smallPool.tiles.front().size = { 3.0f, 3.0f };
    smallPool.tiles.front().height = 0.35f;
    smallPool.waterSurfaces.front().position = { 1.0f, 1.0f };
    smallPool.waterSurfaces.front().size = { 1.0f, 1.0f };
    smallPool.waterSurfaces.front().elevation = 0.82f;
    smallPool.waterGridBounds = { .width = 3, .height = 3 };
    smallPool.tiles.push_back({
        .position = { 0.0f, 1.0f },
        .color = { 0.30f, 0.32f, 0.35f, 1.0f },
        .height = 1.0f,
        .showGrid = false,
    });
    smallPool.tiles.push_back({
        .position = { blockPositionX, 1.0f },
        .color = { 0.62f, 0.30f, 0.08f, 1.0f },
        .baseElevation = 1.0f,
        .height = 1.0f,
        .showGrid = false,
    });
    const ImageData enteringBlockBaseline = capture(smallPool);
    smallPool.waterRipples[0] = {
        .position = { 1.5f, 1.5f, 0.82f },
        .ageSeconds = sampleTimeSeconds - contactSeconds,
    };
    smallPool.waterRippleCount = 1;
    const ImageData enteringBlockRipple = capture(smallPool);
    saveRippleEvidence(evidence / "entering-block-baseline.png", enteringBlockBaseline);
    saveRippleEvidence(evidence / "entering-block-ripple.png", enteringBlockRipple);
    const auto enteringBlockDifference = rippleImageDifference(
        enteringBlockBaseline, enteringBlockRipple);
    std::cout << "One-tile pool entering block: ripple weight="
              << enteringBlockDifference.weight << ", peak="
              << enteringBlockDifference.peak << '\n';
    verify(enteringBlockDifference.weight > 200.0 && enteringBlockDifference.peak > 5.0,
        "Water-entry ripple was hidden by the block before it finished entering a one-tile pool");
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a water-entry ripple rendering error");
}

void verifyRimHeavyGroundChunkPublication(SDL_Window* window,
    const std::filesystem::path& root, FontAtlas& font,
    const AssetManifest& manifest, const std::filesystem::path& cache)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    renderer.setFrustumCullingEnabled(false);
    constexpr uint32_t rowWidth = 30;
    constexpr uint32_t tileCount = rowWidth * rowWidth;
    constexpr uint64_t uploadLimit = 8ULL * 1024 * 1024;
    const RenderModel ground = manifest.modelIdByName("GroundRock01");
    const auto splat = groundSplatTexturesForScreen(
        [&](std::string_view name) { return manifest.findTextureIdByName(name); },
        std::nullopt);
    verify(splat.valid(), "Ground chunk fixture is missing its splat textures");

    const auto makeFrame = [&](bool chunks, bool optimize) {
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = rowWidth * 2;
        frame.levelHeight = rowWidth * 2;
        frame.levelDepth = 1;
        // Pitch is measured from vertical. The long top-view lens keeps all
        // 38 facets of every isolated rim facing the camera. Their individual
        // cap draws exceed the ordinary reserve, but their chunks fit 8 MiB.
        frame.cameraPitchDegrees = 0.0f;
        frame.cameraDistanceMultiplier = 4.0f;
        frame.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 1 };
        frame.groundSplat = splat;
        frame.groundChunksRequested = chunks;
        frame.groundChunkMeshoptimizer = optimize;
        frame.requestedGroundRimWidth = 0.12f;
        frame.requestedGroundRimDepth = 0.10f;
        for (uint32_t index = 0; index < tileCount; ++index) {
            const int x = static_cast<int>((index % rowWidth) * 2);
            const int y = static_cast<int>((index / rowWidth) * 2);
            RenderFrameData::Tile tile {
                .cell = { x, y, 0 },
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .color = { 1, 1, 1, 1 },
                .height = 1.0f,
                .showGrid = false,
                .model = ground,
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
    renderer.waitForAssets(renderAssetRequirementsForFrame(makeFrame(true, true)));
    UiDrawData ui;
    ui.viewportSize = { 640, 360 };
    const auto draw = [&](bool chunks, bool optimize) {
        renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
        ImGui::Render();
#endif
        renderer.drawFrame(renderer.prepareFrame(makeFrame(chunks, optimize)), ui);
        verify(!renderer.hasFatalFailure(), "Ground chunk fixture failed to render");
        verify(renderer.assetLoadingStats().droppedDrawInstances == 0,
            "Rim-heavy chunk publication dropped draw instances");
        const RenderStats stats = renderer.renderStats();
        verify(stats.unavailableModels == 0 && stats.groundTrianglesBeforeProcessing > 0,
            "Ground chunk fixture did not draw its preloaded rock bodies");
        // Polling the real upload fence on the next frame stays deterministic
        // even when the test machine submits the small render very quickly.
        renderer.waitIdle();
        return stats;
    };
    const auto verifyAdopted = [&](const RenderStats& stats, uint64_t uploads,
                                  uint64_t rebuilds, bool optimize) {
        verify(stats.groundChunkTiles == tileCount && stats.groundChunkDraws > 0,
            "Rim-heavy view did not adopt every uploaded ground tile");
        verify(!stats.groundRimBudgetFallback && stats.resolvedGroundRimTiles == tileCount,
            "Uploaded ground chunks did not restore the requested body/cap rims");
        verify(stats.groundChunkUploads == uploads && stats.groundChunkCacheRebuilds == rebuilds,
            "Stable ground chunks repeatedly rebuilt or uploaded their geometry");
        verify(stats.groundChunkGeometryBytes > 0 && stats.groundChunkGeometryBytes <= uploadLimit &&
                stats.groundChunkOriginalBytes <= uploadLimit,
            "Rim-heavy ground fixture exceeded the chunk upload limit");
        verify(stats.groundChunksRequested && stats.groundChunkMeshoptimizer == optimize,
            "Ground chunk request or optimizer mode was lost during publication");
    };
    const auto awaitAdoption = [&](RenderStats stats, uint64_t uploads,
                                  uint64_t rebuilds, bool optimize) {
        uint32_t frames = 1;
        while (stats.groundChunkTiles != tileCount && frames < 8) {
            stats = draw(true, optimize);
            ++frames;
        }
        verifyAdopted(stats, uploads, rebuilds, optimize);
        return stats;
    };

    const RenderStats first = draw(true, true);
    verify(first.groundRimBudgetFallback && first.resolvedGroundRimTiles == 0 &&
            first.groundChunkDraws == 0,
        "Rim-heavy first frame did not exercise the unready flat fallback");
    verify(first.groundChunkUploads == 1 && first.groundChunkCacheRebuilds == 1,
        "Flat fallback replaced or failed to upload the requested rim generation");
    const RenderStats adopted = awaitAdoption(first, 1, 1, true);
    for (int repeat = 0; repeat < 3; ++repeat) {
        const RenderStats stable = draw(true, true);
        verifyAdopted(stable, 1, 1, true);
        verify(stable.groundChunkCacheHits > adopted.groundChunkCacheHits,
            "Warm rim-heavy frames did not reuse their compiled geometry");
    }

    const RenderStats disabled = draw(false, true);
    verify(disabled.groundChunkDraws == 0 && disabled.groundRimBudgetFallback &&
            disabled.resolvedGroundRimTiles == 0,
        "Disabling chunks did not retain the coherent flat draw-budget fallback");
    verify(disabled.groundChunkUploads == 1 && disabled.groundChunkCacheRebuilds == 1,
        "Disabling chunks rebuilt or replaced the retained geometry");
    verifyAdopted(draw(true, true), 1, 1, true);

    const RenderStats changed = draw(true, false);
    verify(changed.groundRimBudgetFallback && changed.resolvedGroundRimTiles == 0 &&
            changed.groundChunkDraws == 0 && changed.groundChunkUploads == 2 &&
            changed.groundChunkCacheRebuilds == 2,
        "Optimizer replacement did not upload one retained generation through flat fallback");
    (void)awaitAdoption(changed, 2, 2, false);
    verifyAdopted(draw(true, false), 2, 2, false);
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a rim-heavy ground chunk publication error");
    std::cout << "Rim-heavy ground chunks: " << tileCount
              << " tiles adopted without publication starvation\n";
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
        if (argc > 1 && std::string_view(argv[1]) == "--water-entry-ripples") {
            verifyWaterEntryRipples(window.get(), root, font, manifest,
                temp.path() / "cache.bin");
            verify(vulkanDebug::validationErrorCount() == 0,
                "Vulkan validation reported a water-entry ripple rendering error");
            std::cout << "Water-entry ripple GPU regression passed\n";
            return 0;
        }
        const auto image = verifyPreview(window.get(), root, font, manifest,
            temp.path() / "cache.bin", AntiAliasingMode::None, 100);
        (void)verifyPreview(window.get(), root, font, manifest,
            temp.path() / "cache.bin", AntiAliasingMode::Msaa4x, 50);
        verifyRimHeavyGroundChunkPublication(window.get(), root, font, manifest,
            temp.path() / "cache.bin");
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
