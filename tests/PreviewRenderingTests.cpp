#include "ScopedTestDirectory.hpp"
#include "GroundGpuFixture.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/FrameArena.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/PerformanceFixtures.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RockScratchConfig.hpp"
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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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

double verifyInteractionOutlineImage(const ImageData& baseline,
    const ImageData& outlined, bool requireBlueSilhouette = true)
{
    verify(baseline.width == outlined.width && baseline.height == outlined.height,
        "Interaction outline changed the capture extent");
    const auto channel = [](const ImageData& image, std::size_t pixel, int component) {
        return std::to_integer<int>(image.rgba[pixel * 4 + component]);
    };
    // The warm cube is the only colored geometry. Locate its actual rasterized
    // footprint so the checks also work after the renderer changes resolution.
    uint32_t left = baseline.width;
    uint32_t top = baseline.height;
    uint32_t right = 0;
    uint32_t bottom = 0;
    uint32_t cubePixels = 0;
    for (uint32_t y = 0; y < baseline.height; ++y) {
        for (uint32_t x = 0; x < baseline.width; ++x) {
            const std::size_t pixel = std::size_t(y) * baseline.width + x;
            if (channel(baseline, pixel, 0) > channel(baseline, pixel, 1) + 30 &&
                channel(baseline, pixel, 1) > channel(baseline, pixel, 2) + 10) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
                ++cubePixels;
            }
        }
    }
    verify(cubePixels > 100, "Interaction outline fixture did not render its warm cube");
    const int margin = std::max(3, static_cast<int>(std::ceil(
        6.0 * baseline.width / 640.0)));
    verify(int(right) - int(left) > margin * 2 && int(bottom) - int(top) > margin * 2,
        "Interaction outline fixture cube is too small to test its interior");
    double weight = 0.0;
    uint32_t bluePixels = 0;
    uint32_t darkenedPixels = 0;
    for (uint32_t y = 0; y < baseline.height; ++y) {
        for (uint32_t x = 0; x < baseline.width; ++x) {
            const std::size_t pixel = std::size_t(y) * baseline.width + x;
            int total = 0;
            int peak = 0;
            for (int component = 0; component < 3; ++component) {
                const int delta = std::abs(channel(baseline, pixel, component) -
                    channel(outlined, pixel, component));
                total += delta;
                peak = std::max(peak, delta);
            }
            weight += total / 3.0;
            const bool inside = int(x) > int(left) + margin &&
                int(x) < int(right) - margin && int(y) > int(top) + margin &&
                int(y) < int(bottom) - margin;
            const bool outside = int(x) < int(left) - margin ||
                int(x) > int(right) + margin || int(y) < int(top) - margin ||
                int(y) > int(bottom) + margin;
            if (inside) {
                verify(peak <= 1, "Interaction outline repainted the cube's interior");
            }
            if (outside) {
                verify(peak <= 1, "Interaction outline changed unflagged terrain");
            }
            if (channel(baseline, pixel, 0) > channel(outlined, pixel, 0) + 10) {
                ++darkenedPixels;
            }
            // Undersampling blends the navy hull into the neutral floor. It
            // must still darken that floor and move its hue toward blue, even
            // when the resulting RGB channels differ by only one step.
            if (peak > 10 && channel(outlined, pixel, 2) > channel(outlined, pixel, 0) &&
                channel(outlined, pixel, 2) > channel(outlined, pixel, 1) &&
                channel(outlined, pixel, 0) < channel(baseline, pixel, 0)) {
                ++bluePixels;
            }
        }
    }
    verify(darkenedPixels > 8, "Interaction outline did not shade any contour pixels");
    if (requireBlueSilhouette) {
        verify(bluePixels > 8, "Interaction outline did not produce a dark blue silhouette");
    }
    // Captures have the internal render dimensions. Dividing the changed
    // color energy by image area compares the same display-pixel-width ring
    // at different render scales, while retaining antialiasing coverage.
    return weight / (double(baseline.width) * baseline.height);
}

