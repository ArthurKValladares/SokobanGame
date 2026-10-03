#include "engine/Application.hpp"

#include "engine/Log.hpp"
#include "engine/PerformanceAnalysis.hpp"
#include "engine/Profiler.hpp"
#include "engine/PerformanceFixtures.hpp"
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

void Application::appendEvidenceEffects(RenderFrameData& frame)
{
    if (evidenceEffects_.empty()) {
        return;
    }
    if (!evidenceEffectSnapshot_) {
        evidenceEffectSnapshot_.emplace();
        evidenceEffectSnapshot_->levelWidth = frame.levelWidth;
        evidenceEffectSnapshot_->levelHeight = frame.levelHeight;
        PerformanceEffectFixture fixture(assetManifest_);
        fixture.append(*evidenceEffectSnapshot_, assetManifest_, evidenceEffects_,
            evidenceEffects_ == "mixed-stress" ? 32U : 8U);
        if (evidenceEffectSnapshot_->particles.empty()) {
            throw std::runtime_error("Evidence effect fixture emitted no particles");
        }
    }
    for (const auto& particle : evidenceEffectSnapshot_->particles) {
        if (!frame.particles.push_back(particle)) {
            throw std::runtime_error("Evidence particles exceeded frame capacity");
        }
    }
    for (const auto& tile : evidenceEffectSnapshot_->tiles) {
        if (!frame.tiles.push_back(tile)) {
            throw std::runtime_error("Evidence effects exceeded tile capacity");
        }
    }
}

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
    if (evidenceEffectSnapshot_ &&
        evidenceStats_.preparedParticles < evidenceEffectSnapshot_->particles.size()) {
        throw std::runtime_error("Evidence particle fixture did not reach scene preparation");
    }
    if (evidenceEffectSnapshot_ && evidenceStats_.particleDrawCalls == 0) {
        throw std::runtime_error("Evidence particles were prepared but not drawn");
    }
    if ((evidenceEffects_ == "special-blocks" || evidenceEffects_ == "mixed-stress") &&
        (evidenceStats_.preparedEnergyFaces == 0 || evidenceStats_.preparedEnergyModels == 0 ||
            evidenceStats_.preparedBlurModels == 0 || evidenceStats_.unavailableModels != 0)) {
        throw std::runtime_error("Evidence special shaders did not reach the loaded scene path");
    }
    // A three-frame image capture takes its normal image before the first
    // in-flight frame is reused and its queries collected. Performance runs
    // must collect timestamps, while the existing minimal image path stays valid.
    if (smokeFrames_ > 3 && evidenceStats_.gpuTimestampsSupported &&
        !evidenceStats_.gpuFrameTiming.available) {
        throw std::runtime_error("Evidence GPU timestamps were not collected");
    }
    if (evidenceWaterEnabled_ &&
        !evidenceStats_.mainSceneHasTranslucency) {
        throw std::runtime_error(
            "Evidence water fixture did not reach the translucent scene path");
    }
    if (evidenceWaterEnabled_ && evidenceAmbientOcclusionEnabled_ &&
        !evidenceStats_.ssaoColorSnapshotCopied) {
        throw std::runtime_error(
            "Evidence water fixture did not exercise the SSAO color snapshot");
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
    report << "- Evidence location: ";
    if (evidenceLevel_ >= 0) {
        report << "level " << evidenceLevel_ << ", screen " << evidenceScreen_;
    } else {
        report << "overworld";
    }
    report << "\n- Developer workspace: "
           << (evidenceDebugUi_ ? "visible (Level Editor)" : "hidden")
           << "\n- CPU profiler: "
           << (CpuProfiler::instance().enabled() ? "enabled" : "disabled") << "\n";
    report << "- Effect fixture: " << (evidenceEffects_.empty() ? "none" : evidenceEffects_)
           << "; " << (evidenceEffectSnapshot_ ? evidenceEffectSnapshot_->particles.size() : 0)
           << " retained particles\n";
    report << "- Authored water: " << (evidenceWaterDisabled_ ? "disabled" : "enabled")
           << "\n- Water reflections: "
           << (evidenceWaterReflectionsDisabled_ ? "disabled" : "enabled") << "\n";
    report << "- Swapchain: " << evidenceStats_.swapchainWidth << 'x'
           << evidenceStats_.swapchainHeight << "\n";
    report << "- Present mode: " << renderer_.presentModeName() << "\n";
    report << "- Scene target: " << evidenceStats_.renderWidth << 'x'
           << evidenceStats_.renderHeight << "\n";
    report << "- SSAO target: " << evidenceStats_.ssaoWidth << 'x'
           << evidenceStats_.ssaoHeight << "\n";
    report << "- Atmosphere target: " << evidenceStats_.atmosphereWidth << 'x'
           << evidenceStats_.atmosphereHeight << "\n";
    if (evidenceStats_.atmosphereUnscissoredPixels != 0) {
        const double coverage = 100.0 *
            static_cast<double>(evidenceStats_.atmosphereCompositePixels) /
            static_cast<double>(evidenceStats_.atmosphereUnscissoredPixels);
        report << "- Atmosphere coverage: "
               << evidenceStats_.atmosphereMediaCount << " media, "
               << evidenceStats_.atmosphereCompositePixels << " / "
               << evidenceStats_.atmosphereUnscissoredPixels
               << " full-resolution pixels (" << std::fixed
               << std::setprecision(1) << coverage << "% after scissoring)"
               << std::setprecision(3) << "\n";
    }
    report << "- Ambient occlusion: "
           << (evidenceAmbientOcclusionEnabled_ ? "enabled" : "disabled")
           << "\n";
    report << "- Evidence water fixture: "
           << (evidenceWaterEnabled_ ? "enabled" : "disabled") << "\n";
    report << "- Main-scene translucency: "
           << (evidenceStats_.mainSceneHasTranslucency ? "present" : "absent")
           << "\n";
    report << "- SSAO color snapshot: "
           << (evidenceStats_.ssaoColorSnapshotCopied ? "exercised" : "skipped")
           << "\n";
    report << "- MSAA samples: " << evidenceStats_.activeSamples << "\n";
    report << "- Scene depth: " << evidenceStats_.sceneDepthBits
           << " bits\n";
    report << "- Draw calls: " << evidenceStats_.drawCalls << "\n";
    report << "- Triangles: " << evidenceStats_.triangles << "\n";
    report << "- Prepared particles: " << evidenceStats_.preparedParticles << "\n";
    report << "- Particle draw calls: " << evidenceStats_.particleDrawCalls << "\n";
    report << "- Special-surface coverage: " << evidenceStats_.preparedWaterFaces
           << " water faces, " << evidenceStats_.preparedEnergyFaces << " energy faces, "
           << evidenceStats_.preparedEnergyModels << " energy models, "
           << evidenceStats_.preparedBlurModels << " blurred ice models\n";
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
    report << "- Point-shadow quad submissions: "
           << evidenceStats_.pointShadowQuadDrawCalls << " draws for "
           << evidenceStats_.pointShadowQuadInstances << " instances\n";
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
        "  Atmosphere command recording",
        evidenceStats_.atmosphereCommandRecordingTiming);
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
    writePhase("    GPU scene surfaces", evidenceStats_.gpuSceneFacesTiming);
    writePhase("    GPU scene models", evidenceStats_.gpuSceneModelsTiming);
    writePhase(
        "  GPU scene depth publish", evidenceStats_.gpuSceneDepthPublishTiming);
    writePhase("  GPU scene translucency", evidenceStats_.gpuSceneTranslucencyTiming);
    writePhase("    GPU particles", evidenceStats_.gpuParticleTiming);
    writePhase(
        "  GPU mirror continuation",
        evidenceStats_.gpuSceneMirrorContinuationTiming);
    writePhase("GPU SSAO", evidenceStats_.gpuSsaoTiming);
    writePhase("  GPU SSAO scene snapshot", evidenceStats_.gpuSsaoSnapshotTiming);
    writePhase("  GPU SSAO occlusion", evidenceStats_.gpuSsaoOcclusionTiming);
    writePhase("  GPU SSAO composite", evidenceStats_.gpuSsaoCompositeTiming);
    writePhase("GPU volumetric atmosphere", evidenceStats_.gpuAtmosphereTiming);
    writePhase(
        "  GPU global atmosphere",
        evidenceStats_.gpuAtmosphereGlobalTiming);
    writePhase(
        "    GPU ray integration",
        evidenceStats_.gpuAtmosphereGlobalIntegrationTiming);
    writePhase(
        "    GPU depth-aware composite",
        evidenceStats_.gpuAtmosphereGlobalCompositeTiming);
    writePhase(
        "  GPU bounded fog volumes",
        evidenceStats_.gpuAtmosphereVolumesTiming);
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
    const auto writeApplicationPhase = [&report](
        std::string_view label, const FrameTimeTelemetry& telemetry) {
        const FrameTimeSummary timing = telemetry.summary();
        report << "- " << label << ": average " << timing.averageMilliseconds
               << " ms, p95 " << timing.p95Milliseconds << " ms, maximum "
               << timing.maximumMilliseconds << " ms (" << timing.sampleCount
               << " samples)\n";
    };
    writeApplicationPhase("Application frame (including profiler, excluding frame cap)",
        applicationFrameTelemetry_);
    writeApplicationPhase("Frame interval (including pacing)", applicationIntervalTelemetry_);
    writeApplicationPhase("Frame pacing", applicationPacingTelemetry_);
    writeApplicationPhase("Application update", applicationUpdateTelemetry_);
    writeApplicationPhase("Application UI", applicationUiTelemetry_);
    writeApplicationPhase("Application frame build/prepare", applicationBuildTelemetry_);
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
    report << "- Process resident memory: "
           << evidenceStats_.processResidentBytes / (1024.0 * 1024.0)
           << " MiB (peak "
           << evidenceStats_.processPeakResidentBytes / (1024.0 * 1024.0)
           << " MiB)\n";
    report << "- GPU allocation memory: "
           << evidenceStats_.gpuMemoryAllocationBytes / (1024.0 * 1024.0)
           << " MiB in " << evidenceStats_.gpuMemoryAllocationCount
           << " allocations; "
           << evidenceStats_.gpuMemoryBlockBytes / (1024.0 * 1024.0)
           << " MiB reserved in blocks\n";
    report << "- Scene image: `" << sceneName << "`\n";
    if (evidenceAmbientOcclusionEnabled_) {
        report << "- Filtered SSAO image: `" << occlusionName << "`\n";
        report << "\nSimulation: " << (evidenceAnimate_ ? "fixed 1/60 s steps" : "frozen")
               << ". The final two images share the same simulation state "
                  "and differ by the SSAO composite debug selector.\n";
    } else {
        report << "\nSimulation: " << (evidenceAnimate_ ? "fixed 1/60 s steps" : "frozen")
               << ". Ambient occlusion was disabled for the complete timing window.\n";
    }
    writePerformanceAnalysisMarkdown(
        report,
        analyzePerformance(
            evidenceStats_, CpuProfiler::instance().latestFrame()));
    if (CpuProfiler::instance().enabled()) {
        std::string error;
        if (!CpuProfiler::instance().exportChromeTrace(
                evidenceOutputDirectory_ / "cpu-trace.json", &error)) {
            throw std::runtime_error("Could not export evidence CPU trace: " + error);
        }
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
