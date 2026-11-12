#include "engine/InputBindings.hpp"
#include "engine/FrameArena.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

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

constexpr std::array menuRecoveryKeys {
    std::pair { InputAction::MoveUp, std::string_view("Up") },
    std::pair { InputAction::MoveDown, std::string_view("Down") },
    std::pair { InputAction::MoveLeft, std::string_view("Left") },
    std::pair { InputAction::MoveRight, std::string_view("Right") },
    std::pair { InputAction::MenuConfirm, std::string_view("Return") },
};

bool needsMenuBinding(InputAction action)
{
    return std::ranges::any_of(menuRecoveryKeys, [action](const auto& entry) {
        return entry.first == action;
    });
}

bool usableBinding(const InputBinding& binding)
{
    if (const auto* key = std::get_if<KeyboardBinding>(&binding)) {
        return (key->modifiers & ~keyModifierAll) == 0U &&
            key->scancode.find('\0') == std::string::npos &&
            SDL_GetScancodeFromName(key->scancode.c_str()) != SDL_SCANCODE_UNKNOWN;
    }
    if (const auto* button = std::get_if<GamepadButtonBinding>(&binding)) {
        return isKnownGamepadButtonName(button->button);
    }
    const auto& axis = std::get<GamepadAxisBinding>(binding);
    return isKnownGamepadAxisName(axis.axis) && std::isfinite(axis.threshold) &&
        axis.threshold >= 0.1f && axis.threshold <= 1.0f &&
        (axis.direction == AxisDirection::Positive ||
            (axis.direction == AxisDirection::Negative &&
                axis.axis != "lefttrigger" && axis.axis != "righttrigger"));
}

bool hasBindingForDevice(const std::vector<InputBinding>& bindings, BindingDeviceClass device)
{
    return std::ranges::any_of(bindings, [device](const InputBinding& binding) {
        return bindingDeviceClass(binding) == device && usableBinding(binding);
    });
}

std::string_view faceButtonName(std::string_view button)
{
    if (button == "a") return "south";
    if (button == "b") return "east";
    if (button == "x") return "west";
    if (button == "y") return "north";
    return button;
}

bool sameControl(const InputBinding& left, const InputBinding& right)
{
    if (left.index() != right.index()) return false;
    if (const auto* key = std::get_if<KeyboardBinding>(&left)) {
        const auto& other = std::get<KeyboardBinding>(right);
        if (key->modifiers != other.modifiers) return false;
        const auto scancode = SDL_GetScancodeFromName(key->scancode.c_str());
        return scancode != SDL_SCANCODE_UNKNOWN &&
            scancode == SDL_GetScancodeFromName(other.scancode.c_str());
    }
    if (const auto* button = std::get_if<GamepadButtonBinding>(&left)) {
        return faceButtonName(button->button) ==
            faceButtonName(std::get<GamepadButtonBinding>(right).button);
    }
    const auto& axis = std::get<GamepadAxisBinding>(left);
    const auto& other = std::get<GamepadAxisBinding>(right);
    return axis.axis == other.axis && axis.direction == other.direction;
}

bool controlAvailable(const InputBindings& bindings, InputAction action, const InputBinding& candidate)
{
    return std::ranges::none_of(inputActionDefinitions, [&](const auto& definition) {
        return definition.action != action && inputActionsConflict(action, definition.action) &&
            std::ranges::any_of(bindings.forAction(definition.action), [&](const InputBinding& binding) {
                return sameControl(binding, candidate);
            });
    });
}

bool hasMenuBinding(const InputBindings& bindings, InputAction action, BindingDeviceClass device)
{
    return std::ranges::any_of(bindings.forAction(action), [&](const InputBinding& candidate) {
        if (bindingDeviceClass(candidate) != device || !usableBinding(candidate)) return false;
        return std::ranges::none_of(inputActionDefinitions, [&](const auto& definition) {
            const InputAction other = definition.action;
            return other != action && (needsMenuBinding(other) || other == InputAction::MenuBack) &&
                std::ranges::any_of(bindings.forAction(other), [&](const InputBinding& binding) {
                    return sameControl(candidate, binding);
                });
        });
    });
}

