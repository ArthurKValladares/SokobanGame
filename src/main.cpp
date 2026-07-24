#include "engine/Application.hpp"
#include "engine/Log.hpp"

#include <exception>

int main(int, char**)
{
    int exitCode = 0;
    try {
        sokoban::Application app;
        app.run();
    } catch (const std::exception& error) {
        sokoban::log::error(sokoban::log::Category::Application)
            << "Fatal error: " << error.what();
        exitCode = 1;
    }
    sokoban::log::shutdown();
    return exitCode;
}
