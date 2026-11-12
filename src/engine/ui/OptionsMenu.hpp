#pragma once

#include "engine/Math.hpp"
#include "engine/SettingsTypes.hpp"
#include "engine/ui/UiScroll.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <cstdint>

namespace sokoban {

class UiContext;
class InputPromptCatalog;
struct GamepadPresentation;

enum class OptionsMenuPage : uint8_t {
    Main,
    Graphics,
    Audio,
    Controls,
    EditorControls,
    QuitConfirmation,
};

enum class OptionsMenuRowId : uint8_t {
    Graphics,
    Audio,
    Controls,
    ExitToTitle,
    Quit,
    AntiAliasing,
    Vsync,
    AllowTearing,
    FrameRateLimit,
    RenderScalePreset,
    CustomRenderScale,
    Exposure,
    AmbientOcclusion,
    AmbientOcclusionStrength,
    Display,
    MasterVolume,
    MusicVolume,
    BindingDevice,
#define SOKOBAN_INPUT_ACTION(id, ...) id,
#include "engine/InputActions.def"
#undef SOKOBAN_INPUT_ACTION
    EditorControls,
    EditorControlsSection,
    ResetBindings,
    Back,
    CancelQuit,
    ConfirmQuit,
    ConfirmInteract = MenuConfirm,
};

// The Editor Controls page shows one group of editor bindings at a time.
enum class EditorControlsSection : uint8_t {
    Editing,
    Playtest,
    RecentTiles,
};

enum class OptionsMenuRowKind : uint8_t {
    Button,
    SegmentedChoice,
    StepperChoice,
    Toggle,
    Slider,
    CustomRenderScale,
    Tabs,
    Binding,
};

enum class OptionsMenuRowTone : uint8_t {
    Normal,
    Accent,
    Danger,
};

enum class OptionsMenuSliderDisplay : uint8_t {
    Percent,
    ExposureEv,
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
    float sliderMinimum = 0.0f;
    float sliderMaximum = 1.0f;
    OptionsMenuSliderDisplay sliderDisplay =
        OptionsMenuSliderDisplay::Percent;
    bool toggleValue = false;
    bool enabled = true;
    OptionsMenuRowTone tone = OptionsMenuRowTone::Normal;
    bool flexibleSpaceBefore = false;
    bool dividerBefore = false;
};

struct OptionsMenuState {
    bool open = false;
    bool allowTitleExit = false;
    OptionsMenuPage page = OptionsMenuPage::Main;
    int selectedRow = 0;
    BindingDeviceClass controlsBindingDevice =
        BindingDeviceClass::Keyboard;
    EditorControlsSection editorControlsSection =
        EditorControlsSection::Editing;
    std::optional<InputAction> capturingAction;
    std::optional<int> customRenderScalePreview;
    std::string controlsFilter;

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

namespace intent {

struct Open {
    bool allowTitleExit = false;
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
struct SetControlsFilter {
    std::string query;
};

} // namespace intent
} // namespace options

using OptionsAction = std::variant<
    options::SettingsChanged,
    options::Quit,
    options::ExitToTitle>;

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
    options::intent::ProvideBinding,
    options::intent::SetControlsFilter>;

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

    void open(bool allowTitleExit = false);
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

struct OptionsMenuViewInput {
    float wheelY = 0.0f;
    std::optional<Vec2> wheelPosition;
    bool revealSelection = false;
    bool extendTextSelection = false;
};

// Renderer-facing adapter. Scroll position is presentation state; the
// authoritative menu and settings still change through semantic intents.
class OptionsMenuView {
public:
    [[nodiscard]] bool focusControlsSearch(const OptionsMenuState& state);
    [[nodiscard]] bool handleControlsSearchPress(UiContext& ui,
        const OptionsMenuState& state, Vec2 position, bool extendSelection);
    void blurControlsSearch();
    [[nodiscard]] bool controlsSearchFocused(const OptionsMenuState& state) const;
    [[nodiscard]] UiTextEdit* activeTextEdit(const OptionsMenuState& state);
    [[nodiscard]] std::optional<std::string_view> controlsSearchQuery(
        const OptionsMenuState& state) const;
    [[nodiscard]] UiRect textCaret() const { return textCaret_; }

    [[nodiscard]] std::optional<OptionsMenuIntent> draw(
        UiContext& ui,
        Vec2 viewport,
        const OptionsMenuState& state,
        const UserSettings& settings,
        const InputPromptCatalog* inputPrompts = nullptr,
        const GamepadPresentation* gamepad = nullptr,
        OptionsMenuViewInput input = {});

private:
    void prepareControlsSearch(const OptionsMenuState& state);

    UiTextEdit controlsSearch_ { 128 };
    UiTextField controlsSearchField_;
    std::optional<OptionsMenuPage> searchPage_;
    UiRect searchBounds_;
    UiRect textCaret_;
    float searchTextSize_ = 0.0f;
    bool searchFocused_ = false;
    bool searchPressHandled_ = false;
    UiScroll scroll_;
    std::optional<OptionsMenuPage> scrollPage_;
    std::string lastControlsFilter_;
    std::optional<OptionsMenuRowId> lastSelectedRow_;
    Vec2 lastScrollViewport_ {};
};

} // namespace sokoban