bool addMenuFallback(InputBindings& bindings, InputAction action,
    BindingDeviceClass device, std::string_view recoveryKey)
{
    const auto addIfAvailable = [&](InputBinding candidate) {
        if (!usableBinding(candidate) || !controlAvailable(bindings, action, candidate)) return false;
        auto& actionBindings = bindings.forAction(action);
        const auto firstOnDevice = std::ranges::find_if(actionBindings, [device](const InputBinding& binding) {
            return bindingDeviceClass(binding) == device;
        });
        actionBindings.insert(firstOnDevice, std::move(candidate));
        return true;
    };
    static const InputBindings defaults = defaultInputBindings();
    for (const auto& binding : defaults.forAction(action)) {
        if (bindingDeviceClass(binding) == device && addIfAvailable(binding)) return true;
    }
    if (device == BindingDeviceClass::Keyboard) {
        if (addIfAvailable(KeyboardBinding { std::string(recoveryKey) })) return true;
        for (const char key : std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) {
            if (addIfAvailable(KeyboardBinding { std::string(1, key) })) return true;
        }
        return false;
    }
    for (const char* button : { "east", "south", "west", "north", "dpup", "dpdown", "dpleft",
             "dpright", "leftshoulder", "rightshoulder", "leftstick", "rightstick" }) {
        if (addIfAvailable(GamepadButtonBinding { button })) return true;
    }
    for (const char* axis : { "leftx", "lefty", "rightx", "righty" }) {
        for (const AxisDirection direction : { AxisDirection::Negative, AxisDirection::Positive }) {
            if (addIfAvailable(GamepadAxisBinding { axis, direction })) return true;
        }
    }
    return addIfAvailable(GamepadAxisBinding { "lefttrigger" }) ||
        addIfAvailable(GamepadAxisBinding { "righttrigger" });
}

} // namespace

