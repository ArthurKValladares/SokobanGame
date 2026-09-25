#pragma once

#include "engine/InputBindings.hpp"
#include "engine/Math.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {

enum class ActiveInputDevice {
    KeyboardMouse,
    Gamepad,
};

struct GamepadPresentation {
    SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN;
    std::string name;
    std::array<SDL_GamepadButtonLabel, 4> faceButtonLabels {
        SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
        SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
        SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
        SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
    };
};

// Owns SDL device state and maps raw controls to semantic actions. Gameplay
// consumes actions; raw keyboard/mouse queries remain available for editor UI.
class InputState {
public:
    enum class PressPolicy {
        Record,
        Suppress,
    };

    explicit InputState(bool discoverConnectedGamepads = true);
    ~InputState();

    InputState(const InputState&) = delete;
    InputState& operator=(const InputState&) = delete;

    void beginFrame();
    // Suppress keeps physical held state current without creating a press
    // edge. It is used while a binding capture owns the control event.
    void handleEvent(
        const SDL_Event& event,
        PressPolicy pressPolicy = PressPolicy::Record);
    void setBindings(InputBindings bindings);

    // A capture candidate. A keyboard chord is captured when its non-modifier
    // key goes down, carrying the modifiers held at that moment; a modifier
    // key on its own (for a held action such as the eyedropper) is captured
    // when it is released without another key having been pressed.
    [[nodiscard]] static std::optional<InputBinding> bindingCandidate(
        const SDL_Event& event,
        float axisCaptureThreshold = 0.75f);
    [[nodiscard]] static bool isModifierKey(SDL_Scancode scancode);

    [[nodiscard]] bool actionDown(InputAction action) const;
    [[nodiscard]] bool actionPressed(InputAction action) const;
    [[nodiscard]] bool keyBoundToAction(SDL_Scancode key, InputAction action) const;
    [[nodiscard]] bool keyDown(SDL_Scancode scancode) const;
    [[nodiscard]] bool keyPressed(SDL_Scancode scancode) const;
    // KeyModifier bits currently held (either side).
    [[nodiscard]] std::uint8_t heldModifiers() const;
    [[nodiscard]] bool mouseButtonDown(Uint8 button) const;
    [[nodiscard]] bool mouseButtonPressed(Uint8 button) const;
    [[nodiscard]] Vec2 mousePosition() const { return mousePosition_; }
    [[nodiscard]] const InputBindings& bindings() const { return bindings_; }
    [[nodiscard]] ActiveInputDevice activeDevice() const { return activeDevice_; }
    [[nodiscard]] std::size_t connectedGamepadCount() const { return gamepads_.size(); }
    [[nodiscard]] const std::string& activeGamepadName() const { return activeGamepadName_; }
    [[nodiscard]] GamepadPresentation activeGamepadPresentation() const;
    [[nodiscard]] std::size_t invalidBindingCount() const { return invalidBindingCount_; }

private:
    enum class CompiledBindingKind {
        Keyboard,
        GamepadButton,
        GamepadAxis,
    };

    struct CompiledBinding {
        CompiledBindingKind kind = CompiledBindingKind::Keyboard;
        int control = 0;
        std::uint8_t modifiers = keyModifierNone;
        AxisDirection direction = AxisDirection::Positive;
        float threshold = 0.5f;
    };

    struct OpenGamepad {
        SDL_JoystickID id = 0;
        SDL_Gamepad* handle = nullptr;
    };

    void openConnectedGamepads();
    void openGamepad(SDL_JoystickID id);
    void closeGamepad(SDL_JoystickID id);
    void activateGamepad(SDL_JoystickID id, bool pollState);
    void clearGamepadState();
    void pollActiveGamepadState();
    [[nodiscard]] SDL_Gamepad* gamepadHandle(SDL_JoystickID id) const;
    [[nodiscard]] bool bindingDown(const CompiledBinding& binding, bool previousAxis) const;
    [[nodiscard]] bool bindingPressed(const CompiledBinding& binding) const;
    // Whether a keyboard binding's chord is the one the held modifiers
    // select for its key: all of its modifiers are held, and no binding on
    // the same key with more of the held modifiers exists.
    [[nodiscard]] bool keyboardChordSelected(
        const CompiledBinding& binding) const;

    InputBindings bindings_ = defaultInputBindings();
    std::array<std::vector<CompiledBinding>, inputActionCount> compiledBindings_;
    // Every keyboard (scancode, modifiers) pair bound to any action, for
    // choosing the most specific chord.
    std::vector<std::pair<int, std::uint8_t>> keyboardChords_;
    std::array<bool, SDL_SCANCODE_COUNT> keysDown_ {};
    std::array<bool, SDL_SCANCODE_COUNT> keysPressed_ {};
    std::array<bool, 8> mouseButtonsDown_ {};
    std::array<bool, 8> mouseButtonsPressed_ {};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> gamepadButtonsDown_ {};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> gamepadButtonsPressed_ {};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> gamepadAxes_ {};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> previousGamepadAxes_ {};
    std::vector<OpenGamepad> gamepads_;
    SDL_JoystickID activeGamepadId_ = 0;
    Vec2 mousePosition_ {};
    std::string activeGamepadName_;
    ActiveInputDevice activeDevice_ = ActiveInputDevice::KeyboardMouse;
    std::size_t invalidBindingCount_ = 0;
};

} // namespace sokoban
