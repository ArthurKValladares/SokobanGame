#pragma once

#include "engine/InputBindings.hpp"
#include "engine/Math.hpp"

#include <optional>

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

struct OptionsMenuResult {
    bool settingsChanged = false;
    bool quitRequested = false;
    bool titleRequested = false;
};

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
    void open(OptionsMenuSettings settings, bool allowTitleExit = false);
    void close();
    void back();
    void requestQuitConfirmation();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] int selectedRow() const { return selectedRow_; }
    [[nodiscard]] const OptionsMenuSettings& settings() const { return settings_; }

    [[nodiscard]] OptionsMenuResult draw(
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
    void navigateRows(const OptionsMenuInput& input, int rowCount);
    void setPage(Page page);
    [[nodiscard]] OptionsMenuResult drawMain(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);
    [[nodiscard]] OptionsMenuResult drawGraphics(
        UiContext& ui,
        UiRect panel,
        Vec2 viewport,
        const OptionsMenuInput& input);
    [[nodiscard]] OptionsMenuResult drawAudio(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);
    [[nodiscard]] OptionsMenuResult drawControls(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);
    [[nodiscard]] OptionsMenuResult drawQuitConfirmation(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);

    bool open_ = false;
    bool allowTitleExit_ = false;
    Page page_ = Page::Main;
    int selectedRow_ = 0;
    bool customRenderScaleDragPending_ = false;
    std::optional<InputAction> capturingAction_;
    bool bindingAssigned_ = false;
    OptionsMenuSettings settings_ {};
};

} // namespace sokoban
