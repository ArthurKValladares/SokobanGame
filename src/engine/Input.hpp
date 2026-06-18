#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>

#include <array>

namespace sokoban {

class InputState {
public:
    void beginFrame();
    void handleEvent(const SDL_Event& event);

    [[nodiscard]] bool keyDown(SDL_Scancode scancode) const;

private:
    std::array<bool, SDL_SCANCODE_COUNT> keysDown_ {};
};

} // namespace sokoban