bool inputActionsConflict(InputAction left, InputAction right)
{
    if (left == right) {
        return true;
    }
    // The router uses tile rotation while placing a tile or tile decoration,
    // and gizmo rotation when editing a placed decoration. Their shared
    // default key and any shared custom key are intentional.
    if ((left == InputAction::EditorRotateTile && right == InputAction::EditorGizmoRotate) ||
        (right == InputAction::EditorRotateTile && left == InputAction::EditorGizmoRotate)) {
        return false;
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

const InputActionDefinition& inputActionDefinition(InputAction action)
{
    return inputActionDefinitions[actionIndex(action)];
}

InputActionContext inputActionContext(InputAction action)
{
    return inputActionDefinition(action).context;
}

std::optional<InputAction> findInputAction(std::string_view name)
{
    const auto normalize = [](std::string_view value) {
        std::string result;
        for (unsigned char c : value) {
            if (std::isspace(c) || c == '_' || c == '-' || c == '/') continue;
            result += static_cast<char>(std::tolower(c));
        }
        return result;
    };
    const std::string normalized = normalize(name);
    if (normalized.empty()) return std::nullopt;
    for (const auto& definition : inputActionDefinitions) {
        if (normalize(definition.name) == normalized || normalize(definition.label) == normalized)
            return definition.action;
        std::string_view aliases = definition.aliases;
        while (!aliases.empty()) {
            const std::size_t end = aliases.find('|');
            if (normalize(aliases.substr(0, end)) == normalized) return definition.action;
            if (end == std::string_view::npos) break;
            aliases.remove_prefix(end + 1);
        }
    }
    return std::nullopt;
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

std::string_view actionBindingsDisplay(
    const InputBindings& bindings, InputAction action,
    BindingDeviceClass deviceClass, FrameArena& arena)
{
    const auto visitPieces = [&](auto append) {
        bool first = true;
        for (const auto& binding : bindings.forAction(action)) {
            if (bindingDeviceClass(binding) != deviceClass) {
                continue;
            }
            if (!first) {
                append(" / ");
            }
            first = false;
            if (const auto* key = std::get_if<KeyboardBinding>(&binding)) {
                if ((key->modifiers & keyModifierCtrl) != 0U) { append("Ctrl+"); }
                if ((key->modifiers & keyModifierShift) != 0U) { append("Shift+"); }
                if ((key->modifiers & keyModifierAlt) != 0U) { append("Alt+"); }
                append(key->scancode);
            } else if (const auto* button = std::get_if<GamepadButtonBinding>(&binding)) {
                append("Pad ");
                append(button->button);
            } else {
                const auto& axis = std::get<GamepadAxisBinding>(binding);
                append("Pad ");
                append(axis.axis);
                append(axis.direction == AxisDirection::Negative ? "-" : "+");
            }
        }
    };
    std::size_t bytes = 0;
    visitPieces([&](std::string_view piece) { bytes += piece.size(); });
    if (bytes == 0) {
        return "Unbound";
    }
    char* const text = arena.allocateUninitialized<char>(bytes);
    if (!text) {
        return {};
    }
    std::size_t offset = 0;
    visitPieces([&](std::string_view piece) {
        std::memcpy(text + offset, piece.data(), piece.size());
        offset += piece.size();
    });
    return { text, bytes };
}

void assignBinding(
    InputBindings& bindings,
    InputAction action,
    const InputBinding& candidate)
{
    InputBindings updated = bindings;
    std::vector<InputBinding>& target = updated.forAction(action);
    const std::vector<InputBinding> displaced = target;
    std::erase_if(target, [&](const InputBinding& existing) {
        return existing.index() == candidate.index();
    });
    target.push_back(candidate);

    const BindingDeviceClass device = bindingDeviceClass(candidate);
    for (std::size_t i = 0; i < inputActionCount; ++i) {
        const InputAction existingAction = static_cast<InputAction>(i);
        if (existingAction == action || !inputActionsConflict(action, existingAction)) continue;

        auto& existing = updated.actions[i];
        const bool menuAction = needsMenuBinding(existingAction);
        const auto removed = std::erase_if(existing, [&](const InputBinding& binding) {
            return menuAction ? sameControl(binding, candidate) : binding == candidate;
        });
        if (removed == 0 || (!existing.empty() &&
                (!menuAction || hasBindingForDevice(existing, device)))) {
            continue;
        }
        const auto replacement = std::ranges::find_if(displaced, [&](const InputBinding& binding) {
            return binding.index() == candidate.index() &&
                (menuAction ? !sameControl(binding, candidate) &&
                                  controlAvailable(updated, existingAction, binding)
                            : binding != candidate);
        });
        if (replacement != displaced.end()) {
            existing.push_back(*replacement);
        }
    }
    if (restoreMenuBindings(updated)) {
        bindings = std::move(updated);
    }
}

bool restoreMenuBindings(InputBindings& bindings)
{
    for (const auto& [action, recoveryKey] : menuRecoveryKeys) {
        for (const auto device : { BindingDeviceClass::Keyboard, BindingDeviceClass::Gamepad }) {
            if (!hasMenuBinding(bindings, action, device) &&
                !addMenuFallback(bindings, action, device, recoveryKey)) {
                return false;
            }
        }
    }
    return true;
}

InputBindings defaultInputBindings()
{
    InputBindings bindings;
#define SOKOBAN_INPUT_ACTION(id, name, label, aliases, context, group, order, ...) \
    bindings.forAction(InputAction::id) = { __VA_ARGS__ };
#include "engine/InputActions.def"
#undef SOKOBAN_INPUT_ACTION
    return bindings;
}

std::string_view inputActionName(InputAction action)
{
    return inputActionDefinition(action).name;
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
