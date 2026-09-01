#include "engine/Application.hpp"

#include "engine/Log.hpp"
#include "engine/render/PngWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {
namespace {

void writeCapture(
    const std::filesystem::path& path,
    const ImageData& image)
{
    std::vector<uint8_t> pixels(image.rgba.size());
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = static_cast<uint8_t>(image.rgba[index]);
    }
    writeRgbaPng(path, image.width, image.height, pixels);
}

std::string evidenceSuffix(
    uint32_t renderScalePercent,
    uint32_t activeSamples,
    bool waterEnabled,
    bool ambientOcclusionEnabled)
{
    std::string suffix = "scale-" + std::to_string(renderScalePercent) +
        "-msaa-" + std::to_string(activeSamples);
    if (waterEnabled) {
        suffix += "-water";
    }
    if (!ambientOcclusionEnabled) {
        suffix += "-ao-off";
    }
    return suffix;
}

} // namespace

void Application::captureEvidenceScene()
{
    std::error_code error;
    std::filesystem::create_directories(evidenceOutputDirectory_, error);
    if (error) {
        throw std::runtime_error(
            "Could not create evidence directory '" +
            evidenceOutputDirectory_.string() + "': " + error.message());
    }

    evidenceStats_ = renderer_.renderStats();
    if (evidenceWaterEnabled_ &&
        !evidenceStats_.mainSceneHasTranslucency) {
        throw std::runtime_error(
            "Evidence water fixture did not reach the translucent scene path");
    }
    if (evidenceWaterEnabled_ && evidenceAmbientOcclusionEnabled_ &&
        !evidenceStats_.ssaoColorSnapshotCopied) {
        throw std::runtime_error(
            "Evidence water fixture did not exercise the SSAO color-copy fallback");
    }
    const std::string suffix = evidenceSuffix(
        evidenceStats_.renderScalePercent,
        evidenceStats_.activeSamples,
        evidenceWaterEnabled_,
        evidenceAmbientOcclusionEnabled_);
    writeCapture(
        evidenceOutputDirectory_ / ("scene-" + suffix + ".png"),
        renderer_.captureRenderedFrame());
    evidenceSceneCaptured_ = true;

    // The next and final smoke frame keeps every scene input fixed and changes
    // only the composite's output selector.
    if (evidenceAmbientOcclusionEnabled_) {
        presentationSettings_.lighting.ambientOcclusionDebug =
            RenderFrameData::Lighting::AmbientOcclusion::Debug::Occlusion;
    }
}

