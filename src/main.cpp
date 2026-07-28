#include "engine/Application.hpp"
#include "engine/Log.hpp"

#include <exception>
#include <string_view>

int main(int argc, char** argv)
{
    // Renders every tile through the normal frame path and saves the result as
    // a PNG, then exits. Run it after changing tile models, materials or
    // lighting; the editor palette loads whatever it produced.
    bool bakeThumbnails = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--bake-tile-thumbnails") {
            bakeThumbnails = true;
        }
    }

    int exitCode = 0;
    try {
        sokoban::Application app;
        if (bakeThumbnails) {
            sokoban::log::info(sokoban::log::Category::Application)
                << "Starting in tile thumbnail bake mode; the game will not "
                   "run and the process exits when the bake finishes.";
            const bool baked = app.bakeTileThumbnails();
            sokoban::log::shutdown();
            return baked ? 0 : 1;
        }
        // Said plainly, because "I passed the flag and it just opened the
        // game" is otherwise indistinguishable from the flag not arriving.
        sokoban::log::info(sokoban::log::Category::Application)
            << "Starting normally. Pass --bake-tile-thumbnails to re-bake the "
               "editor's tile palette pictures instead.";
        app.run();
    } catch (const std::exception& error) {
        sokoban::log::error(sokoban::log::Category::Application)
            << "Fatal error: " << error.what();
        exitCode = 1;
    }
    sokoban::log::shutdown();
    return exitCode;
}
