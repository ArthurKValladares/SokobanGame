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
    bool ambientOcclusionEnabled)
{
    std::string suffix = "scale-" + std::to_string(renderScalePercent);
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
    const std::string suffix = evidenceSuffix(
        evidenceStats_.renderScalePercent,
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
           << evidenceStats_.renderScalePercent << "% scale\n\n";
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
    report << "- MSAA samples: " << evidenceStats_.activeSamples << "\n";
    report << "- Draw calls: " << evidenceStats_.drawCalls << "\n";
    report << "- Triangles: " << evidenceStats_.triangles << "\n";
    report << "- Render passes: " << evidenceStats_.renderPasses << "\n";
    report << "- Persistent renderables: "
           << evidenceStats_.persistentRenderables << " (bounds reused "
           << evidenceStats_.reusedRenderableBounds << ", rebuilt "
           << evidenceStats_.rebuiltRenderableBounds << ")\n";
    report << "- CPU frame: average "
           << evidenceStats_.cpuFrameAverageMilliseconds << " ms, p95 "
           << evidenceStats_.cpuFrameP95Milliseconds << " ms, maximum "
           << evidenceStats_.cpuFrameMaximumMilliseconds << " ms ("
           << evidenceStats_.cpuFrameTimingSamples << " samples)\n";
    if (evidenceStats_.gpuFrameTimingAvailable) {
        report << "- GPU frame: average "
               << evidenceStats_.gpuFrameAverageMilliseconds << " ms, p95 "
               << evidenceStats_.gpuFrameP95Milliseconds << " ms, maximum "
               << evidenceStats_.gpuFrameMaximumMilliseconds << " ms ("
               << evidenceStats_.gpuFrameTimingSamples << " samples)\n";
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
