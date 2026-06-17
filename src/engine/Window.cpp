#include "engine/Window.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace sokoban {

Window::Window(const std::string& title, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    window_ = SDL_CreateWindow(
        title.c_str(),
        width,
        height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
}

Window::~Window()
{
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

} // namespace sokoban
