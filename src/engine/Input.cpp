#include "engine/Input.hpp"

namespace sokoban {

void InputState::beginFrame()
{
    keysPressed_.fill(false);
}

void InputState::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        keysDown_[event.key.scancode] = true;
        keysPressed_[event.key.scancode] = true;
    }

    if (event.type == SDL_EVENT_KEY_UP) {
        keysDown_[event.key.scancode] = false;
    }
}

bool InputState::keyDown(SDL_Scancode scancode) const
{
    return keysDown_[scancode];
}

bool InputState::keyPressed(SDL_Scancode scancode) const
{
    return keysPressed_[scancode];
}

} // namespace sokoban
