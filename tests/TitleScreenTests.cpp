// Headless tests for the title screen, level select, level-complete overlay,
// and the pause menu's title-exit entry.

#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/LevelCompleteOverlay.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"
#include "engine/ui/Ui.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

const std::filesystem::path fontPath =
    std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "ui/Karla-Regular.ttf";

std::vector<sokoban::TitleLevelInfo> sampleLevels()
{
    return {
        {
            .screenCount = 3,
            .unlocked = true,
            .completed = true,
            .reachedScreens = 3,
            .bestMoves = 42,
            .bestTimeSeconds = 83.0,
        },
        {
            .screenCount = 2,
            .unlocked = true,
            .completed = false,
            .reachedScreens = 2,
        },
        {
            .screenCount = 4,
            .unlocked = false,
            .completed = false,
            .reachedScreens = 0,
        },
    };
}

void testTitleNavigationAndResults()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::TitleScreen title;
    title.open(sampleLevels());
    CHECK(title.isOpen());
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);

    auto draw = [&](sokoban::TitleScreenInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const sokoban::TitleScreenResult result =
            title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // Row order: Continue, New Game, Level Select, Options, Quit.
    CHECK(draw({ .confirm = true }).continueRequested);
    CHECK(draw({ .down = true }).continueRequested == false);
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::NewGameConfirmation);

    // Cancel returns to Main without confirming.
    CHECK(!draw({ .confirm = true }).newGameConfirmed);
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
    title.close();
    title.open(sampleLevels());
    draw({ .down = true });
    draw({ .confirm = true });
    draw({ .down = true });
    CHECK(draw({ .confirm = true }).newGameConfirmed);

    // Options and quit rows report requests.
    title.open(sampleLevels());
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(draw({ .confirm = true }).optionsRequested);
    draw({ .down = true });
    CHECK(draw({ .confirm = true }).quitRequested);

    // Wrap-around navigation: up from the first row reaches the last.
    title.open(sampleLevels());
    draw({ .up = true });
    CHECK(draw({ .confirm = true }).quitRequested);
}

void testLevelSelectScreensAndLocking()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::TitleScreen title;
    title.open(sampleLevels());

    auto draw = [&](sokoban::TitleScreenInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const sokoban::TitleScreenResult result =
            title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    draw({ .down = true });
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::LevelSelect);

    // Completed level: every screen is selectable and choice clamps.
    draw({ .right = true });
    draw({ .right = true });
    draw({ .right = true });
    CHECK(title.selectedScreen() == 2);
    const sokoban::TitleScreenResult startLate = draw({ .confirm = true });
    CHECK(startLate.startRequested &&
        startLate.startRequested->level == 0 &&
        startLate.startRequested->screen == 2);

    // Screen choice resets when moving to another level.
    title.open(sampleLevels());
    draw({ .down = true });
    draw({ .down = true });
    draw({ .confirm = true });
    draw({ .right = true });
    draw({ .down = true });
    CHECK(title.selectedScreen() == 0);

    // Unfinished level: only reached screens are selectable.
    draw({ .right = true });
    draw({ .right = true });
    CHECK(title.selectedScreen() == 1);
    const sokoban::TitleScreenResult startSecond = draw({ .confirm = true });
    CHECK(startSecond.startRequested &&
        startSecond.startRequested->level == 1 &&
        startSecond.startRequested->screen == 1);

    // Locked level: confirm does nothing.
    title.open(sampleLevels());
    draw({ .down = true });
    draw({ .down = true });
    draw({ .confirm = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(!draw({ .confirm = true }).startRequested);

    // Back row (after the levels) returns to Main.
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);

    // back() leaves sub-pages but not Main.
    draw({ .down = true });
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::LevelSelect);
    title.back();
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
    title.back();
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
    CHECK(title.isOpen());
}

void testLevelCompleteOverlay()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::LevelCompleteOverlay overlay;
    CHECK(!overlay.isOpen());

    overlay.open({
        .level = 1,
        .moves = 34,
        .timeSeconds = 61.5,
        .previousBestMoves = 40,
        .previousBestTimeSeconds = 59.0,
        .newBestMoves = true,
        .newBestTime = false,
        .hasNextLevel = true,
    });
    CHECK(overlay.isOpen());

    auto draw = [&](sokoban::LevelCompleteInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const sokoban::LevelCompleteResult result =
            overlay.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    CHECK(draw({ .confirm = true }).continueRequested);
    draw({ .down = true });
    const sokoban::LevelCompleteResult toTitle = draw({ .confirm = true });
    CHECK(toTitle.titleRequested && !toTitle.continueRequested);
    draw({ .up = true });
    CHECK(draw({ .confirm = true }).continueRequested);

    overlay.close();
    CHECK(!overlay.isOpen());
    CHECK(!draw({ .confirm = true }).continueRequested);
}

void testOptionsTitleExitRow()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::OptionsMenu menu;

    auto draw = [&](sokoban::OptionsMenuInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const sokoban::OptionsMenuResult result =
            menu.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // Pause context: Graphics, Audio, Controls, Exit To Title, Quit.
    menu.open({}, true);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    const sokoban::OptionsMenuResult exitToTitle = draw({ .confirm = true });
    CHECK(exitToTitle.titleRequested);

    // Title context: the row is absent and the fourth row is Quit.
    menu.open({}, false);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(!draw({ .confirm = true }).titleRequested);
    CHECK(menu.page() == sokoban::OptionsMenu::Page::QuitConfirmation);
}

} // namespace

int main()
{
    testTitleNavigationAndResults();
    testLevelSelectScreensAndLocking();
    testLevelCompleteOverlay();
    testOptionsTitleExitRow();

    if (failures == 0) {
        std::cout << "TitleScreenTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "TitleScreenTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