void verifyInteractionOutlines(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    const auto makeFrame = [](bool outline, bool occluded = false) {
        RenderFrameData frame = scene({ 0.55f, 0.55f, 0.55f, 1.0f }, false);
        frame.skyCloudsEnabled = false;
        frame.lighting.sun.intensity = 0.0f;
        frame.cameraOverride = RenderFrameData::CameraOverride {
            .position = { 2.0f, 2.0f, 10.0f },
            .forward = { 0.0f, 0.0f, -1.0f },
        };
        frame.tiles.push_back({
            .position = { 1.0f, 1.0f },
            .size = { 2.0f, 2.0f },
            .color = { 0.75f, 0.33f, 0.12f, 1.0f },
            .baseElevation = 1.0f,
            .height = 1.0f,
            .showGrid = false,
            .interactionOutline = outline,
        });
        if (occluded) {
            // A larger opaque block covers the entire outlined hull. Toggling
            // the hidden object's flag must leave every output pixel intact.
            frame.tiles.push_back({
                .position = { 0.5f, 0.5f },
                .size = { 3.0f, 3.0f },
                .color = { 0.55f, 0.55f, 0.55f, 1.0f },
                .baseElevation = 3.0f,
                .height = 0.3f,
                .showGrid = false,
            });
        }
        return frame;
    };
    const auto captureFrame = [&](const RenderFrameData& frame) {
        for (int repeat = 0; repeat < 3; ++repeat) {
            ui.beginFrame({ 640, 360 }, {}, false, false);
            ui.endFrame();
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(frame), ui.drawData());
            verify(!renderer.hasFatalFailure(), "Interaction outline fixture failed to render");
        }
        return renderer.captureRenderedFrame();
    };
    const auto capture = [&](bool outline, bool occluded = false) {
        return captureFrame(makeFrame(outline, occluded));
    };
    const auto evidence = std::filesystem::current_path() / "out" / "interaction-outline-evidence";
    std::filesystem::create_directories(evidence);
    double referenceWeight = 0.0;
    uint32_t referenceWidth = 0;
    for (const auto samples : std::array { AntiAliasingMode::None, AntiAliasingMode::Msaa4x }) {
        // Establish the reference at 100% before testing undersampling.
        for (int scale : std::array { 100, 50, 75 }) {
            renderer.setAntiAliasingMode(samples);
            renderer.setRenderScalePercent(scale);
            const ImageData baseline = capture(false);
            const ImageData outlined = capture(true);
            const std::string label = std::to_string(scale) +
                (samples == AntiAliasingMode::None ? "-1x" : "-msaa4x");
            saveRippleEvidence(evidence / (label + "-baseline.png"), baseline);
            saveRippleEvidence(evidence / (label + "-outlined.png"), outlined);
            // At undersampled/MSAA edges the navy can share every covered
            // pixel with the orange face. Assert its unblended hue at 100%;
            // the other scales still require a dark contour, intact interior
            // and stable coverage rather than an impossible pure-blue pixel.
            const double weight = verifyInteractionOutlineImage(baseline, outlined, scale == 100);
            if (referenceWeight == 0.0) {
                referenceWeight = weight;
                referenceWidth = baseline.width;
            }
            verify(baseline.width == referenceWidth * static_cast<uint32_t>(scale) / 100,
                "Interaction outline fixture did not apply its requested render scale");
            std::cout << "Interaction outline " << label << ": normalized coverage=" << weight
                      << ", reference ratio=" << weight / referenceWeight << '\n';
            verify(weight > referenceWeight * 0.45 && weight < referenceWeight * 1.75,
                "Interaction outline width changed with render scale or MSAA");
            const ImageData occludedBaseline = capture(false, true);
            const ImageData occludedOutline = capture(true, true);
            verify(difference(occludedBaseline, occludedOutline, 0, 0,
                    occludedBaseline.width, occludedBaseline.height) == 0.0,
                "Interaction outline showed through an opaque occluder");
        }
    }
    renderer.setAntiAliasingMode(AntiAliasingMode::Msaa4x);
    renderer.setRenderScalePercent(100);
    RenderFrameData transparentCube = makeFrame(false);
    transparentCube.tiles.back().blurBehind = true;
    transparentCube.tiles.back().color = { 1.0f, 0.30f, 0.03f, 0.65f };
    const ImageData transparentBaseline = captureFrame(transparentCube);
    transparentCube.tiles.back().interactionOutline = true;
    const ImageData transparentOutlined = captureFrame(transparentCube);
    saveRippleEvidence(evidence / "transparent-cube-baseline.png", transparentBaseline);
    saveRippleEvidence(evidence / "transparent-cube-outlined.png", transparentOutlined);
    // A translucent navy edge over the orange face can retain an orange hue;
    // the opaque cases above verify hue, while this case checks its coverage.
    (void)verifyInteractionOutlineImage(transparentBaseline, transparentOutlined, false);

    const auto glass = manifest.findModelIdByName("Glass");
    verify(glass.has_value(), "Interaction outline fixture is missing the Glass model");
    RenderFrameData glassFrame = makeFrame(false);
    glassFrame.tiles.back().model = *glass;
    glassFrame.tiles.back().color = { 1.0f, 1.0f, 1.0f, 1.0f };
    renderer.waitForAssets(renderAssetRequirementsForFrame(glassFrame));
    const ImageData glassBaseline = captureFrame(glassFrame);
    verify(renderer.renderStats().unavailableModels == 0,
        "Interaction outline Glass fixture did not finish loading its model");
    const uint32_t glassBaselineDraws = renderer.renderStats().drawCalls;
    glassFrame.tiles.back().interactionOutline = true;
    const ImageData glassOutlined = captureFrame(glassFrame);
    verify(renderer.renderStats().drawCalls > glassBaselineDraws,
        "Interaction outline did not draw the resident transparent Glass model");
    saveRippleEvidence(evidence / "glass-baseline.png", glassBaseline);
    saveRippleEvidence(evidence / "glass-outlined.png", glassOutlined);
    verify(difference(glassBaseline, glassOutlined, 0, 0,
            glassBaseline.width, glassBaseline.height) > 0.0,
        "Interaction outline did not visibly contour the transparent Glass model");
    // Transparent geometry contributes no self-depth. An expanded back hull
    // would consequently paint the whole center navy; a surface contour must
    // preserve the view through the broad, camera-facing part of real Glass.
    const double glassInteriorChange = difference(glassBaseline, glassOutlined,
        glassBaseline.width * 48 / 100, glassBaseline.height * 46 / 100,
        glassBaseline.width * 52 / 100, glassBaseline.height * 54 / 100);
    std::cout << "Transparent Glass: interior change=" << glassInteriorChange << '\n';
    verify(glassInteriorChange < 0.1,
        "Interaction outline filled the transparent Glass model's interior");
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported an interaction outline rendering error");
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

