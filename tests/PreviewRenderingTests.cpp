#include "ScopedTestDirectory.hpp"
#include "GroundGpuFixture.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/FrameArena.hpp"
#include "engine/GroundGeometry.hpp"
#include "engine/PerformanceFixtures.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/RockScratchConfig.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/render/PngWriter.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/VulkanDebugUtils.hpp"
#include "engine/render/VulkanRenderer.hpp"
#include "engine/render/WaterGeometry.hpp"
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

void verifyRobotAnimation(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache,
    const std::optional<std::filesystem::path>& evidence)
{
    const auto robot = manifest.findModelIdByName("Robot");
    verify(robot.has_value(), "Robot animation fixture is missing its production model");
    verify(manifest.model(*robot).geometry == ModelGeometry::Skinned,
        "Robot animation fixture requires the production skinned model");
    const auto drive = manifest.animationIdByName("RobotDrive");
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    UiDrawData ui;
    ui.viewportSize = { 640, 360 };
    const auto makeFrame = [&](float firstPhase, float secondPhase, unsigned mask = 3) {
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.skyCloudsEnabled = false;
        frame.levelWidth = 6;
        frame.levelHeight = 4;
        frame.levelDepth = 2;
        frame.cameraOverride = RenderFrameData::CameraOverride {
            .position = { 5.7f, 8.4f, 4.4f },
            .forward = { -3.0f, -6.5f, -4.0f },
            .verticalFovDegrees = 29.0f,
        };
        frame.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 0.65f };
        frame.lighting.sun = { .direction = { -0.35f, -0.45f, 0.82f },
            .color = { 1, 1, 1 }, .intensity = 0.9f };
        frame.tiles.push_back({
            .position = { -2, -2 }, .size = { 10, 8 },
            .color = { 0.55f, 0.56f, 0.58f, 1 },
            .height = 0.0f, .showGrid = false,
        });
        for (unsigned index = 0; index < 2; ++index) {
            if (!(mask & (1U << index))) {
                continue;
            }
            frame.tiles.push_back({
                .position = { 1.0f + static_cast<float>(index) * 2.4f, 1.4f },
                .size = { 1, 1 }, .color = { 1, 1, 1, 1 },
                .baseElevation = 0.0f, .height = 1.0f,
                .showGrid = false, .model = *robot, .animation = drive,
                .animationInstanceId = 0x524F424F54000001ULL + index,
                .animationLoops = true, .animationCrossfades = false,
                .animationTimeSeconds = index == 0 ? firstPhase : secondPhase,
            });
        }
        return frame;
    };
    renderer.waitForAssets(renderAssetRequirementsForFrame(makeFrame(0, 0)));
    const auto capture = [&](float firstPhase, float secondPhase, unsigned mask = 3) {
        // Exercise both in-flight palette slots, including repeated poses.
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(
                makeFrame(firstPhase, secondPhase, mask)), ui);
            verify(!renderer.hasFatalFailure(), "Robot animation frame failed to render");
            const auto loading = renderer.assetLoadingStats();
            verify(renderer.renderStats().unavailableModels == 0,
                "Robot animation dropped a resident model or its requested pose");
            verify(loading.droppedSkinningInstances == 0 && loading.droppedDrawInstances == 0,
                "Robot animation exceeded its pose or draw capacity");
            verify(loading.failedAssets == 0 && loading.pendingModels == 0 &&
                    loading.pendingAnimations == 0,
                "Robot animation assets were not completely drained before capture");
        }
        return renderer.captureRenderedFrame();
    };
    const ImageData empty = capture(0, 0, 0);
    struct PixelBounds { uint32_t left, top, right, bottom; };
    const auto footprint = [&](const ImageData& image) {
        PixelBounds bounds { image.width, image.height, 0, 0 };
        uint32_t occupied = 0;
        for (uint32_t y = 0; y < image.height; ++y) {
            for (uint32_t x = 0; x < image.width; ++x) {
                const std::size_t pixel = (std::size_t(y) * image.width + x) * 4;
                int peak = 0;
                for (unsigned channel = 0; channel < 3; ++channel) {
                    peak = std::max(peak, std::abs(
                        std::to_integer<int>(image.rgba[pixel + channel]) -
                        std::to_integer<int>(empty.rgba[pixel + channel])));
                }
                if (peak > 6) {
                    bounds.left = std::min(bounds.left, x);
                    bounds.right = std::max(bounds.right, x + 1);
                    bounds.top = std::min(bounds.top, y);
                    bounds.bottom = std::max(bounds.bottom, y + 1);
                    ++occupied;
                }
            }
        }
        verify(occupied > 400 && bounds.right > bounds.left + 20 &&
                bounds.bottom > bounds.top + 20,
            "Robot animation fixture did not draw a recognizable robot");
        return bounds;
    };
    const PixelBounds first = footprint(capture(0, 0, 1));
    const PixelBounds second = footprint(capture(0, 0, 2));
    verify(first.right < second.left || second.right < first.left,
        "Robot animation fixture overlapped its independently posed robots");
    const auto regionDifference = [](const ImageData& before, const ImageData& after,
                                     PixelBounds bounds, bool roof) {
        const uint32_t height = bounds.bottom - bounds.top;
        return difference(before, after, bounds.left,
            roof ? bounds.top : bounds.top + height / 2,
            bounds.right, roof ? bounds.top + height / 4 : bounds.bottom);
    };
    const ImageData phaseZero = capture(0, 0);
    const ImageData firstMoving = capture(0.19f, 0);
    const ImageData bothMoving = capture(0.19f, 0.41f);
    const double firstMotion = regionDifference(phaseZero, firstMoving, first, false);
    const double secondMotion = regionDifference(firstMoving, bothMoving, second, false);
    std::cout << "RobotDrive rendered track/cog differences: first=" << firstMotion
              << ", second=" << secondMotion << '\n';
    verify(firstMotion > 0.05 && secondMotion > 0.05,
        "RobotDrive changed no visible track/cog pixels at distinct phases");
    verify(regionDifference(phaseZero, firstMoving, first, true) == 0.0 &&
            regionDifference(firstMoving, bothMoving, second, true) == 0.0,
        "RobotDrive moved the rigid roof while animating its drive mechanism");
    verify(difference(phaseZero, firstMoving, second.left, second.top,
                second.right, second.bottom) == 0.0 &&
            difference(firstMoving, bothMoving, first.left, first.top,
                first.right, first.bottom) == 0.0,
        "RobotDrive reused one instance's pose for another robot");
    const ImageData frozen = capture(0.19f, 0.41f);
    verify(difference(bothMoving, frozen, 0, 0, frozen.width, frozen.height) == 0.0,
        "RobotDrive advanced while the signed travel phase was frozen");
    const ImageData looped = capture(2.0f, 2.0f);
    verify(difference(phaseZero, looped, 0, 0, looped.width, looped.height) == 0.0,
        "RobotDrive did not loop exactly at its two-second boundary");
    if (evidence) {
        std::filesystem::create_directories(*evidence);
        saveRippleEvidence(*evidence / "phase-000.png", phaseZero);
        saveRippleEvidence(*evidence / "phase-019.png", firstMoving);
        saveRippleEvidence(*evidence / "phase-041.png", bothMoving);
        saveRippleEvidence(*evidence / "frozen.png", frozen);
        saveRippleEvidence(*evidence / "looped.png", looped);
        for (unsigned index = 0; index < 32; ++index) {
            const float phase = float(index) * 2.0f / 32.0f;
            saveRippleEvidence(*evidence /
                ("drive-" + std::to_string(1000 + index).substr(1) + ".png"),
                capture(phase, -phase));
        }
        std::cout << "Robot animation evidence: " << evidence->string() << '\n';
    }
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a robot animation error");
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

