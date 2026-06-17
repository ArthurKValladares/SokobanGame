#include "engine/Application.hpp"

#include <SDL3/SDL.h>

namespace sokoban {

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , renderer_(window_.nativeHandle(), SOKOBAN_ASSET_DIR)
{
}

Application::~Application()
{
    renderer_.waitIdle();
}

void Application::run()
{
    while (running_) {
        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                running_ = false;
            }
        }

        renderer_.drawFrame();
    }
}

} // namespace sokoban
