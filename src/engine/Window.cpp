#include "engine/Window.hpp"
#include "engine/Log.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <stdexcept>

namespace sokoban {
namespace {

void syncWindow(SDL_Window* window, const char* operation)
{
    if (!SDL_SyncWindow(window)) {
        throw std::runtime_error(
            std::string(operation) + " timed out: " + SDL_GetError());
    }
}

} // namespace

Window::Window(const std::string& title, int width, int height)
{
    const auto started = std::chrono::steady_clock::now();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    const auto initialized = std::chrono::steady_clock::now();

    window_ = SDL_CreateWindow(
        title.c_str(),
        width,
        height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    const auto created = std::chrono::steady_clock::now();
    log::info(log::Category::Application)
        << "Window startup phases (us): sdl-init="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               initialized - started).count()
        << " create="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               created - initialized).count()
        << " total="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               created - started).count();
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
    syncWindow(window_, "SDL_SetWindowFullscreen");
}

void Window::setWindowedMaximized()
{
    if (!SDL_SetWindowFullscreen(window_, false)) {
        throw std::runtime_error(
            std::string("SDL_SetWindowFullscreen failed: ") + SDL_GetError());
    }
    if (!SDL_MaximizeWindow(window_)) {
        throw std::runtime_error(
            std::string("SDL_MaximizeWindow failed: ") + SDL_GetError());
    }
    syncWindow(window_, "Window maximize");
}

void Window::setWindowedSize(int width, int height)
{
    if (!SDL_SetWindowFullscreen(window_, false)) {
        throw std::runtime_error(
            std::string("SDL_SetWindowFullscreen failed: ") + SDL_GetError());
    }
    if (!SDL_RestoreWindow(window_)) {
        throw std::runtime_error(
            std::string("SDL_RestoreWindow failed: ") + SDL_GetError());
    }
    if (!SDL_SetWindowSize(window_, width, height)) {
        throw std::runtime_error(
            std::string("SDL_SetWindowSize failed: ") + SDL_GetError());
    }
    if (!SDL_SetWindowPosition(
            window_,
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED)) {
        throw std::runtime_error(
            std::string("SDL_SetWindowPosition failed: ") + SDL_GetError());
    }
    syncWindow(window_, "Windowed size change");
}

} // namespace sokoban