void verifyWaterStreams(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    const auto capture = [&](uint32_t flow, float time) {
        auto frame = scene({ 0.16f, 0.18f, 0.22f, 1.0f }, false);
        frame.levelWidth = 1;
        frame.levelHeight = 1;
        frame.cameraExtent = { 0, 0, 0, 1, 1, 2 };
        frame.cameraPitchDegrees = 0.0f;
        frame.skyCloudsEnabled = false;
        frame.tiles.front().size = { 1.0f, 1.0f };
        frame.waterGridBounds = { .width = 1, .height = 1 };
        frame.waterAnimationTimeSeconds = time;
        frame.waterRendering.rippleSpeed = 1.0f;
        frame.waterRendering.primaryRippleOpacity = 0.0f;
        frame.waterRendering.secondaryRippleOpacity = 0.0f;
        frame.waterRendering.reflectionStrength = 0.0f;
        frame.waterRendering.refractionStrength = 0.0f;
        frame.waterRendering.underwaterCausticStrength = 0.0f;
        frame.waterSurfaces.push_back({
            .color = frame.waterRendering.surfaceColor,
            .elevation = 1.02f,
            .streamDirection = flow,
            .streamBoundaryMask = 15,
        });
        for (int slot = 0; slot < 3; ++slot) {
            ui.beginFrame({ 640, 360 }, {}, false, false);
            ui.endFrame();
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(frame), ui.drawData(), false);
            verify(!renderer.hasFatalFailure(), "Water stream fixture failed to render");
        }
        return renderer.captureRenderedFrame();
    };
    const auto quietEarly = capture(0, 0.17f);
    const auto quietLate = capture(0, 0.40f);
    std::array<ImageData, 4> straights;
    const auto evidence = std::filesystem::current_path() / "out" / "water-stream-evidence";
    std::filesystem::create_directories(evidence);
    for (uint32_t flow = 1; flow <= 12; ++flow) {
        const auto early = capture(flow, 0.17f);
        const auto late = capture(flow, 0.40f);
        const double visible = difference(quietEarly, early, 0, 0, early.width, early.height);
        double animation = 0.0;
        for (std::size_t pixel = 0; pixel < early.rgba.size(); pixel += 4) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto sample = pixel + channel;
                const int first = std::to_integer<int>(early.rgba[sample]) -
                    std::to_integer<int>(quietEarly.rgba[sample]);
                const int second = std::to_integer<int>(late.rgba[sample]) -
                    std::to_integer<int>(quietLate.rgba[sample]);
                animation += std::abs(second - first);
            }
        }
        animation /= static_cast<double>(early.width) * early.height * 3.0;
        std::cout << "Water stream " << flow << ": contrast=" << visible
                  << ", animation=" << animation << '\n';
        verify(visible > 0.5, "Water stream foam was not clearly visible");
        verify(animation > 0.5, "Water stream foam did not visibly advance");
        if (flow <= 4) {
            straights[flow - 1] = early;
        } else {
            const uint32_t incoming = (flow - 5) % 4;
            const uint32_t exit = (incoming + (flow <= 8 ? 1U : 3U)) % 4;
            verify(difference(straights[exit], early, 0, 0, early.width, early.height) > 0.5,
                "Curved water stream rendered as a straight stream");
        }
        saveRippleEvidence(evidence / ("flow-" + std::to_string(flow) + ".png"), early);
    }
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a water stream rendering error");
}

