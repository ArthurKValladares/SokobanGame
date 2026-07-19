#pragma once

#include "engine/Math.hpp"

#include <optional>
#include <vector>

namespace sokoban {

class UiContext;
struct UiRect;

struct TitleLevelInfo {
    int screenCount = 0;
    bool unlocked = false;
    bool completed = false;
    // Screens the player has entered (max reached index + 1). Completed
    // levels expose every screen regardless of this value.
    int reachedScreens = 0;
    std::optional<int> bestMoves;
    std::optional<double> bestTimeSeconds;
};

struct TitleScreenInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
};

struct TitleStartRequest {
    int level = 0;
    int screen = 0;
};

struct TitleScreenResult {
    bool continueRequested = false;
    bool newGameConfirmed = false;
    std::optional<TitleStartRequest> startRequested;
    bool optionsRequested = false;
    bool quitRequested = false;
};

// Headless title-screen state and navigation (main menu, new-game
// confirmation, and level/screen select). Rendering goes through the shared
// UiContext/UiControls stack; the caller owns what each result means.
class TitleScreen {
public:
    enum class Page {
        Main,
        NewGameConfirmation,
        LevelSelect,
    };

    void open(std::vector<TitleLevelInfo> levels);
    void close();
    // Leaves LevelSelect/NewGameConfirmation back to Main; no-op on Main.
    void back();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] Page page() const { return page_; }
    [[nodiscard]] int selectedRow() const { return selectedRow_; }
    [[nodiscard]] int selectedScreen() const { return selectedScreen_; }

    [[nodiscard]] TitleScreenResult draw(
        UiContext& ui,
        Vec2 viewport,
        const TitleScreenInput& input);

private:
    void setPage(Page page);
    void navigateRows(const TitleScreenInput& input, int rowCount);
    [[nodiscard]] int selectableScreens(const TitleLevelInfo& level) const;
    [[nodiscard]] TitleScreenResult drawMain(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);
    [[nodiscard]] TitleScreenResult drawNewGameConfirmation(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);
    [[nodiscard]] TitleScreenResult drawLevelSelect(
        UiContext& ui,
        UiRect panel,
        const TitleScreenInput& input);

    bool open_ = false;
    Page page_ = Page::Main;
    int selectedRow_ = 0;
    int selectedScreen_ = 0;
    std::vector<TitleLevelInfo> levels_;
};

} // namespace sokoban
