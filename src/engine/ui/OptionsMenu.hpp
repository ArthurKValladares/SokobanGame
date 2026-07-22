#pragma once

#include "engine/InputBindings.hpp"
#include "engine/Math.hpp"

#include <optional>
#include <variant>

namespace sokoban {

class UiContext;
struct UiRect;

struct OptionsMenuSettings {
    int antiAliasingSamples = 8;
    int renderScalePercent = 100;
    bool customRenderScale = false;
    int customRenderScalePercent = 100;
    bool ambientOcclusion = true;
    bool fullscreen = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    float masterVolume = 1.0f;
    float musicVolume = 0.5f;
    InputBindings input = defaultInputBindings();

    bool operator==(const OptionsMenuSettings&) const = default;
};

struct OptionsMenuInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
};

// A frame of options interaction produces at most one action.
namespace options {

// The settings snapshot changed; apply and persist settings().
struct SettingsChanged {};
struct Quit {};
struct ExitToTitle {};
struct OpenLevelSelect {};

} // namespace options

using OptionsAction = std::variant<
    options::SettingsChanged,
    options::Quit,
    options::ExitToTitle,
    options::OpenLevelSelect>;

class OptionsMenu {
public:
    enum class Page {
        Main,
        Graphics,
        Audio,
        Controls,
        QuitConfirmation,
    };

    // allowTitleExit shows an "Exit To Title" entry; enable it when the menu
    // is opened as the in-game pause menu rather than from the title screen.
    // allowLevelSelect shows a "Level Select" entry (pause context only,
    // unlocked once the save has beaten the game).
    void open(
        OptionsMenuSettings settings,
        bool allowTitleExit = false,
        bool allowLevelSelect = false);
    void close();
    void back();
    void requestQuitConfirmation();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] int selectedRow() const { return selectedRow_; }
    [[nodiscard]] const OptionsMenuSettings& settings() const { return settings_; }

    [[nodiscard]] std::optional<OptionsAction> draw(
        UiContext& ui,
        Vec2 viewport,
        const OptionsMenuInput& input);

    // True while the Controls page waits for a raw key/button/axis. The
    // caller feeds InputState::bindingCandidate results here and suppresses
    // normal navigation from those raw events meanwhile. Escape / Start
    // cancel through back().
    [[nodiscard]] bool capturingBinding() const { return capturingAction_.has_value(); }
    [[nodiscard]] std::optional<InputAction> capturingAction() const { return capturingAction_; }
    void provideBindingCandidate(const InputBinding& candidate);

private:
    void setPage(Page page);
    [[nodiscard]] std::optional<OptionsAction> drawMain(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);
    [[nodiscard]] std::optional<OptionsAction> drawGraphics(
        UiContext& ui,
        UiRect panel,
        Vec2 viewport,
        const OptionsMenuInput& input);
    [[nodiscard]] std::optional<OptionsAction> drawAudio(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);
    [[nodiscard]] std::optional<OptionsAction> drawControls(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);
    [[nodiscard]] std::optional<OptionsAction> drawQuitConfirmation(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);

    bool open_ = false;
    bool allowTitleExit_ = false;
    bool allowLevelSelect_ = false;
    Page page_ = Page::Main;
    int selectedRow_ = 0;
    bool customRenderScaleDragPending_ = false;
    std::optional<InputAction> capturingAction_;
    bool bindingAssigned_ = false;
    OptionsMenuSettings settings_ {};
};

} // namespace sokoban
