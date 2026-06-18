#pragma once

#include "engine/Math.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <array>

namespace sokoban {

class InputState {
public:
    void beginFrame();
    void handleEvent(const SDL_Event& event);

    [[nodiscard]] bool keyDown(SDL_Scancode scancode) const;
    [[nodiscard]] bool keyPressed(SDL_Scancode scancode) const;
    [[nodiscard]] bool mouseButtonDown(Uint8 button) const;
    [[nodiscard]] bool mouseButtonPressed(Uint8 button) const;
    [[nodiscard]] Vec2 mousePosition() const { return mousePosition_; }

private:
    std::array<bool, SDL_SCANCODE_COUNT> keysDown_ {};
    std::array<bool, SDL_SCANCODE_COUNT> keysPressed_ {};
    std::array<bool, 8> mouseButtonsDown_ {};
    std::array<bool, 8> mouseButtonsPressed_ {};
    Vec2 mousePosition_ {};
};

} // namespace sokoban