void Application::finishEvidenceCapture()
{
    if (!evidenceSceneCaptured_) {
        throw std::logic_error(
            "Evidence capture reached its final frame without a scene image");
    }
    const std::string suffix = evidenceSuffix(
        evidenceStats_.renderScalePercent,
        evidenceStats_.activeSamples,
        evidenceWaterEnabled_,
        evidenceAmbientOcclusionEnabled_);
    const std::string sceneName = "scene-" + suffix + ".png";
    std::string occlusionName;
    if (evidenceAmbientOcclusionEnabled_) {
        occlusionName = "occlusion-" + suffix + ".png";
        writeCapture(
            evidenceOutputDirectory_ / occlusionName,
            renderer_.captureRenderedFrame());
    }

    const std::filesystem::path reportPath =
        evidenceOutputDirectory_ / ("metrics-" + suffix + ".md");
    std::ofstream report(reportPath, std::ios::trunc);
    if (!report) {
        throw std::runtime_error(
            "Could not write evidence report '" + reportPath.string() + "'");
    }
    report << std::fixed << std::setprecision(3);
    report << "# Render evidence — "
           << evidenceStats_.renderScalePercent << "% scale, "
           << evidenceStats_.activeSamples << "x MSAA\n\n";
    report << "- Device: " << renderer_.physicalDeviceName()
           << " (" << renderer_.physicalDeviceTypeName() << ")\n";
    report << "- Swapchain: " << evidenceStats_.swapchainWidth << 'x'
           << evidenceStats_.swapchainHeight << "\n";
    report << "- Scene target: " << evidenceStats_.renderWidth << 'x'
           << evidenceStats_.renderHeight << "\n";
    report << "- SSAO target: " << evidenceStats_.ssaoWidth << 'x'
           << evidenceStats_.ssaoHeight << "\n";
    report << "- Ambient occlusion: "
           << (evidenceAmbientOcclusionEnabled_ ? "enabled" : "disabled")
           << "\n";
    report << "- Evidence water fixture: "
           << (evidenceWaterEnabled_ ? "enabled" : "disabled") << "\n";
    report << "- Main-scene translucency: "
           << (evidenceStats_.mainSceneHasTranslucency ? "present" : "absent")
           << "\n";
    report << "- SSAO color-copy fallback: "
           << (evidenceStats_.ssaoColorSnapshotCopied ? "exercised" : "skipped")
           << "\n";
    report << "- MSAA samples: " << evidenceStats_.activeSamples << "\n";
    report << "- Draw calls: " << evidenceStats_.drawCalls << "\n";
    report << "- Triangles: " << evidenceStats_.triangles << "\n";
    report << "- Render passes: " << evidenceStats_.renderPasses << "\n";
    report << "- Point-shadow faces: "
           << evidenceStats_.pointShadowFacesInRange << " / "
           << evidenceStats_.pointShadowFaceCandidates << " in range; "
           << evidenceStats_.pointShadowFacesCulled << " culled ("
           << evidenceStats_.pointShadowFacesCulled * 6U
           << " face draws avoided)\n";
    report << "- Point-shadow cube faces: "
           << evidenceStats_.pointShadowCubeFacesRendered << " rendered, "
           << evidenceStats_.pointShadowCubeFacesReused << " reused\n";
    report << "- Point-shadow models: "
           << evidenceStats_.pointShadowModelsInRange << " / "
           << evidenceStats_.pointShadowModelCandidates << " in range; "
           << evidenceStats_.pointShadowModelsCulled << " culled\n";
    report << "- Persistent renderables: "
           << evidenceStats_.persistentRenderables << " (visible "
           << evidenceStats_.visibleRenderables << ", culled "
           << evidenceStats_.culledRenderables << "; bounds reused "
           << evidenceStats_.reusedRenderableBounds << ", rebuilt "
           << evidenceStats_.rebuiltRenderableBounds << ")\n";
    report << "- Main-scene frustum culling: "
           << (evidenceStats_.frustumCullingEnabled ? "enabled" : "disabled")
           << "\n";
    report << "- Parallel scene preparation: "
           << (evidenceStats_.parallelScenePreparationEnabled
                   ? "enabled"
                   : "disabled")
           << "\n";
    report << "- Point-shadow range/cache optimizations: "
           << (evidenceStats_.pointShadowOptimizationsEnabled
                   ? "enabled"
                   : "disabled")
           << "\n";
    report << "- Recorder scratch reuse: "
           << (evidenceStats_.recorderScratchReuseEnabled
                   ? "enabled"
                   : "disabled")
           << "; " << evidenceStats_.recorderScratchGrowths
           << " capacity growths this frame; "
           << evidenceStats_.recorderScratchCapacityBytes
           << " bytes of model-recording capacity\n";
    const auto writePhase = [&report](
                                std::string_view label,
                                const RenderPhaseTiming& timing) {
        report << "- " << label << ": ";
        if (!timing.available) {
            report << "unavailable\n";
            return;
        }
        report << "average " << timing.averageMilliseconds
               << " ms, p95 " << timing.p95Milliseconds
               << " ms, maximum " << timing.maximumMilliseconds
               << " ms (" << timing.samples << " samples)\n";
    };
    writePhase("Asset scheduling", evidenceStats_.assetSchedulingTiming);
    writePhase("Frame-fence wait", evidenceStats_.frameFenceWaitTiming);
    writePhase("Asset maintenance", evidenceStats_.assetMaintenanceTiming);
    writePhase("Image acquisition", evidenceStats_.imageAcquisitionTiming);
    writePhase("Command recording", evidenceStats_.commandRecordingTiming);
    writePhase("Submit/present", evidenceStats_.submitPresentTiming);
    writePhase("  Recorder setup", evidenceStats_.recorderSetupTiming);
    writePhase(
        "  Game command recording",
        evidenceStats_.gameCommandRecordingTiming);
    writePhase(
        "    Shadow command recording",
        evidenceStats_.shadowCommandRecordingTiming);
    writePhase(
        "    Scene command recording",
        evidenceStats_.sceneCommandRecordingTiming);
    writePhase(
        "  SSAO command recording",
        evidenceStats_.ssaoCommandRecordingTiming);
    writePhase(
        "  Preview command recording",
        evidenceStats_.previewCommandRecordingTiming);
    writePhase(
        "  Output/UI command recording",
        evidenceStats_.outputCommandRecordingTiming);
    writePhase(
        "Asset publication events",
        evidenceStats_.assetPublicationEventTiming);
    writePhase("GPU shadows", evidenceStats_.gpuShadowTiming);
    writePhase("GPU scene color/depth", evidenceStats_.gpuSceneTiming);
    writePhase("  GPU scene raster/resolve", evidenceStats_.gpuSceneRasterTiming);
    writePhase(
        "  GPU scene depth publish", evidenceStats_.gpuSceneDepthPublishTiming);
    writePhase("  GPU scene translucency", evidenceStats_.gpuSceneTranslucencyTiming);
    writePhase("GPU SSAO", evidenceStats_.gpuSsaoTiming);
    writePhase("  GPU SSAO scene snapshot", evidenceStats_.gpuSsaoSnapshotTiming);
    writePhase("  GPU SSAO occlusion", evidenceStats_.gpuSsaoOcclusionTiming);
    writePhase("  GPU SSAO composite", evidenceStats_.gpuSsaoCompositeTiming);
    writePhase("GPU output/UI", evidenceStats_.gpuOutputTiming);
    report << "- Asset publications: " << evidenceStats_.assetPublications
           << " across " << evidenceStats_.assetPublicationFrames
           << " frames\n";
    report << "- Texture uploads: "
           << evidenceStats_.textureUploadSubmissions << " submitted, "
           << evidenceStats_.textureUploadCompletions << " completed, "
           << evidenceStats_.textureUploadsInFlight << " in flight\n";
    // Character-identical to the block this replaces, which is why it can use
    // the shared writer. The CPU and GPU lines below cannot: the CPU line has
    // no unavailable branch at all, and the GPU line's says why timestamps are
    // missing. Those differences are deliberate, so they stay written out.
    writePhase("Scene preparation", evidenceStats_.scenePreparationTiming);
    report << "- CPU frame: average "
           << evidenceStats_.cpuFrameTiming.averageMilliseconds << " ms, p95 "
           << evidenceStats_.cpuFrameTiming.p95Milliseconds << " ms, maximum "
           << evidenceStats_.cpuFrameTiming.maximumMilliseconds << " ms ("
           << evidenceStats_.cpuFrameTiming.samples << " samples)\n";
    if (evidenceStats_.gpuFrameTiming.available) {
        report << "- GPU frame: average "
               << evidenceStats_.gpuFrameTiming.averageMilliseconds << " ms, p95 "
               << evidenceStats_.gpuFrameTiming.p95Milliseconds << " ms, maximum "
               << evidenceStats_.gpuFrameTiming.maximumMilliseconds << " ms ("
               << evidenceStats_.gpuFrameTiming.samples << " samples)\n";
    } else {
        report << "- GPU frame: unavailable (timestamp support: "
               << (evidenceStats_.gpuTimestampsSupported ? "yes" : "no")
               << ")\n";
    }
    report << "- Scene image: `" << sceneName << "`\n";
    if (evidenceAmbientOcclusionEnabled_) {
        report << "- Filtered SSAO image: `" << occlusionName << "`\n";
        report << "\nThe simulation was frozen for the run. The two images "
                  "differ only by the SSAO composite debug selector.\n";
    } else {
        report << "\nThe simulation was frozen for the run. Ambient occlusion "
                  "was disabled for the complete timing window.\n";
    }
    if (!report) {
        throw std::runtime_error(
            "Could not finish evidence report '" + reportPath.string() + "'");
    }
    log::info(log::Category::Rendering)
        << "Archived render evidence at "
        << evidenceOutputDirectory_.string();
}

} // namespace sokoban
