#pragma once

#include "engine/Math.hpp"
#include "engine/SettingsTypes.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace sokoban {

class UiContext;

enum class OptionsMenuPage {
    Main,
    Graphics,
    Audio,
    Controls,
    QuitConfirmation,
};

enum class OptionsMenuRowId {
    Graphics,
    Audio,
    Controls,
    LevelSelect,
    ExitToTitle,
    Quit,
    AntiAliasing,
    RenderScalePreset,
    CustomRenderScale,
    AmbientOcclusion,
    Display,
    MasterVolume,
    MusicVolume,
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Undo,
    Restart,
    ResetBindings,
    Back,
    CancelQuit,
    ConfirmQuit,
};

enum class OptionsMenuRowKind {
    Button,
    SegmentedChoice,
    StepperChoice,
    Toggle,
    Slider,
    CustomRenderScale,
    Binding,
};

enum class OptionsMenuRowTone {
    Normal,
    Accent,
    Danger,
};

struct OptionsMenuChoice {
    int value = 0;
    std::string_view label;
};

// Pure presentation data. The renderer consumes these rows without deciding
// what a setting means or mutating the authoritative settings value.
struct OptionsMenuRow {
    OptionsMenuRowId id {};
    OptionsMenuRowKind kind = OptionsMenuRowKind::Button;
    std::string_view label;
    std::span<const OptionsMenuChoice> choices;
    int choiceValue = 0;
    float sliderValue = 0.0f;
    bool toggleValue = false;
    bool enabled = true;
    OptionsMenuRowTone tone = OptionsMenuRowTone::Normal;
    bool flexibleSpaceBefore = false;
    bool dividerBefore = false;
};

struct OptionsMenuState {
    bool open = false;
    bool allowTitleExit = false;
    bool allowLevelSelect = false;
    OptionsMenuPage page = OptionsMenuPage::Main;
    int selectedRow = 0;
    std::optional<InputAction> capturingAction;
    std::optional<int> customRenderScalePreview;

    bool operator==(const OptionsMenuState&) const = default;
};

struct OptionsMenuInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
};

namespace options {

struct SettingsChanged {
    UserSettings settings;
};
struct Quit {};
struct ExitToTitle {};
struct OpenLevelSelect {};

namespace intent {

struct Open {
    bool allowTitleExit = false;
    bool allowLevelSelect = false;
};
struct Close {};
struct Back {};
struct RequestQuitConfirmation {};
struct Navigate {
    int direction = 0;
};
struct AdjustSelected {
    int direction = 0;
};
struct ActivateSelected {};
struct ActivateRow {
    OptionsMenuRowId row {};
};
struct SelectChoice {
    OptionsMenuRowId row {};
    int value = 0;
};
struct SetToggle {
    OptionsMenuRowId row {};
    bool value = false;
};
struct SetSlider {
    OptionsMenuRowId row {};
    float value = 0.0f;
    bool commit = true;
};
struct ProvideBinding {
    InputBinding binding;
};

} // namespace intent
} // namespace options

using OptionsAction = std::variant<
    options::SettingsChanged,
    options::Quit,
    options::ExitToTitle,
    options::OpenLevelSelect>;

using OptionsMenuIntent = std::variant<
    options::intent::Open,
    options::intent::Close,
    options::intent::Back,
    options::intent::RequestQuitConfirmation,
    options::intent::Navigate,
    options::intent::AdjustSelected,
    options::intent::ActivateSelected,
    options::intent::ActivateRow,
    options::intent::SelectChoice,
    options::intent::SetToggle,
    options::intent::SetSlider,
    options::intent::ProvideBinding>;

struct OptionsMenuReduction {
    OptionsMenuState state;
    std::optional<OptionsAction> action;
};

[[nodiscard]] std::vector<OptionsMenuRow> optionsMenuRows(
    const OptionsMenuState& state,
    const UserSettings& settings);

// Pure menu state/settings transition. It has no UiContext, SDL, renderer, or
// persistence dependency and returns changed settings as an explicit action.
[[nodiscard]] OptionsMenuReduction reduceOptionsMenu(
    const OptionsMenuState& state,
    const UserSettings& settings,
    const OptionsMenuIntent& intent);

// State-only controller. UserSettings remains owned by PlayerProfile and is
// supplied for each reduction.
class OptionsMenu {
public:
    using Page = OptionsMenuPage;

    void open(
        bool allowTitleExit = false,
        bool allowLevelSelect = false);
    void close();
    void back();
    void requestQuitConfirmation();

    [[nodiscard]] std::optional<OptionsAction> handleInput(
        const UserSettings& settings,
        const OptionsMenuInput& input);
    [[nodiscard]] std::optional<OptionsAction> dispatch(
        const UserSettings& settings,
        const OptionsMenuIntent& intent);
    [[nodiscard]] std::optional<OptionsAction> provideBindingCandidate(
        const UserSettings& settings,
        const InputBinding& candidate);

    [[nodiscard]] bool isOpen() const { return state_.open; }
    [[nodiscard]] Page page() const { return state_.page; }
    [[nodiscard]] int selectedRow() const { return state_.selectedRow; }
    [[nodiscard]] bool capturingBinding() const {
        return state_.capturingAction.has_value();
    }
    [[nodiscard]] std::optional<InputAction> capturingAction() const {
        return state_.capturingAction;
    }
    [[nodiscard]] const OptionsMenuState& state() const { return state_; }

private:
    OptionsMenuState state_;
};

// Stateless renderer-facing adapter. It consumes declarative rows and emits
// semantic intents; it never owns or mutates settings policy.
class OptionsMenuView {
public:
    [[nodiscard]] std::optional<OptionsMenuIntent> draw(
        UiContext& ui,
        Vec2 viewport,
        const OptionsMenuState& state,
        const UserSettings& settings) const;
};

} // namespace sokoban
