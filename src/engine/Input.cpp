#include "engine/Input.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace sokoban {
namespace {

float normalizedAxis(Sint16 value)
{
    return value < 0
        ? static_cast<float>(value) / 32768.0f
        : static_cast<float>(value) / 32767.0f;
}

SDL_GamepadButton gamepadButtonFromBindingName(std::string_view name)
{
    if (name == "south") return SDL_GAMEPAD_BUTTON_SOUTH;
    if (name == "east") return SDL_GAMEPAD_BUTTON_EAST;
    if (name == "west") return SDL_GAMEPAD_BUTTON_WEST;
    if (name == "north") return SDL_GAMEPAD_BUTTON_NORTH;
    const std::string text(name);
    return SDL_GetGamepadButtonFromString(text.c_str());
}

std::string gamepadButtonBindingName(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "south";
    case SDL_GAMEPAD_BUTTON_EAST: return "east";
    case SDL_GAMEPAD_BUTTON_WEST: return "west";
    case SDL_GAMEPAD_BUTTON_NORTH: return "north";
    default:
        if (const char* name = SDL_GetGamepadStringForButton(button)) {
            return name;
        }
        return {};
    }
}

} // namespace

InputState::InputState(bool discoverConnectedGamepads)
{
    setBindings(defaultInputBindings());
    if (discoverConnectedGamepads) {
        openConnectedGamepads();
    }
}

InputState::~InputState()
{
    for (const OpenGamepad& gamepad : gamepads_) {
        SDL_CloseGamepad(gamepad.handle);
    }
}

void InputState::beginFrame()
{
    keysPressed_.fill(false);
    mouseButtonsPressed_.fill(false);
    gamepadButtonsPressed_.fill(false);
    previousGamepadAxes_ = gamepadAxes_;
}

void InputState::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode < SDL_SCANCODE_COUNT) {
        keysDown_[event.key.scancode] = true;
        keysPressed_[event.key.scancode] = true;
        activeDevice_ = ActiveInputDevice::KeyboardMouse;
    }

    if (event.type == SDL_EVENT_KEY_UP && event.key.scancode < SDL_SCANCODE_COUNT) {
        keysDown_[event.key.scancode] = false;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mousePosition_ = { event.motion.x, event.motion.y };
        activeDevice_ = ActiveInputDevice::KeyboardMouse;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        mousePosition_ = { event.button.x, event.button.y };
        if (event.button.button < mouseButtonsDown_.size()) {
            mouseButtonsDown_[event.button.button] = true;
            mouseButtonsPressed_[event.button.button] = true;
        }
        activeDevice_ = ActiveInputDevice::KeyboardMouse;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        mousePosition_ = { event.button.x, event.button.y };
        if (event.button.button < mouseButtonsDown_.size()) {
            mouseButtonsDown_[event.button.button] = false;
        }
    }

    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        openGamepad(event.gdevice.which);
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        closeGamepad(event.gdevice.which);
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMAPPED &&
        activeGamepadId_ == event.gdevice.which) {
        activateGamepad(event.gdevice.which, true);
    }

    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        keysDown_.fill(false);
        keysPressed_.fill(false);
        mouseButtonsDown_.fill(false);
        mouseButtonsPressed_.fill(false);
        clearGamepadState();
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED && activeGamepadId_ != 0) {
        pollActiveGamepadState();
        previousGamepadAxes_ = gamepadAxes_;
    }

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
        if (activeGamepadId_ != event.gbutton.which) {
            activateGamepad(event.gbutton.which, false);
        }
        gamepadButtonsDown_[event.gbutton.button] = true;
        gamepadButtonsPressed_[event.gbutton.button] = true;
        activeDevice_ = ActiveInputDevice::Gamepad;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
        activeGamepadId_ == event.gbutton.which &&
        event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
        gamepadButtonsDown_[event.gbutton.button] = false;
    }

    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) {
        const float value = normalizedAxis(event.gaxis.value);
        if (activeGamepadId_ == event.gaxis.which || std::abs(value) >= 0.25f) {
            if (activeGamepadId_ != event.gaxis.which) {
                activateGamepad(event.gaxis.which, false);
            }
            gamepadAxes_[event.gaxis.axis] = value;
            if (std::abs(value) >= 0.25f) {
                activeDevice_ = ActiveInputDevice::Gamepad;
            }
        }
    }
}

