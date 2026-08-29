#include "engine/Application.hpp"
#include "engine/CommandLineOptions.hpp"
#include "engine/CrashDiagnostics.hpp"
#include "engine/Log.hpp"
#include "engine/SaveStore.hpp"
#include "engine/render/VulkanDebugUtils.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string_view>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_ENABLE_DEBUG_UI 0
#endif

int main(int argc, char** argv)
{
    // SDL uses this before its video subsystem starts to provide the platform
    // integration points with a stable product identity and version.
    if (!SDL_SetAppMetadata("Sokoban 3D", SOKOBAN_GAME_VERSION,
            "com.sokoban3d.game")) {
        sokoban::log::warning(sokoban::log::Category::Application)
            << "Could not set SDL application metadata: " << SDL_GetError();
    }

    std::filesystem::path diagnosticDirectory;
    std::filesystem::path logPath;
    int exitCode = 0;
    try {
        diagnosticDirectory = sokoban::SaveStore::preferencePath(
            "Sokoban3D", "Sokoban3D");
        logPath = diagnosticDirectory / "log.txt";
        sokoban::log::addFileSink(logPath);
        sokoban::crash::install(diagnosticDirectory / "crashes");
#if SOKOBAN_ENABLE_DEBUG_UI
        sokoban::log::setMinimumLevel(sokoban::log::Level::Debug);
#endif
        sokoban::log::info(sokoban::log::Category::Application)
            << "Session started: Sokoban 3D " << SOKOBAN_GAME_VERSION;

        // Parsed before a window, a device or a save file exists, so a bad
        // argument costs nothing and says so plainly. The parser itself is
        // headless and covered by tests/CommandLineOptionsTests.cpp.
        const std::vector<std::string_view> arguments(argv + 1, argv + argc);
        const sokoban::CommandLineOptions options =
            sokoban::parseCommandLine(arguments);
        if (options.malformed) {
            sokoban::log::error(sokoban::log::Category::Application)
                << options.error << ". " << sokoban::commandLineUsage;
            sokoban::log::shutdown();
            return 2;
        }

#if !SOKOBAN_ENABLE_DEBUG_UI
        // Recognised everywhere so the flag reports a policy rather than
        // looking like a typo, but only a developer build can do the work.
        if (options.bakeTileThumbnails) {
            sokoban::log::error(sokoban::log::Category::Application)
                << "--bake-tile-thumbnails is unavailable in a shipping build";
            sokoban::log::shutdown();
            return 2;
        }
#endif
        sokoban::Application app {
            sokoban::ApplicationOptions {
                .smokeFrames = options.smokeFrames,
                .saveDirectoryOverride = options.saveDirectory,
                .evidenceOutputDirectory =
                    options.evidenceOutputDirectory,
                .evidenceRenderScalePercent =
                    options.evidenceRenderScalePercent,
                .evidenceAmbientOcclusionEnabled =
                    options.evidenceAmbientOcclusionEnabled,
            }
        };
#if SOKOBAN_ENABLE_DEBUG_UI
        // Renders every tile through the normal frame path and saves the
        // result as a PNG, then exits. Run it after changing tile models,
        // materials or lighting; the editor palette loads what it produced.
        if (options.bakeTileThumbnails) {
            sokoban::log::info(sokoban::log::Category::Application)
                << "Starting in tile thumbnail bake mode; the game will not "
                   "run and the process exits when the bake finishes.";
            const bool baked = app.bakeTileThumbnails();
            sokoban::log::shutdown();
            return baked ? 0 : 1;
        }
#endif
        if (options.requireValidation &&
            !sokoban::vulkanDebug::validationActive()) {
            sokoban::log::error(sokoban::log::Category::Rendering)
                << "--require-validation was passed but the Vulkan validation "
                   "layer is not active. Build a Debug configuration with "
                   "SOKOBAN_ENABLE_VALIDATION=ON and make sure "
                   "VK_LAYER_KHRONOS_validation is installed and reachable "
                   "through VK_LAYER_PATH.";
            sokoban::log::shutdown();
            return 4;
        }

        // Said plainly, because "I passed the flag and it just opened the
        // game" is otherwise indistinguishable from the flag not arriving.
        if (!options.smokeRun()) {
            sokoban::log::info(sokoban::log::Category::Application)
                << "Starting normally. Pass --bake-tile-thumbnails to re-bake "
                   "the editor's tile palette pictures instead.";
        }
        app.run();

        // A clean exit is not the same as a clean run. The validation layer
        // logs everything it finds, and in a wall of stderr that is easy to
        // miss; turning it into an exit code is what lets CI gate on it.
        //
        // Only meaningful when validation was actually loaded - shipping
        // builds do not request the layer, so this is always zero there.
        const std::uint64_t validationErrors =
            sokoban::vulkanDebug::validationErrorCount();
        if (validationErrors != 0) {
            sokoban::log::error(sokoban::log::Category::Rendering)
                << "Vulkan validation reported " << validationErrors
                << " error(s); see the messages above.";
            exitCode = 3;
        }
    } catch (const std::exception& error) {
        sokoban::log::error(sokoban::log::Category::Application)
            << "Fatal error: " << error.what();
        sokoban::log::flush();
        const auto dumpPath = sokoban::crash::writeMinidump();
        if (dumpPath) {
            sokoban::log::info(sokoban::log::Category::Application)
                << "Wrote crash dump: " << dumpPath->string();
            sokoban::log::flush();
        }
        sokoban::crash::showFatalErrorDialog(error.what(), logPath, dumpPath);
        exitCode = 1;
    } catch (...) {
        constexpr std::string_view unknownError = "An unknown exception escaped the game loop.";
        sokoban::log::error(sokoban::log::Category::Application)
            << "Fatal error: " << unknownError;
        sokoban::log::flush();
        const auto dumpPath = sokoban::crash::writeMinidump();
        sokoban::crash::showFatalErrorDialog(unknownError, logPath, dumpPath);
        exitCode = 1;
    }
    sokoban::log::shutdown();
    return exitCode;
}
