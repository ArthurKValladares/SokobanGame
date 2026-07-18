#include "engine/InputBindings.hpp"

#include <algorithm>
#include <stdexcept>

namespace sokoban {
namespace {

std::size_t actionIndex(InputAction action)
{
    const std::size_t index = static_cast<std::size_t>(action);
    if (index >= inputActionCount) {
        throw std::out_of_range("invalid input action");
    }
    return index;
}

} // namespace

std::vector<InputBinding>& InputBindings::forAction(InputAction action)
{
    return actions[actionIndex(action)];
}

const std::vector<InputBinding>& InputBindings::forAction(InputAction action) const
{
    return actions[actionIndex(action)];
}

InputBindings defaultInputBindings()
{
    InputBindings bindings;
    bindings.forAction(InputAction::MoveUp) = {
        KeyboardBinding { "W" },
        GamepadButtonBinding { "dpup" },
        GamepadAxisBinding { "lefty", AxisDirection::Negative },
    };
    bindings.forAction(InputAction::MoveDown) = {
        KeyboardBinding { "S" },
        GamepadButtonBinding { "dpdown" },
        GamepadAxisBinding { "lefty", AxisDirection::Positive },
    };
    bindings.forAction(InputAction::MoveLeft) = {
        KeyboardBinding { "A" },
        GamepadButtonBinding { "dpleft" },
        GamepadAxisBinding { "leftx", AxisDirection::Negative },
    };
    bindings.forAction(InputAction::MoveRight) = {
        KeyboardBinding { "D" },
        GamepadButtonBinding { "dpright" },
        GamepadAxisBinding { "leftx", AxisDirection::Positive },
    };
    bindings.forAction(InputAction::Undo) = {
        KeyboardBinding { "Z" },
        GamepadButtonBinding { "west" },
    };
    bindings.forAction(InputAction::Restart) = {
        KeyboardBinding { "R" },
        GamepadButtonBinding { "north" },
    };
    bindings.forAction(InputAction::MenuBack) = {
        KeyboardBinding { "Escape" },
        GamepadButtonBinding { "start" },
    };
    return bindings;
}

std::string_view inputActionName(InputAction action)
{
    switch (action) {
    case InputAction::MoveUp: return "moveUp";
    case InputAction::MoveDown: return "moveDown";
    case InputAction::MoveLeft: return "moveLeft";
    case InputAction::MoveRight: return "moveRight";
    case InputAction::Undo: return "undo";
    case InputAction::Restart: return "restart";
    case InputAction::MenuBack: return "menuBack";
    case InputAction::Count: break;
    }
    throw std::invalid_argument("invalid input action");
}

InputAction inputActionFromName(std::string_view name)
{
    for (std::size_t i = 0; i < inputActionCount; ++i) {
        const InputAction action = static_cast<InputAction>(i);
        if (inputActionName(action) == name) {
            return action;
        }
    }
    throw std::invalid_argument("unknown input action '" + std::string(name) + "'");
}

std::string_view axisDirectionName(AxisDirection direction)
{
    return direction == AxisDirection::Negative ? "negative" : "positive";
}

AxisDirection axisDirectionFromName(std::string_view name)
{
    if (name == "negative") {
        return AxisDirection::Negative;
    }
    if (name == "positive") {
        return AxisDirection::Positive;
    }
    throw std::invalid_argument("unknown axis direction '" + std::string(name) + "'");
}

bool isKnownGamepadButtonName(std::string_view name)
{
    constexpr std::array names {
        std::string_view("south"), std::string_view("east"),
        std::string_view("west"), std::string_view("north"),
        std::string_view("a"), std::string_view("b"),
        std::string_view("x"), std::string_view("y"),
        std::string_view("back"), std::string_view("guide"),
        std::string_view("start"), std::string_view("leftstick"),
        std::string_view("rightstick"), std::string_view("leftshoulder"),
        std::string_view("rightshoulder"), std::string_view("dpup"),
        std::string_view("dpdown"), std::string_view("dpleft"),
        std::string_view("dpright"), std::string_view("misc1"),
        std::string_view("paddle1"), std::string_view("paddle2"),
        std::string_view("paddle3"), std::string_view("paddle4"),
        std::string_view("touchpad"), std::string_view("misc2"),
        std::string_view("misc3"), std::string_view("misc4"),
        std::string_view("misc5"), std::string_view("misc6"),
    };
    return std::ranges::find(names, name) != names.end();
}

bool isKnownGamepadAxisName(std::string_view name)
{
    constexpr std::array names {
        std::string_view("leftx"), std::string_view("lefty"),
        std::string_view("rightx"), std::string_view("righty"),
        std::string_view("lefttrigger"), std::string_view("righttrigger"),
    };
    return std::ranges::find(names, name) != names.end();
}

} // namespace sokoban