void InputState::setBindings(InputBindings bindings)
{
    bindings_ = std::move(bindings);
    for (auto& compiled : compiledBindings_) {
        compiled.clear();
    }
    invalidBindingCount_ = 0;

    for (std::size_t actionIndex = 0; actionIndex < inputActionCount; ++actionIndex) {
        for (const InputBinding& binding : bindings_.actions[actionIndex]) {
            std::visit([&](const auto& value) {
                using Binding = std::decay_t<decltype(value)>;
                CompiledBinding compiled;
                bool valid = true;
                if constexpr (std::is_same_v<Binding, KeyboardBinding>) {
                    compiled.kind = CompiledBindingKind::Keyboard;
                    compiled.control = SDL_GetScancodeFromName(value.scancode.c_str());
                    valid = compiled.control != SDL_SCANCODE_UNKNOWN;
                } else if constexpr (std::is_same_v<Binding, GamepadButtonBinding>) {
                    compiled.kind = CompiledBindingKind::GamepadButton;
                    compiled.control = gamepadButtonFromBindingName(value.button);
                    valid = compiled.control != SDL_GAMEPAD_BUTTON_INVALID;
                } else {
                    compiled.kind = CompiledBindingKind::GamepadAxis;
                    compiled.control = SDL_GetGamepadAxisFromString(value.axis.c_str());
                    compiled.direction = value.direction;
                    compiled.threshold = std::clamp(value.threshold, 0.1f, 1.0f);
                    valid = compiled.control != SDL_GAMEPAD_AXIS_INVALID;
                }
                if (valid) {
                    compiledBindings_[actionIndex].push_back(compiled);
                } else {
                    ++invalidBindingCount_;
                }
            }, binding);
        }
    }
}

std::optional<InputBinding> InputState::bindingCandidate(
    const SDL_Event& event,
    float axisCaptureThreshold)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (const char* name = SDL_GetScancodeName(event.key.scancode);
            name && *name != '\0') {
            return KeyboardBinding { name };
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        event.gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
        std::string name = gamepadButtonBindingName(
            static_cast<SDL_GamepadButton>(event.gbutton.button));
        if (!name.empty()) {
            return GamepadButtonBinding { std::move(name) };
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        event.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) {
        const float value = normalizedAxis(event.gaxis.value);
        const float captureThreshold = std::clamp(axisCaptureThreshold, 0.1f, 1.0f);
        if (std::abs(value) >= captureThreshold) {
            if (const char* name = SDL_GetGamepadStringForAxis(
                    static_cast<SDL_GamepadAxis>(event.gaxis.axis))) {
                return GamepadAxisBinding {
                    .axis = name,
                    .direction = value < 0.0f
                        ? AxisDirection::Negative
                        : AxisDirection::Positive,
                    .threshold = 0.5f,
                };
            }
        }
    }
    return std::nullopt;
}

bool InputState::actionDown(InputAction action) const
{
    const std::size_t index = static_cast<std::size_t>(action);
    return index < inputActionCount &&
        std::ranges::any_of(compiledBindings_[index], [&](const CompiledBinding& binding) {
            return bindingDown(binding, false);
        });
}

bool InputState::actionPressed(InputAction action) const
{
    const std::size_t index = static_cast<std::size_t>(action);
    return index < inputActionCount &&
        std::ranges::any_of(compiledBindings_[index], [&](const CompiledBinding& binding) {
            return bindingPressed(binding);
        });
}

bool InputState::keyBoundToAction(SDL_Scancode key, InputAction action) const
{
    const std::size_t index = static_cast<std::size_t>(action);
    return index < inputActionCount &&
        std::ranges::any_of(compiledBindings_[index], [&](const CompiledBinding& binding) {
            return binding.kind == CompiledBindingKind::Keyboard &&
                binding.control == key;
        });
}

bool InputState::keyDown(SDL_Scancode scancode) const
{
    return scancode >= 0 && scancode < SDL_SCANCODE_COUNT && keysDown_[scancode];
}

bool InputState::keyPressed(SDL_Scancode scancode) const
{
    return scancode >= 0 && scancode < SDL_SCANCODE_COUNT && keysPressed_[scancode];
}

bool InputState::mouseButtonDown(Uint8 button) const
{
    return button < mouseButtonsDown_.size() && mouseButtonsDown_[button];
}

