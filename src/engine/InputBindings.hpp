#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sokoban {
class FrameArena;

enum class InputAction : uint8_t {
#define SOKOBAN_INPUT_ACTION(id, ...) id,
#include "engine/InputActions.def"
#undef SOKOBAN_INPUT_ACTION
    Count,
};

inline constexpr int editorRecentTileActionCount = 9;
[[nodiscard]] constexpr InputAction editorRecentTileAction(int slot)
{
    return static_cast<InputAction>(
        static_cast<int>(InputAction::EditorRecentTile1) + slot);
}

enum class AxisDirection : uint8_t {
    Negative,
    Positive,
};

// Modifier keys a keyboard chord requires. Left and right keys are
// equivalent.
enum KeyModifier : std::uint8_t {
    keyModifierNone = 0,
    keyModifierCtrl = 1U << 0U,
    keyModifierShift = 1U << 1U,
    keyModifierAlt = 1U << 2U,
    keyModifierAll = keyModifierCtrl | keyModifierShift | keyModifierAlt,
};

struct KeyboardBinding {
    // SDL scancode name, such as "S" or "Left Alt".
    std::string scancode;
    // KeyModifier bits that must be held. When several bindings on the same
    // key are satisfied, only the one requiring the most modifiers fires, so
    // Ctrl+S does not also press S and Shift+F5 does not also press F5.
    std::uint8_t modifiers = keyModifierNone;

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

enum class BindingDeviceClass : uint8_t {
    Keyboard,
    Gamepad,
};

[[nodiscard]] InputBindings defaultInputBindings();
[[nodiscard]] BindingDeviceClass bindingDeviceClass(const InputBinding& binding);
// Short human-readable label, e.g. "W", "Ctrl+S", "Pad dpup", or
// "Pad lefty-".
[[nodiscard]] std::string bindingDisplayName(const InputBinding& binding);
// One display string for every binding of an action, joined with " / ";
// "Unbound" when empty.
[[nodiscard]] std::string actionBindingsDisplay(
    const InputBindings& bindings,
    InputAction action);
// One display string for bindings from a single device class; "Unbound" when
// the action has no binding for that class.
[[nodiscard]] std::string actionBindingsDisplay(
    const InputBindings& bindings,
    InputAction action,
    BindingDeviceClass deviceClass);
[[nodiscard]] std::string_view actionBindingsDisplay(
    const InputBindings& bindings,
    InputAction action,
    BindingDeviceClass deviceClass,
    FrameArena& arena);
// Rebinds `action`: bindings identical to `candidate` are removed from every
// action active in the same input context, and the action's bindings of the
// candidate's exact kind (keyboard / pad button / pad axis) are replaced by
// the candidate. Editor-only modifiers may intentionally reuse gameplay keys;
// an action that would otherwise become empty receives the target's displaced
// same-kind binding. Rebinding a d-pad button still keeps an existing stick
// binding and vice versa.
void assignBinding(
    InputBindings& bindings,
    InputAction action,
    const InputBinding& candidate);
// Which actions may share a binding: gameplay and menu actions never run
// while a document is being edited, and editor actions only run then. Undo,
// Back, and Play/Stop Draft are live in both.
enum class InputActionContext : uint8_t {
    Gameplay,
    Editor,
    Global,
};
enum class InputActionGroup : uint8_t {
    Controls,
    EditorEditing,
    EditorPlaytest,
    EditorRecentTiles,
    Hidden,
};

struct InputActionDefinition {
    InputAction action;
    std::string_view name;
    std::string_view label;
    std::string_view aliases;
    InputActionContext context;
    InputActionGroup group;
    int menuOrder;
};

inline constexpr std::array inputActionDefinitions {
#define SOKOBAN_INPUT_ACTION(id, name, label, aliases, context, group, order, ...) \
    InputActionDefinition { InputAction::id, name, label, aliases, \
        InputActionContext::context, InputActionGroup::group, order },
#include "engine/InputActions.def"
#undef SOKOBAN_INPUT_ACTION
};
static_assert(inputActionDefinitions.size() == inputActionCount);

[[nodiscard]] const InputActionDefinition& inputActionDefinition(InputAction action);
// Resolves saved names, controls-menu labels, and aliases for tutorial markup.
// Ignores case, whitespace, underscores, hyphens, and slashes.
[[nodiscard]] std::optional<InputAction> findInputAction(std::string_view name);
[[nodiscard]] InputActionContext inputActionContext(InputAction action);
// "Ctrl+Shift+" style prefix for a modifier mask; empty for none.
[[nodiscard]] std::string keyModifierPrefix(std::uint8_t modifiers);
[[nodiscard]] std::string_view keyModifierName(KeyModifier modifier);
[[nodiscard]] std::string_view inputActionName(InputAction action);
[[nodiscard]] InputAction inputActionFromName(std::string_view name);
[[nodiscard]] std::string_view axisDirectionName(AxisDirection direction);
[[nodiscard]] AxisDirection axisDirectionFromName(std::string_view name);
[[nodiscard]] bool isKnownGamepadButtonName(std::string_view name);
[[nodiscard]] bool isKnownGamepadAxisName(std::string_view name);

} // namespace sokoban
