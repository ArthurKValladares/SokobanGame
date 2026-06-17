#pragma once

#include <SDL3/SDL_video.h>

#include <string>

namespace sokoban {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] SDL_Window* nativeHandle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
};

} // namespace sokoban
