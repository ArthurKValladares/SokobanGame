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

[[nodiscard]] InputBindings defaultInputBindings();
[[nodiscard]] std::string_view inputActionName(InputAction action);
[[nodiscard]] InputAction inputActionFromName(std::string_view name);
[[nodiscard]] std::string_view axisDirectionName(AxisDirection direction);
[[nodiscard]] AxisDirection axisDirectionFromName(std::string_view name);
[[nodiscard]] bool isKnownGamepadButtonName(std::string_view name);
[[nodiscard]] bool isKnownGamepadAxisName(std::string_view name);

} // namespace sokoban
