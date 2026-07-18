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
        SDL_WINDOW_VULKAN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

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

Vec2 Window::size() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    return { static_cast<float>(width), static_cast<float>(height) };
}

Vec2 Window::sizeInPixels() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    return { static_cast<float>(width), static_cast<float>(height) };
}

void Window::setFullscreen(bool fullscreen)
{
    if (!SDL_SetWindowFullscreen(window_, fullscreen)) {
        throw std::runtime_error(
            std::string("SDL_SetWindowFullscreen failed: ") + SDL_GetError());
    }
}

} // namespace sokoban