void verifyRockScratches(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    const auto splat = groundSplatTexturesForScreen(
        [&](std::string_view name) { return manifest.findTextureIdByName(name); }, std::nullopt);
    verify(splat.valid(), "Rock scratch fixture is missing its ground textures");
    constexpr float pushDuration = 0.6f;
    const auto makeFrame = [&](std::optional<float> age, float duration = 0.6f) {
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = 8;
        frame.levelHeight = 8;
        frame.levelDepth = 2;
        frame.skyCloudsEnabled = false;
        frame.lighting.sun.intensity = 0.0f;
        frame.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 1 };
        // Crop slightly inside the floor: the scratch reaches the uncovered
        // main-view perimeter as well as the preview's opaque center.
        frame.cameraOverride = RenderFrameData::CameraOverride {
            .position = { 4.0f, 4.0f, 4.0f }, .forward = { 0.0f, 0.0f, -1.0f },
        };
        frame.groundSplat = splat;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                frame.tiles.push_back({
                    .cell = { x, y, 0 },
                    .position = { static_cast<float>(x), static_cast<float>(y) },
                    .color = { 1, 1, 1, 1 }, .height = 1.0f, .showGrid = false,
                    .effect = RenderSurfaceEffect::GroundSplat, .groundTop = true,
                });
            }
        }
        if (age) {
            frame.rockScratches[0] = {
                .start = { 1.0f, 4.0f, 1.0f }, .end = { 7.0f, 4.0f, 1.0f },
                .ageSeconds = *age, .durationSeconds = duration,
            };
            frame.rockScratchCount = 1;
        }
        return frame;
    };
    const RenderFrameData quiet = makeFrame(std::nullopt);
    renderer.waitForAssets(renderAssetRequirementsForFrame(quiet));
    const auto capture = [&](const RenderFrameData& main,
                             const std::optional<RenderFrameData>& preview = std::nullopt) {
        for (int frame = 0; frame < 3; ++frame) {
            ui.beginFrame({ 640, 360 }, {}, false, false);
            if (preview) ScreenPreviewOverlay::draw(ui, { 640, 360 });
            ui.endFrame();
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(main, preview), ui.drawData(), bool(preview));
            verify(!renderer.hasFatalFailure(), "Rock scratch fixture failed to render");
        }
        return renderer.captureRenderedFrame();
    };
    const RenderFrameData active = makeFrame(pushDuration + 0.1f);
    const ImageData baseline = capture(quiet);
    const ImageData full = capture(active);
    const ImageData partial = capture(makeFrame(pushDuration * 0.5f));
    const ImageData fading = capture(makeFrame(config::rockScratchLifetimeSeconds - 1.0f));
    const ImageData expired = capture(makeFrame(config::rockScratchLifetimeSeconds + pushDuration + 0.1f));
    const auto fullChange = rippleImageDifference(baseline, full);
    const auto partialChange = rippleImageDifference(baseline, partial);
    const auto fadingChange = rippleImageDifference(baseline, fading);
    const std::filesystem::path evidence = std::filesystem::current_path() /
        "out" / "rock-scratch-evidence";
    std::filesystem::create_directories(evidence);
    saveRippleEvidence(evidence / "baseline.png", baseline);
    saveRippleEvidence(evidence / "full.png", full);
    saveRippleEvidence(evidence / "partial.png", partial);
    saveRippleEvidence(evidence / "fading.png", fading);
    std::cout << "Rock scratch: full weight=" << fullChange.weight << ", peak=" << fullChange.peak
              << ", partial weight=" << partialChange.weight << ", fading weight=" << fadingChange.weight << '\n';
    verify(fullChange.weight > 200.0 && fullChange.peak > 3.0,
        "Rock scratch did not visibly alter the ground material");
    verify(partialChange.weight > fullChange.weight * 0.2 &&
            partialChange.weight < fullChange.weight * 0.8,
        "Rock scratch was not progressively revealed along the push");
    verify(fadingChange.weight > 0.0 && fadingChange.weight < fullChange.weight * 0.5,
        "Rock scratch did not gradually fade before expiring");
    verify(rippleImageDifference(baseline, expired).weight == 0.0,
        "Expired rock scratch still changed the ground");

    // Stretch deposition time so the oldest end is gone while the newest
    // end remains visible. In this vertical camera, +world-X points left.
    const ImageData orderedFade = capture(makeFrame(config::rockScratchLifetimeSeconds + 0.5f, 4.0f));
    verify(difference(baseline, orderedFade, baseline.width * 9 / 10, baseline.height * 2 / 5,
            baseline.width, baseline.height * 3 / 5) == 0.0,
        "Rock scratch's oldest end outlived its deposited lifetime");
    verify(difference(baseline, orderedFade, baseline.width / 10, baseline.height * 2 / 5,
            baseline.width / 5, baseline.height * 3 / 5) > 0.05,
        "Rock scratch's newer end disappeared with its older end");

    RenderFrameData wrongHeight = active;
    wrongHeight.rockScratches[0].start.z += 1.0f;
    wrongHeight.rockScratches[0].end.z += 1.0f;
    verify(rippleImageDifference(baseline, capture(wrongHeight)).weight == 0.0,
        "Rock scratch leaked onto a ground layer at another elevation");
    for (const bool cliff : { false, true }) {
        RenderFrameData excluded = quiet;
        for (auto& tile : excluded.tiles) {
            tile.groundTop = cliff;
            tile.cliffWall = cliff;
        }
        const ImageData excludedBaseline = capture(excluded);
        excluded.rockScratches = active.rockScratches;
        excluded.rockScratchCount = active.rockScratchCount;
        verify(rippleImageDifference(excludedBaseline, capture(excluded)).weight == 0.0,
            cliff ? "Rock scratch changed a cliff top" : "Rock scratch changed a non-ground surface");
    }
    RenderFrameData water = quiet;
    water.tiles.clear();
    water.waterSurfaces.push_back({ .size = { 8, 8 }, .color = { .1f, .4f, .6f, .5f }, .elevation = 1 });
    water.waterGridBounds = { .width = 8, .height = 8 };
    water.waterAnimationTimeSeconds = 0.7f;
    const ImageData waterBaseline = capture(water);
    water.rockScratches = active.rockScratches;
    water.rockScratchCount = active.rockScratchCount;
    verify(rippleImageDifference(waterBaseline, capture(water)).weight == 0.0,
        "Rock scratch changed a water-only frame");

    const ImageData previewBaseline = capture(quiet, quiet);
    const ImageData mainScratch = capture(active, quiet);
    const ImageData previewScratch = capture(quiet, active);
    const uint32_t xBegin = baseline.width * 2 / 5;
    const uint32_t xEnd = baseline.width * 3 / 5;
    const uint32_t yBegin = baseline.height * 2 / 5;
    const uint32_t yEnd = baseline.height * 3 / 5;
    verify(difference(previewBaseline, mainScratch, xBegin, yBegin, xEnd, yEnd) == 0.0,
        "Main-view rock scratch leaked into a quiet preview");
    verify(difference(previewBaseline, previewScratch, xBegin, yBegin, xEnd, yEnd) > 0.05,
        "Preview ground did not render its own rock scratch");
    verify(difference(previewBaseline, mainScratch, 0, yBegin, baseline.width / 16, yEnd) > 0.05,
        "Rock scratch fixture did not cover the visible main-view perimeter");
    verify(difference(previewBaseline, previewScratch, 0, yBegin, baseline.width / 16, yEnd) == 0.0,
        "Preview rock scratch leaked into the quiet main view");
    saveRippleEvidence(evidence / "preview.png", previewScratch);

    // Exercise real push contacts through the gameplay frame builder and the
    // shipped native cap meshes, retaining the rock, water, and scene lighting.
    RockScratchEvidenceFixture gameplayFixture;
    PresentationSettings settings;
    settings.applyTileScales(manifest);
    GroundGeometryCache groundCache;
    FrameArena arena("rock scratch gameplay regression", renderFrameArenaBytes());
    const RenderFrameData gameplay = gameplayFixture.build(manifest, settings, groundCache, arena);
    verify(gameplay.rockScratchCount > 0 && !gameplay.waterSurfaces.empty() &&
            std::ranges::any_of(gameplay.tiles, [](const auto& tile) { return tile.groundModule; }),
        "Rock scratch gameplay fixture did not emit its native ground, water, and push contacts");
    renderer.waitForAssets(renderAssetRequirementsForFrame(gameplay));
    RenderFrameData gameplayQuiet = gameplay;
    gameplayQuiet.rockScratchCount = 0;
    const ImageData gameplayBaseline = capture(gameplayQuiet);
    const ImageData gameplayImage = capture(gameplay);
    saveRippleEvidence(evidence / "gameplay-baseline.png", gameplayBaseline);
    saveRippleEvidence(evidence / "gameplay.png", gameplayImage);
    const auto gameplayChange = rippleImageDifference(gameplayBaseline, gameplayImage);
    std::cout << "Gameplay rock scratch: weight=" << gameplayChange.weight
              << ", peak=" << gameplayChange.peak << '\n';
    verify(gameplayChange.weight > 100.0 && gameplayChange.peak > 2.0,
        "Real gameplay pushes did not visibly scratch the shipped native ground caps");
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a rock scratch rendering error");
}

