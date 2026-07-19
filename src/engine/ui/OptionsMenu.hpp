#pragma once

#include "engine/Math.hpp"

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
};

class OptionsMenu {
public:
    enum class Page {
        Main,
        Graphics,
        Audio,
        QuitConfirmation,
    };

    void open(OptionsMenuSettings settings);
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
    [[nodiscard]] OptionsMenuResult drawQuitConfirmation(
        UiContext& ui,
        UiRect panel,
        const OptionsMenuInput& input);

    bool open_ = false;
    Page page_ = Page::Main;
    int selectedRow_ = 0;
    bool customRenderScaleDragPending_ = false;
    OptionsMenuSettings settings_ {};
};

} // namespace sokoban
