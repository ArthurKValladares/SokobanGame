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

bool editorOnlyAction(InputAction action)
{
    return action == InputAction::EditorReplaceTile ||
        action == InputAction::EditorDeleteTile ||
        action == InputAction::EditorMoveTile;
}

bool actionsShareContext(InputAction left, InputAction right)
{
    if (left == right) {
        return true;
    }
    // Editor modifiers may intentionally reuse gameplay/menu controls because
    // document editing and gameplay are mutually exclusive. Undo and Back are
    // also active while editing, so they still conflict with both groups.
    const bool globallyActive = [](InputAction action) {
        return action == InputAction::Undo || action == InputAction::MenuBack;
    }(left) || [](InputAction action) {
        return action == InputAction::Undo || action == InputAction::MenuBack;
    }(right);
    return globallyActive || editorOnlyAction(left) == editorOnlyAction(right);
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

BindingDeviceClass bindingDeviceClass(const InputBinding& binding)
{
    return std::holds_alternative<KeyboardBinding>(binding)
        ? BindingDeviceClass::Keyboard
        : BindingDeviceClass::Gamepad;
}

std::string bindingDisplayName(const InputBinding& binding)
{
    if (const KeyboardBinding* key = std::get_if<KeyboardBinding>(&binding)) {
        return key->scancode;
    }
    if (const GamepadButtonBinding* button = std::get_if<GamepadButtonBinding>(&binding)) {
        return "Pad " + button->button;
    }
    const GamepadAxisBinding& axis = std::get<GamepadAxisBinding>(binding);
    return "Pad " + axis.axis +
        (axis.direction == AxisDirection::Negative ? "-" : "+");
}

std::string actionBindingsDisplay(const InputBindings& bindings, InputAction action)
{
    std::string result;
    for (const InputBinding& binding : bindings.forAction(action)) {
        if (!result.empty()) {
            result += " / ";
        }
        result += bindingDisplayName(binding);
    }
    return result.empty() ? "Unbound" : result;
}

std::string actionBindingsDisplay(
    const InputBindings& bindings,
    InputAction action,
    BindingDeviceClass deviceClass)
{
    std::string result;
    for (const InputBinding& binding : bindings.forAction(action)) {
        if (bindingDeviceClass(binding) != deviceClass) {
            continue;
        }
        if (!result.empty()) {
            result += " / ";
        }
        result += bindingDisplayName(binding);
    }
    return result.empty() ? "Unbound" : result;
}

void assignBinding(
    InputBindings& bindings,
    InputAction action,
    const InputBinding& candidate)
{
    const std::size_t targetIndex = actionIndex(action);
    const std::vector<InputBinding> displaced = bindings.actions[targetIndex];
    for (std::size_t i = 0; i < inputActionCount; ++i) {
        const InputAction existingAction = static_cast<InputAction>(i);
        if (actionsShareContext(action, existingAction)) {
            std::erase(bindings.actions[i], candidate);
            if (i != targetIndex && bindings.actions[i].empty()) {
                const auto replacement = std::ranges::find_if(
                    displaced,
                    [&](const InputBinding& binding) {
                        return binding.index() == candidate.index() &&
                            binding != candidate;
                    });
                if (replacement != displaced.end()) {
                    bindings.actions[i].push_back(*replacement);
                }
            }
        }
    }
    std::vector<InputBinding>& target = bindings.forAction(action);
    std::erase_if(target, [&](const InputBinding& existing) {
        return existing.index() == candidate.index();
    });
    target.push_back(candidate);
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
    bindings.forAction(InputAction::ShowTopDownView) = {
        KeyboardBinding { "T" },
    };
    bindings.forAction(InputAction::ShowOverworldMap) = {
        KeyboardBinding { "Tab" },
        GamepadAxisBinding {
            "lefttrigger", AxisDirection::Positive, 0.5f },
    };
    bindings.forAction(InputAction::MenuBack) = {
        KeyboardBinding { "Escape" },
        GamepadButtonBinding { "start" },
    };
    bindings.forAction(InputAction::MenuConfirm) = {
        KeyboardBinding { "Space" },
        GamepadButtonBinding { "south" },
    };
    bindings.forAction(InputAction::EditorReplaceTile) = {
        KeyboardBinding { "R" },
    };
    bindings.forAction(InputAction::EditorDeleteTile) = {
        KeyboardBinding { "D" },
    };
    bindings.forAction(InputAction::EditorMoveTile) = {
        KeyboardBinding { "M" },
    };
    bindings.forAction(InputAction::PreviewScreen) = {
        KeyboardBinding { "V" },
        GamepadButtonBinding { "rightshoulder" },
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
    case InputAction::ShowTopDownView: return "showTopDownView";
    case InputAction::ShowOverworldMap: return "showOverworldMap";
    case InputAction::MenuBack: return "menuBack";
    case InputAction::MenuConfirm: return "menuConfirm";
    case InputAction::EditorReplaceTile: return "editorReplaceTile";
    case InputAction::EditorDeleteTile: return "editorDeleteTile";
    case InputAction::EditorMoveTile: return "editorMoveTile";
    case InputAction::PreviewScreen: return "previewScreen";
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
