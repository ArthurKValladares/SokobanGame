#include "engine/InputBindings.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

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

bool actionsShareContext(InputAction left, InputAction right)
{
    if (left == right) {
        return true;
    }
    // Editor controls may intentionally reuse gameplay/menu controls because
    // document editing and gameplay are mutually exclusive. Global actions
    // are live in both, so they conflict with both groups.
    const InputActionContext leftContext = inputActionContext(left);
    const InputActionContext rightContext = inputActionContext(right);
    return leftContext == InputActionContext::Global ||
        rightContext == InputActionContext::Global ||
        leftContext == rightContext;
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

InputActionContext inputActionContext(InputAction action)
{
    switch (action) {
    case InputAction::Undo:
    case InputAction::MenuBack:
    case InputAction::EditorPlayDraft:
        return InputActionContext::Global;
    case InputAction::EditorReplaceTile:
    case InputAction::EditorDeleteTile:
    case InputAction::EditorMoveTile:
    case InputAction::EditorPickTile:
    case InputAction::EditorStraightLine:
    case InputAction::EditorRedo:
    case InputAction::EditorSave:
    case InputAction::EditorPlayFromCursor:
    case InputAction::EditorLayerUp:
    case InputAction::EditorLayerDown:
    case InputAction::EditorToggleLayerLock:
    case InputAction::EditorCycleTool:
    case InputAction::EditorGizmoTranslate:
    case InputAction::EditorGizmoRotate:
    case InputAction::EditorGizmoScale:
    case InputAction::EditorRecentTile1:
    case InputAction::EditorRecentTile2:
    case InputAction::EditorRecentTile3:
    case InputAction::EditorRecentTile4:
    case InputAction::EditorRecentTile5:
    case InputAction::EditorRecentTile6:
    case InputAction::EditorRecentTile7:
    case InputAction::EditorRecentTile8:
    case InputAction::EditorRecentTile9:
        return InputActionContext::Editor;
    case InputAction::MoveUp:
    case InputAction::MoveDown:
    case InputAction::MoveLeft:
    case InputAction::MoveRight:
    case InputAction::Restart:
    case InputAction::ShowTopDownView:
    case InputAction::ShowOverworldMap:
    case InputAction::MenuConfirm:
    case InputAction::PreviewScreen:
    case InputAction::CycleHero:
        return InputActionContext::Gameplay;
    case InputAction::Count:
        break;
    }
    throw std::invalid_argument("invalid input action");
}

std::string_view keyModifierName(KeyModifier modifier)
{
    switch (modifier) {
    case keyModifierCtrl: return "ctrl";
    case keyModifierShift: return "shift";
    case keyModifierAlt: return "alt";
    default: break;
    }
    throw std::invalid_argument("invalid key modifier");
}

std::string keyModifierPrefix(std::uint8_t modifiers)
{
    std::string prefix;
    if ((modifiers & keyModifierCtrl) != 0U) {
        prefix += "Ctrl+";
    }
    if ((modifiers & keyModifierShift) != 0U) {
        prefix += "Shift+";
    }
    if ((modifiers & keyModifierAlt) != 0U) {
        prefix += "Alt+";
    }
    return prefix;
}

std::string bindingDisplayName(const InputBinding& binding)
{
    if (const KeyboardBinding* key = std::get_if<KeyboardBinding>(&binding)) {
        return keyModifierPrefix(key->modifiers) + key->scancode;
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
    bindings.forAction(InputAction::CycleHero) = {
        KeyboardBinding { "Q" },
        GamepadButtonBinding { "leftshoulder" },
    };
    bindings.forAction(InputAction::EditorPickTile) = {
        KeyboardBinding { "Left Alt" },
        KeyboardBinding { "Right Alt" },
    };
    bindings.forAction(InputAction::EditorStraightLine) = {
        KeyboardBinding { "Left Shift" },
        KeyboardBinding { "Right Shift" },
    };
    bindings.forAction(InputAction::EditorRedo) = {
        KeyboardBinding { "Y" },
        KeyboardBinding { "Z", keyModifierCtrl | keyModifierShift },
    };
    bindings.forAction(InputAction::EditorSave) = {
        KeyboardBinding { "S", keyModifierCtrl },
    };
    bindings.forAction(InputAction::EditorPlayDraft) = {
        KeyboardBinding { "F5" },
    };
    bindings.forAction(InputAction::EditorPlayFromCursor) = {
        KeyboardBinding { "F5", keyModifierShift },
    };
    bindings.forAction(InputAction::EditorLayerUp) = {
        KeyboardBinding { "PageUp" },
    };
    bindings.forAction(InputAction::EditorLayerDown) = {
        KeyboardBinding { "PageDown" },
    };
    bindings.forAction(InputAction::EditorToggleLayerLock) = {
        KeyboardBinding { "L" },
    };
    bindings.forAction(InputAction::EditorCycleTool) = {
        KeyboardBinding { "Tab" },
    };
    // Rotate shares R with Replace Tile on purpose: the gizmo exists only in
    // the Decorations tool and Replace only in the Tiles tool.
    bindings.forAction(InputAction::EditorGizmoTranslate) = {
        KeyboardBinding { "T" },
    };
    bindings.forAction(InputAction::EditorGizmoRotate) = {
        KeyboardBinding { "R" },
    };
    bindings.forAction(InputAction::EditorGizmoScale) = {
        KeyboardBinding { "S" },
    };
    for (int slot = 0; slot < editorRecentTileActionCount; ++slot) {
        bindings.forAction(editorRecentTileAction(slot)) = {
            KeyboardBinding { std::to_string(slot + 1) },
        };
    }
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
    case InputAction::CycleHero: return "cycleHero";
    case InputAction::EditorPickTile: return "editorPickTile";
    case InputAction::EditorStraightLine: return "editorStraightLine";
    case InputAction::EditorRedo: return "editorRedo";
    case InputAction::EditorSave: return "editorSave";
    case InputAction::EditorPlayDraft: return "editorPlayDraft";
    case InputAction::EditorPlayFromCursor: return "editorPlayFromCursor";
    case InputAction::EditorLayerUp: return "editorLayerUp";
    case InputAction::EditorLayerDown: return "editorLayerDown";
    case InputAction::EditorToggleLayerLock: return "editorToggleLayerLock";
    case InputAction::EditorCycleTool: return "editorCycleTool";
    case InputAction::EditorGizmoTranslate: return "editorGizmoTranslate";
    case InputAction::EditorGizmoRotate: return "editorGizmoRotate";
    case InputAction::EditorGizmoScale: return "editorGizmoScale";
    case InputAction::EditorRecentTile1: return "editorRecentTile1";
    case InputAction::EditorRecentTile2: return "editorRecentTile2";
    case InputAction::EditorRecentTile3: return "editorRecentTile3";
    case InputAction::EditorRecentTile4: return "editorRecentTile4";
    case InputAction::EditorRecentTile5: return "editorRecentTile5";
    case InputAction::EditorRecentTile6: return "editorRecentTile6";
    case InputAction::EditorRecentTile7: return "editorRecentTile7";
    case InputAction::EditorRecentTile8: return "editorRecentTile8";
    case InputAction::EditorRecentTile9: return "editorRecentTile9";
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
