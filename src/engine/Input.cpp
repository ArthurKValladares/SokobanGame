#include "engine/Input.hpp"

namespace sokoban {

void InputState::beginFrame()
{
    keysPressed_.fill(false);
    mouseButtonsPressed_.fill(false);
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

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mousePosition_ = { event.motion.x, event.motion.y };
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        mousePosition_ = { event.button.x, event.button.y };
        if (event.button.button < mouseButtonsDown_.size()) {
            mouseButtonsDown_[event.button.button] = true;
            mouseButtonsPressed_[event.button.button] = true;
        }
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        mousePosition_ = { event.button.x, event.button.y };
        if (event.button.button < mouseButtonsDown_.size()) {
            mouseButtonsDown_[event.button.button] = false;
        }
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

bool InputState::mouseButtonDown(Uint8 button) const
{
    return button < mouseButtonsDown_.size() && mouseButtonsDown_[button];
}

bool InputState::mouseButtonPressed(Uint8 button) const
{
    return button < mouseButtonsPressed_.size() && mouseButtonsPressed_[button];
}

} // namespace sokoban
