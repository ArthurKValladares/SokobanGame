#include "engine/Application.hpp"

#include <exception>
#include <iostream>

int main(int, char**)
{
    try {
        sokoban::Application app;
        app.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
