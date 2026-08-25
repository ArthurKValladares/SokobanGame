#include "engine/Application.hpp"
#include "engine/CrashDiagnostics.hpp"
#include "engine/Log.hpp"
#include "engine/SaveStore.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <exception>
#include <filesystem>
#include <string_view>

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

#if SOKOBAN_ENABLE_DEBUG_UI
    // Renders every tile through the normal frame path and saves the result as
    // a PNG, then exits. Run it after changing tile models, materials or
    // lighting; the editor palette loads whatever it produced.
    bool bakeThumbnails = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--bake-tile-thumbnails") {
            bakeThumbnails = true;
        }
    }
#else
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--bake-tile-thumbnails") {
            sokoban::log::error(sokoban::log::Category::Application)
                << "--bake-tile-thumbnails is unavailable in a shipping build";
            sokoban::log::shutdown();
            return 2;
        }
    }
#endif
        sokoban::Application app;
#if SOKOBAN_ENABLE_DEBUG_UI
        if (bakeThumbnails) {
            sokoban::log::info(sokoban::log::Category::Application)
                << "Starting in tile thumbnail bake mode; the game will not "
                   "run and the process exits when the bake finishes.";
            const bool baked = app.bakeTileThumbnails();
            sokoban::log::shutdown();
            return baked ? 0 : 1;
        }
#endif
        // Said plainly, because "I passed the flag and it just opened the
        // game" is otherwise indistinguishable from the flag not arriving.
        sokoban::log::info(sokoban::log::Category::Application)
            << "Starting normally. Pass --bake-tile-thumbnails to re-bake the "
               "editor's tile palette pictures instead.";
        app.run();
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
