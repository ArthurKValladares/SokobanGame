#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sokoban {

enum class InputAction {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Undo,
    Restart,
    MenuBack,
    MenuConfirm,
    Count,
};

enum class AxisDirection {
    Negative,
    Positive,
};

struct KeyboardBinding {
    std::string scancode;

    bool operator==(const KeyboardBinding&) const = default;
};

struct GamepadButtonBinding {
    // SDL's stable gamepad mapping name, such as "dpup" or "west".
    std::string button;

    bool operator==(const GamepadButtonBinding&) const = default;
};

struct GamepadAxisBinding {
    // SDL's stable gamepad mapping name, such as "leftx" or "lefty".
    std::string axis;
    AxisDirection direction = AxisDirection::Positive;
    float threshold = 0.5f;

    bool operator==(const GamepadAxisBinding&) const = default;
};

using InputBinding = std::variant<
    KeyboardBinding,
    GamepadButtonBinding,
    GamepadAxisBinding>;

inline constexpr std::size_t inputActionCount =
    static_cast<std::size_t>(InputAction::Count);

struct InputBindings {
    std::array<std::vector<InputBinding>, inputActionCount> actions;

    [[nodiscard]] std::vector<InputBinding>& forAction(InputAction action);
    [[nodiscard]] const std::vector<InputBinding>& forAction(InputAction action) const;

    bool operator==(const InputBindings&) const = default;
};

enum class BindingDeviceClass {
    Keyboard,
    Gamepad,
};

[[nodiscard]] InputBindings defaultInputBindings();
[[nodiscard]] BindingDeviceClass bindingDeviceClass(const InputBinding& binding);
// Short human-readable label, e.g. "W", "Pad dpup", or "Pad lefty-".
[[nodiscard]] std::string bindingDisplayName(const InputBinding& binding);
// One display string for every binding of an action, joined with " / ";
// "Unbound" when empty.
[[nodiscard]] std::string actionBindingsDisplay(
    const InputBindings& bindings,
    InputAction action);
// Rebinds `action`: bindings identical to `candidate` are removed from every
// action (no duplicates), and the action's bindings of the candidate's exact
// kind (keyboard / pad button / pad axis) are replaced by the candidate, so
// rebinding a d-pad button keeps an existing stick binding and vice versa.
void assignBinding(
    InputBindings& bindings,
    InputAction action,
    const InputBinding& candidate);
[[nodiscard]] std::string_view inputActionName(InputAction action);
[[nodiscard]] InputAction inputActionFromName(std::string_view name);
[[nodiscard]] std::string_view axisDirectionName(AxisDirection direction);
[[nodiscard]] AxisDirection axisDirectionFromName(std::string_view name);
[[nodiscard]] bool isKnownGamepadButtonName(std::string_view name);
[[nodiscard]] bool isKnownGamepadAxisName(std::string_view name);

} // namespace sokoban
