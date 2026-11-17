#pragma once

#include "engine/EvidenceScenarios.hpp"
#include "engine/LaunchOptions.hpp"

#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace sokoban {

// Parsed process arguments.
//
// Kept out of main.cpp and free of platform headers so the edge cases can be
// tested headlessly: a flag whose value is missing at the end of argv, a
// non-numeric count, a trailing-garbage count, and zero all have to be
// rejected rather than quietly becoming a run that does nothing. A CI gate
// built on --smoke-frames is only as trustworthy as this.
//
// The switches Application consumes unchanged live in LaunchOptions; the
// fields here are the ones main.cpp acts on or converts first.
struct CommandLineOptions : LaunchOptions {
    // Roots saves, diagnostics and the pipeline cache here instead of the
    // preference path.
    std::string saveDirectory;
    // Fail rather than run when the Vulkan validation layer is not active.
    bool requireValidation = false;
    // Re-bake the editor's tile palette pictures and exit.
    bool bakeTileThumbnails = false;
    // Destination of the evidence archive; see LaunchOptions. Empty means an
    // ordinary run, which never performs readback.
    std::string evidenceOutputDirectory;
    // --edit opens a level document in the editor. Needs a build with
    // developer tools.
    std::string editDocument;
    // Set when parsing rejected the arguments; `error` says why.
    bool malformed = false;
    std::string error;

    [[nodiscard]] bool smokeRun() const { return smokeFrames != 0; }
    [[nodiscard]] bool startLocationRequested() const
    {
        return startLevel >= 0;
    }
};