void verifyRimHeavyGroundChunkPublication(SDL_Window* window,
    const std::filesystem::path& root, FontAtlas& font,
    const AssetManifest& manifest, const std::filesystem::path& cache)
{
    const LegacyGroundGpuFixture fixture(root, manifest);
    const auto& legacyManifest = fixture.manifest();
    VulkanRenderer renderer(window, fixture.root(), cache, legacyManifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    renderer.setFrustumCullingEnabled(false);
    constexpr uint32_t rowWidth = 30;
    constexpr uint32_t tileCount = rowWidth * rowWidth;
    constexpr uint64_t uploadLimit = 8ULL * 1024 * 1024;
    const RenderModel ground = legacyManifest.modelIdByName("GroundRock01");
    const auto splat = groundSplatTexturesForScreen(
        [&](std::string_view name) { return legacyManifest.findTextureIdByName(name); },
        std::nullopt);
    verify(splat.valid() && !splat.rimWall.isNone() && !splat.rimWallNormal.isNone() &&
            !splat.rimWallOrm.isNone(),
        "Ground chunk fixture is missing its splat or rim-wall textures");

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
    std::cout << "Rim-heavy legacy ground chunks: " << tileCount
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
        if (argc > 1 && std::string_view(argv[1]) == "--interaction-outlines") {
            verifyInteractionOutlines(window.get(), root, font, manifest,
                temp.path() / "cache.bin");
            std::cout << "Interaction outline GPU regression passed\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--water-entry-ripples") {
            verifyWaterEntryRipples(window.get(), root, font, manifest,
                temp.path() / "cache.bin");
            verify(vulkanDebug::validationErrorCount() == 0,
                "Vulkan validation reported a water-entry ripple rendering error");
            std::cout << "Water-entry ripple GPU regression passed\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--rock-scratches") {
            verifyRockScratches(window.get(), root, font, manifest, temp.path() / "cache.bin");
            std::cout << "Rock scratch GPU regression passed\n";
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