void verifySunkenRocks(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache,
    bool shorelines = false)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    UiContext ui(font);
    PresentationSettings settings;
    settings.applyTileScales(manifest);
    constexpr Vec4 blueWater { .025f, .30f, .75f, .72f };
    constexpr Vec4 redWater { .70f, .025f, .12f, .72f };
    const auto makeFrame = [&](TileType type, bool rock, bool water, Vec4 waterColor) {
        RenderFrameData frame;
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.levelWidth = 3;
        frame.levelHeight = 3;
        frame.levelDepth = 2;
        frame.skyCloudsEnabled = false;
        frame.cameraOverride = RenderFrameData::CameraOverride {
            .position = { 4.0f, 6.0f, 4.5f },
            .forward = { -2.5f, -4.5f, -4.05f },
            .verticalFovDegrees = 30.0f,
        };
        frame.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 1.0f };
        frame.lighting.sun.intensity = 0.0f;
        frame.tiles.push_back({
            .position = { 0, 0 }, .size = { 3, 3 },
            .color = { .50f, .52f, .54f, 1 },
            .baseElevation = -.35f, .height = 0.0f, .showGrid = false,
        });
        if (rock) {
            // Use the production model, aspect-preserving normalization and
            // manifest scale. A settled water filler keeps its base one layer
            // below its logical cell; its small crown rises above the water.
            auto visual = tileVisual(type, { 1, 1, 0 }, manifest, settings);
            visual.showGrid = false;
            visual.interactionOutline = false;
            frame.tiles.push_back(visual);
        }
        if (water) {
            frame.waterSurfaces.push_back({
                .position = { 0, 0 }, .size = { 3, 3 }, .color = waterColor,
                .elevation = 1.0f - config::waterDepthBelowGround,
            });
            frame.waterGridBounds = { .width = 3, .height = 3 };
            // Fixed tint and opaque depth isolate submerged-body composition
            // from moving ripples, reflection, refraction and caustics.
            frame.waterRendering.reflectionStrength = 0.0f;
            frame.waterRendering.refractionStrength = 0.0f;
            frame.waterRendering.underwaterCausticStrength = 0.0f;
            frame.waterRendering.primaryRippleOpacity = 0.0f;
            frame.waterRendering.secondaryRippleOpacity = 0.0f;
            frame.waterRendering.primaryShorelineOpacity = 0.0f;
            frame.waterRendering.secondaryShorelineOpacity = 0.0f;
        }
        return frame;
    };
    const auto capture = [&](const RenderFrameData& frame) {
        for (int repeat = 0; repeat < 3; ++repeat) {
            ui.beginFrame({ 640, 360 }, {}, false, false);
            ui.endFrame();
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(frame), ui.drawData());
            verify(!renderer.hasFatalFailure(), "Sunken rock fixture failed to render");
            verify(renderer.renderStats().unavailableModels == 0,
                "Sunken rock fixture dropped a production rock model");
        }
        return renderer.captureRenderedFrame();
    };
    const auto evidence = std::filesystem::current_path() / "out" /
        (shorelines ? "sunken-object-shoreline-evidence" : "sunken-rock-evidence");
    std::filesystem::create_directories(evidence);
    std::vector<TileType> objectTypes { TileType::Rock, TileType::RoundRock };
    if (shorelines) objectTypes.push_back(TileType::Ice);
    for (TileType type : objectTypes) {
        const auto production = makeFrame(type, true, true, blueWater);
        verify(!manifest.modelForTile(type).isCube(),
            "Sunken rock fixture requires the production authored rock model");
        renderer.waitForAssets(renderAssetRequirementsForFrame(production));
        const auto empty = capture(makeFrame(type, false, false, blueWater));
        const auto dry = capture(makeFrame(type, true, false, blueWater));
        const auto submerged = capture(production);
        const auto alternateWater = capture(makeFrame(type, true, true, redWater));
        const std::string label = type == TileType::Rock ? "rock"
            : type == TileType::RoundRock ? "round-rock" : "ice";
        saveRippleEvidence(evidence / (label + "-dry.png"), dry);
        saveRippleEvidence(evidence / (label + "-in-water.png"), submerged);
        saveRippleEvidence(evidence / (label + "-alternate-water.png"), alternateWater);
        const auto channel = [](const ImageData& image, std::size_t pixel, std::size_t component) {
            return std::to_integer<int>(image.rgba[pixel * 4 + component]);
        };
        const auto pixelDifference = [&](const ImageData& left, const ImageData& right,
                                         std::size_t pixel) {
            double sum = 0.0;
            for (std::size_t component = 0; component < 3; ++component) {
                sum += std::abs(channel(left, pixel, component) -
                    channel(right, pixel, component));
            }
            return sum / 3.0;
        };
        const auto rockPixel = [&](std::size_t pixel) {
            return pixelDifference(empty, dry, pixel) > 12.0;
        };
        uint64_t capPixels = 0;
        uint64_t underwaterPixels = 0;
        uint64_t surroundingWaterPixels = 0;
        double underwaterTint = 0.0;
        for (uint32_t y = 1; y + 1 < dry.height; ++y) {
            for (uint32_t x = 1; x + 1 < dry.width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(y) * dry.width + x;
                const double waterChange = pixelDifference(submerged, alternateWater, pixel);
                if (!rockPixel(pixel)) {
                    if (waterChange > 12.0) ++surroundingWaterPixels;
                    continue;
                }
                // Exclude silhouette coverage and its one-pixel antialiasing
                // seam, so a cap assertion tests opaque rock interiors.
                if (!rockPixel(pixel - 1) || !rockPixel(pixel + 1) ||
                    !rockPixel(pixel - dry.width) || !rockPixel(pixel + dry.width)) continue;
                if (waterChange < 1.0 && pixelDifference(dry, submerged, pixel) < 1.0) {
                    ++capPixels;
                } else if (waterChange > 12.0 && pixelDifference(dry, submerged, pixel) > 6.0) {
                    ++underwaterPixels;
                    underwaterTint += (channel(submerged, pixel, 2) - channel(submerged, pixel, 0)) -
                        (channel(dry, pixel, 2) - channel(dry, pixel, 0));
                }
            }
        }
        const double meanTint = underwaterTint / static_cast<double>(std::max<uint64_t>(underwaterPixels, 1));
        std::cout << "Sunken " << label << ": opaque cap=" << capPixels
                  << ", submerged body=" << underwaterPixels << ", body blue tint=" << meanTint
                  << ", surrounding water=" << surroundingWaterPixels << '\n';
        if (type != TileType::Ice) {
            verify(capPixels > 20, "Sunken rock lost its opaque walkable crown above the water");
            verify(underwaterPixels > 100 && meanTint > 10.0,
                "Water did not visibly tint the submerged body of the production rock");
            verify(capPixels < (capPixels + underwaterPixels) * .75,
                "Sunken rock showed a mostly dry body instead of a small exposed crown");
        }
        verify(surroundingWaterPixels > 1000,
            "The production rock was not surrounded by visible water");
        verify(renderer.assetLoadingStats().failedAssets == 0,
            "Sunken rock fixture had an asset upload failure");
        if (!shorelines) continue;

        const auto foamFrame = [&](bool enabled, float time, bool object = true) {
            auto frame = makeFrame(type, object, true, blueWater);
            frame.waterAnimationTimeSeconds = time;
            const auto defaults = RenderFrameData::defaultWaterRendering();
            frame.waterRendering.primaryShorelineOpacity = enabled
                ? defaults.primaryShorelineOpacity : 0.0f;
            frame.waterRendering.secondaryShorelineOpacity = enabled
                ? defaults.secondaryShorelineOpacity : 0.0f;
            // No terrain-bank masks: only the real object's depth contour can
            // generate shoreline foam in this pool.
            verify(frame.waterSurfaces.front().shorelineMask == 0,
                "Sunken object foam fixture accidentally enabled a terrain bank");
            return frame;
        };
        constexpr float earlyTime = .23f;
        constexpr float lateTime = .91f;
        const auto earlyOff = capture(foamFrame(false, earlyTime));
        const auto earlyOn = capture(foamFrame(true, earlyTime));
        const auto lateOff = capture(foamFrame(false, lateTime));
        const auto lateOn = capture(foamFrame(true, lateTime));
        saveRippleEvidence(evidence / (label + "-foam-off.png"), earlyOff);
        saveRippleEvidence(evidence / (label + "-foam-early.png"), earlyOn);
        saveRippleEvidence(evidence / (label + "-foam-late.png"), lateOn);
        const auto earlyFoam = rippleImageDifference(earlyOff, earlyOn);
        const auto lateFoam = rippleImageDifference(lateOff, lateOn);

        // Dilate the authored model's silhouette by a small world-scale band
        // (about .1 tile at this camera), retaining its actual rounded contour.
        std::vector<uint8_t> nearObject(static_cast<std::size_t>(dry.width) * dry.height, 0);
        constexpr int contourPixels = 12;
        for (int y = 0; y < static_cast<int>(dry.height); ++y) {
            for (int x = 0; x < static_cast<int>(dry.width); ++x) {
                const std::size_t pixel = static_cast<std::size_t>(y) * dry.width + x;
                if (!rockPixel(pixel)) continue;
                for (int dy = -contourPixels; dy <= contourPixels; ++dy) {
                    const int targetY = y + dy;
                    if (targetY < 0 || targetY >= static_cast<int>(dry.height)) continue;
                    for (int dx = -contourPixels; dx <= contourPixels; ++dx) {
                        const int targetX = x + dx;
                        if (targetX < 0 || targetX >= static_cast<int>(dry.width) ||
                            dx * dx + dy * dy > contourPixels * contourPixels) continue;
                        nearObject[static_cast<std::size_t>(targetY) * dry.width + targetX] = 1;
                    }
                }
            }
        }
        double nearContourWeight = 0.0;
        double animationWeight = 0.0;
        double positiveFoam = 0.0;
        double capChange = 0.0;
        uint64_t capInteriorPixels = 0;
        std::vector<uint8_t> dryCap(nearObject.size(), 0);
        if (type != TileType::Ice) {
            for (std::size_t pixel = 0; pixel < dryCap.size(); ++pixel) {
                // Exact baseline agreement rejects water-covered seam pixels
                // whose small coverage happened to differ by less than 1 RGB.
                dryCap[pixel] = rockPixel(pixel) &&
                    pixelDifference(dry, submerged, pixel) == 0.0 &&
                    pixelDifference(submerged, alternateWater, pixel) == 0.0;
            }
        }
        for (std::size_t pixel = 0; pixel < nearObject.size(); ++pixel) {
            const double earlyChange = pixelDifference(earlyOff, earlyOn, pixel);
            if (nearObject[pixel]) nearContourWeight += earlyChange;
            double signedEarlyChange = 0.0;
            for (std::size_t component = 0; component < 3; ++component) {
                const int earlyDelta = channel(earlyOn, pixel, component) -
                    channel(earlyOff, pixel, component);
                const int lateDelta = channel(lateOn, pixel, component) -
                    channel(lateOff, pixel, component);
                animationWeight += std::abs(earlyDelta - lateDelta) / 3.0;
                signedEarlyChange += earlyDelta / 3.0;
            }
            positiveFoam += std::max(signedEarlyChange, 0.0);
            // Erode two pixels in every direction, including the diagonals,
            // so the assertion measures opaque crown interiors, not the
            // silhouette/waterline seam or its filtered neighboring samples.
            const int x = static_cast<int>(pixel % dry.width);
            const int y = static_cast<int>(pixel / dry.width);
            bool capInterior = dryCap[pixel] && x >= 2 && y >= 2 &&
                x + 2 < static_cast<int>(dry.width) && y + 2 < static_cast<int>(dry.height);
            for (int dy = -2; capInterior && dy <= 2; ++dy) {
                for (int dx = -2; capInterior && dx <= 2; ++dx) {
                    const auto neighbor = static_cast<std::size_t>(y + dy) * dry.width + (x + dx);
                    capInterior = dryCap[neighbor] != 0;
                }
            }
            if (capInterior) {
                ++capInteriorPixels;
                capChange = std::max(capChange, std::max(earlyChange,
                    pixelDifference(lateOff, lateOn, pixel)));
            }
        }
        const double contourShare = nearContourWeight / std::max(earlyFoam.weight, 1.0);
        std::cout << "Sunken " << label << " shoreline: early=" << earlyFoam.weight
                  << ", late=" << lateFoam.weight << ", peak=" << earlyFoam.peak
                  << ", contour share=" << contourShare << ", animation=" << animationWeight
                  << ", exposed cap interior=" << capInteriorPixels
                  << ", exposed cap change=" << capChange << '\n';
        verify(earlyFoam.weight > 150.0 && earlyFoam.peak > 5.0 && positiveFoam > 100.0,
            "Shoreline foam was not visibly crashing around the sunken object's waterline");
        verify(contourShare > .90,
            "Sunken object shoreline foam spread beyond the object's actual contour");
        verify(animationWeight > 50.0 && animationWeight > earlyFoam.weight * .02,
            "Sunken object shoreline foam did not visibly animate");
        if (type != TileType::Ice) {
            verify(capInteriorPixels > 20,
                "Sunken object shoreline fixture did not retain a measurable opaque crown interior");
            verify(capChange < 1.0,
                "Sunken object shoreline foam repainted the opaque crown above the water");
            auto belowSurfaceOff = foamFrame(false, earlyTime);
            auto belowSurfaceOn = foamFrame(true, earlyTime);
            belowSurfaceOff.tiles.back().baseElevation -= .30f;
            belowSurfaceOn.tiles.back().baseElevation -= .30f;
            const auto fullySubmergedOff = capture(belowSurfaceOff);
            const auto fullySubmergedOn = capture(belowSurfaceOn);
            saveRippleEvidence(evidence / (label + "-fully-submerged-foam-off.png"), fullySubmergedOff);
            saveRippleEvidence(evidence / (label + "-fully-submerged-foam-enabled.png"), fullySubmergedOn);
            const auto falseContact = rippleImageDifference(fullySubmergedOff, fullySubmergedOn);
            std::cout << "Fully submerged " << label << ": false shoreline=" << falseContact.weight << '\n';
            verify(falseContact.weight == 0.0,
                "A wholly submerged rock incorrectly generated shoreline contact foam");
        }

        if (type == TileType::Rock) {
            const auto deepOff = capture(foamFrame(false, earlyTime, false));
            const auto deepOn = capture(foamFrame(true, earlyTime, false));
            saveRippleEvidence(evidence / "deep-flat-pool-foam-off.png", deepOff);
            saveRippleEvidence(evidence / "deep-flat-pool-foam-enabled.png", deepOn);
            verify(rippleImageDifference(deepOff, deepOn).weight == 0.0,
                "A deep flat pool floor incorrectly generated object shoreline foam");
            auto emptyOffFrame = foamFrame(false, earlyTime, false);
            auto emptyOnFrame = foamFrame(true, earlyTime, false);
            emptyOffFrame.tiles.clear();
            emptyOnFrame.tiles.clear();
            const auto emptyOff = capture(emptyOffFrame);
            const auto emptyOn = capture(emptyOnFrame);
            saveRippleEvidence(evidence / "empty-water-foam-enabled.png", emptyOn);
            verify(rippleImageDifference(emptyOff, emptyOn).weight == 0.0,
                "Empty water incorrectly generated object shoreline foam");
        }
    }
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a sunken rock rendering error");
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
    const auto skipFrame = [&](float age) {
        auto frame = makeFrame(age);
        frame.waterRipples[0].radiusScale = 0.35f;
        frame.waterRipples[0].intensity = 0.75f;
        return frame;
    };
    const auto skipEarly = capture(skipFrame(0.16f));
    const auto skipExpanded = capture(skipFrame(0.55f));
    const auto skipFading = capture(skipFrame(1.35f));
    const auto skipEarlyDifference = rippleImageDifference(baseline, skipEarly);
    const auto skipExpandedDifference = rippleImageDifference(baseline, skipExpanded);
    const auto skipFadingDifference = rippleImageDifference(baseline, skipFading);
    saveRippleEvidence(evidence / "skip-early.png", skipEarly);
    saveRippleEvidence(evidence / "skip-expanded.png", skipExpanded);
    saveRippleEvidence(evidence / "skip-fading.png", skipFading);
    verify(skipEarlyDifference.weight > 20.0 && skipEarlyDifference.peak > 3.0,
        "Round rock skip ring was not visible on the water");
    verify(skipExpandedDifference.radiusPixels > skipEarlyDifference.radiusPixels * 1.4 &&
        skipExpandedDifference.radiusPixels < expandedDifference.radiusPixels * 0.65,
        "Round rock skip ring did not stay small while expanding");
    verify(skipFadingDifference.peak < skipExpandedDifference.peak * 0.6,
        "Round rock skip ring did not lose intensity with age");
    // Capture the new asset through the same lighting and crop as the editor
    // palette. This also exercises its real GPU mesh/material upload.
    const auto rockFrame = tileThumbnails::buildBakeFrame(
        TileType::RoundRock, manifest, PresentationSettings {});
    renderer.waitForAssets(renderAssetRequirementsForFrame(rockFrame));
    const auto rockImage = capture(rockFrame);
    const auto crop = tileThumbnails::cropFor(rockFrame, rockImage.width, rockImage.height);
    std::vector<uint8_t> cropped(static_cast<std::size_t>(crop.width) * crop.height * 4);
    for (uint32_t y = 0; y < crop.height; ++y) {
        for (uint32_t x = 0; x < crop.width; ++x) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const std::size_t source = ((static_cast<std::size_t>(crop.y) + y) *
                    rockImage.width + static_cast<std::size_t>(crop.x) + x) * 4 + channel;
                cropped[(static_cast<std::size_t>(y) * crop.width + x) * 4 + channel] =
                    std::to_integer<uint8_t>(rockImage.rgba[source]);
            }
        }
    }
    writeRgbaPng(evidence / "tile_round_rock.png", crop.width, crop.height, cropped);
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

    // Use a partially entering block so its leading-edge ripple remains visible
    // in a one-tile pool. The fixture samples .044 s after first water contact;
    // waiting for the completed step would hide the effect beneath the block.
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

