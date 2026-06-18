#include "engine/Input.hpp"

namespace sokoban {

void InputState::beginFrame()
{
}

void InputState::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        keysDown_[event.key.scancode] = true;
    }

    if (event.type == SDL_EVENT_KEY_UP) {
        keysDown_[event.key.scancode] = false;
    }
}

bool InputState::keyDown(SDL_Scancode scancode) const
{
    return keysDown_[scancode];
}

} // namespace sokoban
