#pragma once

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
struct CommandLineOptions {
    // Render this many frames through the ordinary loop, then exit. Zero runs
    // until the player quits.
    std::uint64_t smokeFrames = 0;
    // Roots saves and the pipeline cache here instead of the preference path.
    std::string saveDirectory;
    // Fail rather than run when the Vulkan validation layer is not active.
    bool requireValidation = false;
    // Re-bake the editor's tile palette pictures and exit.
    bool bakeTileThumbnails = false;
    // Archive the last normal frame, a filtered SSAO debug frame, and timing
    // statistics after a smoke run. The scale override keeps separate runs
    // comparable without reading or rewriting a user profile.
    std::string evidenceOutputDirectory;
    int evidenceRenderScalePercent = 100;
    bool evidenceAmbientOcclusionEnabled = true;
    bool evidenceFrustumCullingEnabled = true;
    // Set when parsing rejected the arguments; `error` says why.
    bool malformed = false;
    std::string error;

    [[nodiscard]] bool smokeRun() const { return smokeFrames != 0; }
};

[[nodiscard]] inline CommandLineOptions parseCommandLine(
    std::span<const std::string_view> arguments)
{
    CommandLineOptions options;
    bool evidenceScaleSpecified = false;
    bool evidenceAmbientOcclusionSpecified = false;
    bool evidenceFrustumCullingSpecified = false;
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
        } else if (argument == "--evidence-disable-ao") {
            options.evidenceAmbientOcclusionEnabled = false;
            evidenceAmbientOcclusionSpecified = true;
        } else if (argument == "--evidence-disable-frustum-culling") {
            options.evidenceFrustumCullingEnabled = false;
            evidenceFrustumCullingSpecified = true;
        } else {
            return reject("Unknown argument '" + std::string(argument) + "'");
        }
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
        evidenceAmbientOcclusionSpecified) {
        return reject("--evidence-disable-ao requires --evidence-output");
    }
    if (options.evidenceOutputDirectory.empty() &&
        evidenceFrustumCullingSpecified) {
        return reject(
            "--evidence-disable-frustum-culling requires --evidence-output");
    }
    return options;
}

inline constexpr std::string_view commandLineUsage =
    "Usage: sokoban [--smoke-frames <positive integer>] "
    "[--save-directory <path>] [--require-validation] "
    "[--bake-tile-thumbnails] "
    "[--evidence-output <directory> "
    "--evidence-render-scale <25..100> [--evidence-disable-ao] "
    "[--evidence-disable-frustum-culling]]";

} // namespace sokoban