[[nodiscard]] inline CommandLineOptions parseCommandLine(
    std::span<const std::string_view> arguments)
{
    CommandLineOptions options;
    bool evidenceScaleSpecified = false;
    bool evidenceAntiAliasingSpecified = false;
    bool evidenceVsyncSpecified = false;
    bool evidenceAmbientOcclusionSpecified = false;
    bool evidenceFrustumCullingSpecified = false;
    bool evidenceWaterSpecified = false;
    bool evidencePointLightSpecified = false;
    bool evidencePointLightStressSpecified = false;
    bool evidenceScreenSpecified = false;
    bool startScreenSpecified = false;
    const auto reject = [&options](std::string message) {
        options.malformed = true;
        options.error = std::move(message);
        return options;
    };

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--bake-tile-thumbnails") {
            options.bakeTileThumbnails = true;
        } else if (argument == "--require-validation") {
            options.requireValidation = true;
        } else if (argument == "--smoke-frames") {
            if (index + 1 >= arguments.size()) {
                return reject("--smoke-frames needs a frame count");
            }
            const std::string_view value = arguments[++index];
            std::uint64_t frames = 0;
            const char* const begin = value.data();
            const char* const end = begin + value.size();
            const std::from_chars_result parsed =
                std::from_chars(begin, end, frames);
            // from_chars stops at the first character it cannot use, so the
            // end check is what rejects "12x" rather than accepting 12.
            if (parsed.ec != std::errc {} || parsed.ptr != end) {
                return reject(
                    "--smoke-frames wants a positive integer, got '" +
                    std::string(value) + "'");
            }
            if (frames == 0) {
                return reject("--smoke-frames must be greater than zero");
            }
            options.smokeFrames = frames;
        } else if (argument == "--continue") {
            options.continueGame = true;
        } else if (argument == "--title") {
            options.showTitle = true;
        } else if (argument == "--level" || argument == "--screen" ||
            argument == "--evidence-level" || argument == "--evidence-screen") {
            if (index + 1 >= arguments.size()) {
                return reject(
                    std::string(argument) + " needs a zero-based index");
            }
            const std::string_view value = arguments[++index];
            int parsedIndex = -1;
            const char* const begin = value.data();
            const char* const end = begin + value.size();
            const std::from_chars_result parsed =
                std::from_chars(begin, end, parsedIndex);
            if (parsed.ec != std::errc {} || parsed.ptr != end ||
                parsedIndex < 0) {
                return reject(
                    std::string(argument) +
                    " wants a non-negative integer, got '" +
                    std::string(value) + "'");
            }
            if (argument == "--evidence-level") {
                options.evidenceLevel = parsedIndex;
            } else if (argument == "--evidence-screen") {
                options.evidenceScreen = parsedIndex;
                evidenceScreenSpecified = true;
            } else if (argument == "--level") {
                options.startLevel = parsedIndex;
            } else {
                options.startScreen = parsedIndex;
                startScreenSpecified = true;
            }
        } else if (argument == "--edit") {
            if (index + 1 >= arguments.size()) {
                return reject("--edit needs a level document path");
            }
            options.editDocument = std::string(arguments[++index]);
            if (options.editDocument.empty()) {
                return reject("--edit cannot be empty");
            }
        } else if (argument == "--save-directory") {
            if (index + 1 >= arguments.size()) {
                return reject("--save-directory needs a path");
            }
            options.saveDirectory = std::string(arguments[++index]);
        } else if (argument == "--evidence-output") {
            if (index + 1 >= arguments.size()) {
                return reject("--evidence-output needs a directory");
            }
            options.evidenceOutputDirectory =
                std::string(arguments[++index]);
            if (options.evidenceOutputDirectory.empty()) {
                return reject("--evidence-output cannot be empty");
            }
        } else if (argument == "--evidence-render-scale") {
            if (index + 1 >= arguments.size()) {
                return reject("--evidence-render-scale needs a percentage");
            }
            const std::string_view value = arguments[++index];
            int percent = 0;
            const char* const begin = value.data();
            const char* const end = begin + value.size();
            const std::from_chars_result parsed =
                std::from_chars(begin, end, percent);
            if (parsed.ec != std::errc {} || parsed.ptr != end ||
                percent < 25 || percent > 100) {
                return reject(
                    "--evidence-render-scale wants an integer from 25 to 100, got '" +
                    std::string(value) + "'");
            }
            options.evidenceRenderScalePercent = percent;
            evidenceScaleSpecified = true;
        } else if (argument == "--evidence-msaa") {
            if (index + 1 >= arguments.size()) {
                return reject("--evidence-msaa needs a sample count");
            }
            const std::string_view value = arguments[++index];
            int samples = 0;
            const char* const begin = value.data();
            const char* const end = begin + value.size();
            const std::from_chars_result parsed =
                std::from_chars(begin, end, samples);
            if (parsed.ec != std::errc {} || parsed.ptr != end ||
                (samples != 1 && samples != 2 && samples != 4 &&
                    samples != 8)) {
                return reject(
                    "--evidence-msaa wants one of 1, 2, 4, or 8, got '" +
                    std::string(value) + "'");
            }
            options.evidenceAntiAliasingSamples = samples;
            evidenceAntiAliasingSpecified = true;
        } else if (argument == "--evidence-disable-ao") {
            options.evidenceAmbientOcclusionEnabled = false;
            evidenceAmbientOcclusionSpecified = true;
        } else if (argument == "--evidence-disable-vsync") {
            options.evidenceVsyncEnabled = false;
            evidenceVsyncSpecified = true;
        } else if (argument == "--evidence-disable-frustum-culling") {
            options.evidenceFrustumCullingEnabled = false;
            evidenceFrustumCullingSpecified = true;
        } else if (argument == "--evidence-water") {
            options.evidenceWaterEnabled = true;
            evidenceWaterSpecified = true;
        } else if (argument == "--evidence-debug-ui") {
            options.evidenceDebugUi = true;
        } else if (argument == "--evidence-disable-profiler") {
            options.evidenceProfilerEnabled = false;
        } else if (argument == "--evidence-animate") {
            options.evidenceAnimate = true;
        } else if (argument == "--evidence-effects") {
            if (index + 1 >= arguments.size() ||
                !isEvidenceEffectScenario(arguments[index + 1])) {
                return reject("--evidence-effects needs mirror-swap, witch-swap, turret-volley, portals, special-blocks, mixed-stress, or rock-scratches");
            }
            options.evidenceEffects = arguments[++index];
        } else if (argument == "--evidence-disable-water") {
            options.evidenceWaterDisabled = true;
        } else if (argument == "--evidence-disable-water-reflections") {
            options.evidenceWaterReflectionsDisabled = true;
        } else if (argument == "--serial-scene-preparation") {
            options.parallelScenePreparationEnabled = false;
        } else if (argument == "--evidence-point-light") {
            options.evidencePointLightEnabled = true;
            evidencePointLightSpecified = true;
        } else if (argument == "--evidence-point-light-stress") {
            options.evidencePointLightStressEnabled = true;
            evidencePointLightStressSpecified = true;
        } else if (argument == "--disable-point-shadow-optimizations") {
            options.pointShadowOptimizationsEnabled = false;
        } else if (argument == "--disable-recorder-scratch-reuse") {
            options.recorderScratchReuseEnabled = false;
        } else if (argument == "--disable-water-cell-cache") {
            options.waterCellCacheEnabled = false;
        } else if (argument == "--disable-ground-geometry") {
            options.groundGeometryProcessingEnabled = false;
        } else if (argument == "--ground-rim") {
            options.groundRimEnabled = true;
        } else if (argument == "--disable-ground-rim") {
            options.groundRimEnabled = false;
        } else if (argument == "--ground-chunks") {
            options.groundChunksEnabled = true;
        } else if (argument == "--disable-ground-meshoptimizer") {
            options.groundChunkMeshoptimizerEnabled = false;
        } else if (argument == "--evidence-ground-rim-fixture") {
            options.evidenceGroundRimFixture = true;
        } else if (argument == "--texture-residency-kib") {
            if (index + 1 >= arguments.size()) {
                return reject("--texture-residency-kib needs a size");
            }
            const std::string_view value = arguments[++index];
            std::uint64_t kibibytes = 0;
            const char* const begin = value.data();
            const char* const end = begin + value.size();
            const std::from_chars_result parsed =
                std::from_chars(begin, end, kibibytes);
            constexpr std::uint64_t maximumKiB = 16ULL * 1024ULL * 1024ULL;
            if (parsed.ec != std::errc {} || parsed.ptr != end ||
                kibibytes == 0 || kibibytes > maximumKiB) {
                return reject(
                    "--texture-residency-kib wants an integer from 1 to " +
                    std::to_string(maximumKiB) + ", got '" +
                    std::string(value) + "'");
            }
            options.textureResidencyBudgetKiB = kibibytes;
        } else {
            return reject("Unknown argument '" + std::string(argument) + "'");
        }
    }
    if (startScreenSpecified && options.startLevel < 0) {
        return reject("--screen requires --level");
    }
    const bool launchShortcut = options.continueGame ||
        options.startLocationRequested() || !options.editDocument.empty();
    if (options.showTitle && launchShortcut) {
        return reject(
            "--title cannot be combined with --continue, --level, or --edit");
    }
    if (options.smokeRun() && (launchShortcut || options.showTitle)) {
        return reject(
            "--smoke-frames always starts a new game; it cannot be combined "
            "with --continue, --title, --level, or --edit");
    }
    if (options.startLocationRequested() && !options.editDocument.empty()) {
        return reject("--level and --edit are mutually exclusive");
    }
    if (!options.evidenceOutputDirectory.empty() && options.smokeFrames < 3) {
        return reject(
            "--evidence-output requires --smoke-frames of at least 3");
    }
    if (options.evidenceOutputDirectory.empty() && evidenceScaleSpecified) {
        return reject(
            "--evidence-render-scale requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() &&
        evidenceAntiAliasingSpecified) {
        return reject("--evidence-msaa requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() &&
        evidenceAmbientOcclusionSpecified) {
        return reject("--evidence-disable-ao requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() && evidenceVsyncSpecified) {
        return reject("--evidence-disable-vsync requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() &&
        evidenceFrustumCullingSpecified) {
        return reject(
            "--evidence-disable-frustum-culling requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() && evidenceWaterSpecified) {
        return reject("--evidence-water requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() && evidencePointLightSpecified) {
        return reject("--evidence-point-light requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() &&
        evidencePointLightStressSpecified) {
        return reject(
            "--evidence-point-light-stress requires --evidence-output");
    }
    if (options.evidencePointLightEnabled &&
        options.evidencePointLightStressEnabled) {
        return reject(
            "--evidence-point-light and --evidence-point-light-stress are mutually exclusive");
    }
    if (evidenceScreenSpecified && options.evidenceLevel < 0) {
        return reject("--evidence-screen requires --evidence-level");
    }
    if (options.evidenceWaterEnabled && options.evidenceWaterDisabled) {
        return reject("--evidence-water and --evidence-disable-water are mutually exclusive");
    }
    if (options.evidenceOutputDirectory.empty() &&
        (options.evidenceLevel >= 0 || options.evidenceDebugUi || options.evidenceAnimate ||
            !options.evidenceEffects.empty() || options.evidenceWaterDisabled ||
            options.evidenceWaterReflectionsDisabled ||
            options.evidenceGroundRimFixture ||
            !options.evidenceProfilerEnabled)) {
        return reject("Evidence scenario options require --evidence-output");
    }
    return options;
}

inline constexpr std::string_view commandLineUsage =
    "Usage: sokoban [--continue | --title] "
    "[--level <index> [--screen <index>] | --edit <level document>] "
    "[--smoke-frames <positive integer>] "
    "[--save-directory <path>] [--require-validation] "
    "[--ground-chunks] [--disable-ground-meshoptimizer] "
    "[--texture-residency-kib <1..16777216>] "
    "[--serial-scene-preparation] "
    "[--disable-point-shadow-optimizations] "
    "[--disable-recorder-scratch-reuse] "
    "[--disable-water-cell-cache] "
    "[--bake-tile-thumbnails] "
    "[--evidence-output <directory> "
    "--evidence-render-scale <25..100> [--evidence-msaa <1|2|4|8>] "
    "[--evidence-disable-vsync] "
    "[--evidence-level <index> [--evidence-screen <index>]] "
    "[--evidence-debug-ui] [--evidence-disable-profiler] "
    "[--evidence-animate] "
    "[--evidence-effects <scenario>] [--evidence-disable-water] "
    "[--evidence-disable-water-reflections] "
    "[--evidence-disable-ao] "
    "[--evidence-disable-frustum-culling] [--evidence-water] "
    "[--disable-ground-geometry] "
    "[--ground-rim] [--disable-ground-rim] [--evidence-ground-rim-fixture] "
    "[--evidence-point-light] "
    "[--evidence-point-light-stress]]";

} // namespace sokoban