void verifyFourLayerSplat(SDL_Window* window, const std::filesystem::path& root,
    FontAtlas& font, const AssetManifest& manifest, const std::filesystem::path& cache)
{
    VulkanRenderer renderer(window, root, cache, manifest, font,
        AntiAliasingMode::None, 100, { .vsync = false }, {}, false, true, true, false);
    GroundSplatTextures textures {
        .base = manifest.textureIdByName("GroundGrass"),
        .detail = manifest.textureIdByName("GroundRock"),
        .splatMap = manifest.textureIdByName(groundSplatMapTextureName),
        .texture2 = manifest.textureIdByName("GroundAutumnGrass"),
        .texture3 = manifest.textureIdByName("GroundAutumnRock"),
    };
    const std::array<RenderTexture, 4> albedos {
        textures.base, textures.detail, textures.texture2, textures.texture3,
    };
    RenderFrameData floor;
    floor.viewMode = RenderViewMode::Isometric3D;
    floor.levelWidth = 8;
    floor.levelHeight = 8;
    floor.levelDepth = 2;
    floor.skyCloudsEnabled = false;
    floor.lighting.sun.intensity = 0;
    floor.lighting.ambient = { .color = { 1, 1, 1 }, .intensity = 1 };
    floor.cameraOverride = RenderFrameData::CameraOverride {
        .position = { 4, 4, 4 }, .forward = { 0, 0, -1 },
    };
    floor.groundSplat = textures;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            floor.tiles.push_back({
                .cell = { x, y, 0 },
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .color = { 1, 1, 1, 1 }, .height = 1, .showGrid = false,
                .effect = RenderSurfaceEffect::GroundSplat, .groundTop = true,
            });
        }
    }
    renderer.waitForAssets(renderAssetRequirementsForFrame(floor));
    // waitForAssets permits submitted texture uploads. Repainting requires
    // published Ready slots, so complete uploads and let subsequent frames
    // retire their fences before replacing the fixture's texture images.
    UiDrawData ui;
    ui.viewportSize = { 640, 360 };
    for (int frame = 0; frame < 3; ++frame) {
        renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
        ImGui::Render();
#endif
        renderer.drawFrame(renderer.prepareFrame(floor), ui);
        renderer.waitIdle();
    }
    const auto solidImage = [](uint32_t size, std::array<uint8_t, 4> value) {
        ImageData image;
        image.width = image.height = size;
        image.rgba.resize(static_cast<std::size_t>(size) * size * 4);
        for (std::size_t pixel = 0; pixel < image.rgba.size(); pixel += 4) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                image.rgba[pixel + channel] = static_cast<std::byte>(value[channel]);
            }
        }
        return image;
    };
    // These updates affect only this fixture's GPU resources. Distinct solid
    // colors make the channel mapping independent of shipped artwork changes.
    constexpr std::array<std::array<uint8_t, 4>, 4> colors {{
        { 230, 20, 20, 255 }, { 20, 230, 20, 255 },
        { 20, 20, 230, 255 }, { 230, 230, 20, 255 },
    }};
    for (std::size_t layer = 0; layer < albedos.size(); ++layer) {
        if (!renderer.updateTexture(albedos[layer], solidImage(4, colors[layer]))) {
            throw std::runtime_error("Four-layer fixture could not upload material " +
                std::to_string(layer));
        }
    }
    const auto updateMask = [&](std::array<uint8_t, 4> weights) {
        verify(renderer.updateTexture(textures.splatMap, solidImage(8 * 32, weights)),
            "Four-layer fixture could not upload RGBA weights");
    };
    const auto capture = [&](const GroundSplatTextures& assignment) {
        for (int frame = 0; frame < 3; ++frame) {
            floor.groundSplat = assignment;
            renderer.beginDebugUiFrame();
#if SOKOBAN_ENABLE_DEBUG_UI
            ImGui::Render();
#endif
            renderer.drawFrame(renderer.prepareFrame(floor), ui);
            verify(!renderer.hasFatalFailure(), "Four-layer fixture failed to render");
        }
        return renderer.captureRenderedFrame();
    };
    const auto centerDifference = [&](const ImageData& left, const ImageData& right) {
        return difference(left, right, left.width * 2 / 5, left.height * 2 / 5,
            left.width * 3 / 5, left.height * 3 / 5);
    };
    const std::filesystem::path evidence = std::filesystem::path(SOKOBAN_TEST_SOURCE_DIR) /
        "out" / "four-layer-splat";
    std::filesystem::create_directories(evidence);
    std::array<ImageData, 4> pure;
    constexpr std::array<const char*, 4> layerNames { "red", "green", "blue", "alpha" };
    for (std::size_t layer = 0; layer < albedos.size(); ++layer) {
        std::array<uint8_t, 4> weights {};
        weights[layer] = 255;
        updateMask(weights);
        pure[layer] = capture(textures);
        saveRippleEvidence(evidence / (std::string(layerNames[layer]) + ".png"), pure[layer]);
        // A pure layer must match opaque material 0 using that same albedo.
        // The alpha case fails if mask alpha controls surface transparency.
        GroundSplatTextures reference = textures;
        reference.base = albedos[layer];
        updateMask({ 255, 0, 0, 0 });
        const double error = centerDifference(pure[layer], capture(reference));
        std::cout << "Splat " << layerNames[layer] << " reference error=" << error << '\n';
        verify(error < 0.1, "Pure RGBA channel did not select its assigned opaque material");
    }
    for (std::size_t layer = 1; layer < pure.size(); ++layer) {
        verify(centerDifference(pure[0], pure[layer]) > 20,
            "RGBA layer selection did not visibly change the material");
    }

    constexpr std::array<uint8_t, 4> mixture { 51, 51, 51, 102 };
    updateMask(mixture);
    const ImageData mixed = capture(textures);
    saveRippleEvidence(evidence / "mixed.png", mixed);
    updateMask({ 102, 102, 102, 204 });
    verify(centerDifference(mixed, capture(textures)) < 0.1,
        "RGBA splat weights were not normalized before blending");
    updateMask({ 0, 0, 0, 0 });
    verify(centerDifference(pure[0], capture(textures)) < 0.1,
        "An empty RGBA weight texel did not fall back to material 0");
    std::array<uint8_t, 4> expected { 0, 0, 0, 255 };
    for (std::size_t channel = 0; channel < 3; ++channel) {
        double linear = 0;
        for (std::size_t layer = 0; layer < colors.size(); ++layer) {
            const double srgb = colors[layer][channel] / 255.0;
            const double decoded = srgb <= 0.04045 ? srgb / 12.92 :
                std::pow((srgb + 0.055) / 1.055, 2.4);
            linear += decoded * mixture[layer] / 255.0;
        }
        const double encoded = linear <= 0.0031308 ? linear * 12.92 :
            1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        expected[channel] = static_cast<uint8_t>(std::lround(encoded * 255));
    }
    verify(renderer.updateTexture(textures.base, solidImage(4, expected)),
        "Four-layer fixture could not upload its expected blend");
    updateMask({ 255, 0, 0, 0 });
    const ImageData expectedBlend = capture(textures);
    saveRippleEvidence(evidence / "mixed-reference.png", expectedBlend);
    const double mixedError = centerDifference(mixed, expectedBlend);
    std::cout << "Splat mixed reference error=" << mixedError << '\n';
    verify(mixedError < 1.0, "RGBA material mixture disagreed with its linear-color blend");
    verify(vulkanDebug::validationErrorCount() == 0,
        "Vulkan validation reported a four-layer splat rendering error");
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
        if (argc > 1 && std::string_view(argv[1]) == "--robot-animation") {
            verifyRobotAnimation(window.get(), root, font, manifest,
                temp.path() / "cache.bin", argc > 2
                    ? std::optional<std::filesystem::path>(argv[2]) : std::nullopt);
            std::cout << "Robot animation GPU regression passed\n";
            return 0;
        }
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
        if (argc > 1 && std::string_view(argv[1]) == "--sunken-rocks") {
            verifySunkenRocks(window.get(), root, font, manifest, temp.path() / "cache.bin");
            std::cout << "Sunken rock GPU regression passed\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--sunken-object-shorelines") {
            verifySunkenRocks(window.get(), root, font, manifest, temp.path() / "cache.bin", true);
            std::cout << "Sunken object shoreline GPU regression passed\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--water-streams") {
            verifyWaterStreams(window.get(), root, font, manifest, temp.path() / "cache.bin");
            std::cout << "Water stream GPU regression passed\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--rock-scratches") {
            verifyRockScratches(window.get(), root, font, manifest, temp.path() / "cache.bin");
            std::cout << "Rock scratch GPU regression passed\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--four-layer-splat") {
            verifyFourLayerSplat(window.get(), root, font, manifest, temp.path() / "cache.bin");
            std::cout << "Four-layer splat GPU regression passed\n";
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