bool InputState::mouseButtonPressed(Uint8 button) const
{
    return button < mouseButtonsPressed_.size() && mouseButtonsPressed_[button];
}

void InputState::openConnectedGamepads()
{
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    for (int i = 0; gamepads && i < count; ++i) {
        openGamepad(gamepads[i]);
    }
    SDL_free(gamepads);
}

void InputState::openGamepad(SDL_JoystickID id)
{
    if (gamepadHandle(id)) {
        return;
    }
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (!handle) {
        return;
    }
    gamepads_.push_back({ .id = id, .handle = handle });
    if (activeGamepadId_ == 0) {
        activateGamepad(id, true);
    }
}

void InputState::closeGamepad(SDL_JoystickID id)
{
    const auto found = std::ranges::find(gamepads_, id, &OpenGamepad::id);
    if (found == gamepads_.end()) {
        return;
    }
    SDL_CloseGamepad(found->handle);
    gamepads_.erase(found);
    if (activeGamepadId_ == id) {
        activeGamepadId_ = 0;
        clearGamepadState();
        activeGamepadName_.clear();
        if (!gamepads_.empty()) {
            activateGamepad(gamepads_.front().id, true);
        }
    }
}

void InputState::activateGamepad(SDL_JoystickID id, bool pollState)
{
    activeGamepadId_ = id;
    clearGamepadState();
    if (SDL_Gamepad* handle = gamepadHandle(id)) {
        if (const char* name = SDL_GetGamepadName(handle)) {
            activeGamepadName_ = name;
        } else {
            activeGamepadName_.clear();
        }
        if (pollState) {
            pollActiveGamepadState();
            previousGamepadAxes_ = gamepadAxes_;
        }
    } else {
        activeGamepadName_.clear();
    }
}

void InputState::clearGamepadState()
{
    gamepadButtonsDown_.fill(false);
    gamepadButtonsPressed_.fill(false);
    gamepadAxes_.fill(0.0f);
    previousGamepadAxes_.fill(0.0f);
}

void InputState::pollActiveGamepadState()
{
    SDL_Gamepad* handle = gamepadHandle(activeGamepadId_);
    if (!handle) {
        return;
    }
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
        gamepadButtonsDown_[button] = SDL_GetGamepadButton(
            handle, static_cast<SDL_GamepadButton>(button));
    }
    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
        gamepadAxes_[axis] = normalizedAxis(SDL_GetGamepadAxis(
            handle, static_cast<SDL_GamepadAxis>(axis)));
    }
}

SDL_Gamepad* InputState::gamepadHandle(SDL_JoystickID id) const
{
    const auto found = std::ranges::find(gamepads_, id, &OpenGamepad::id);
    return found == gamepads_.end() ? nullptr : found->handle;
}

bool InputState::bindingDown(const CompiledBinding& binding, bool previousAxis) const
{
    switch (binding.kind) {
    case CompiledBindingKind::Keyboard:
        return keyDown(static_cast<SDL_Scancode>(binding.control));
    case CompiledBindingKind::GamepadButton:
        return binding.control >= 0 && binding.control < SDL_GAMEPAD_BUTTON_COUNT &&
            gamepadButtonsDown_[binding.control];
    case CompiledBindingKind::GamepadAxis: {
        if (binding.control < 0 || binding.control >= SDL_GAMEPAD_AXIS_COUNT) {
            return false;
        }
        const float value = previousAxis
            ? previousGamepadAxes_[binding.control]
            : gamepadAxes_[binding.control];
        return binding.direction == AxisDirection::Negative
            ? value <= -binding.threshold
            : value >= binding.threshold;
    }
    }
    return false;
}

bool InputState::bindingPressed(const CompiledBinding& binding) const
{
    switch (binding.kind) {
    case CompiledBindingKind::Keyboard:
        return keyPressed(static_cast<SDL_Scancode>(binding.control));
    case CompiledBindingKind::GamepadButton:
        return binding.control >= 0 && binding.control < SDL_GAMEPAD_BUTTON_COUNT &&
            gamepadButtonsPressed_[binding.control];
    case CompiledBindingKind::GamepadAxis:
        return bindingDown(binding, false) && !bindingDown(binding, true);
    }
    return false;
}

} // namespace sokoban
