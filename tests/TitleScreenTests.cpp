// Headless tests for the title screen, level select, level-complete overlay,
// and the pause menu's title-exit entry.

#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/LevelCompleteOverlay.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"
#include "engine/ui/Ui.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <variant>
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

template <typename Alternative, typename Action>
[[nodiscard]] bool isAction(const std::optional<Action>& action)
{
    return action.has_value() && std::holds_alternative<Alternative>(*action);
}

template <typename Alternative, typename Action>
[[nodiscard]] const Alternative* actionAs(const std::optional<Action>& action)
{
    return action.has_value() ? std::get_if<Alternative>(&*action) : nullptr;
}

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
    title.setSaveSlots({
        { .state = sokoban::SaveSlotState::Ready, .currentLevel = 1 },
        {},
        {},
    }, 0);
    title.open(sampleLevels());
    CHECK(title.isOpen());
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);

    auto draw = [&](sokoban::TitleScreenInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::TitleAction> result =
            title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    draw();
    const auto& commands = ui.drawData().commands;
    CHECK(!commands.empty());
    CHECK(commands.front().kind == sokoban::UiDrawKind::Image);
    CHECK(commands.front().rect.size.x == 1280.0f);
    CHECK(commands.front().rect.size.y == 720.0f);
    CHECK(commands.front().uvRect.position.x == 0.0f);
    CHECK(commands.front().uvRect.position.y == 0.0f);
    CHECK(commands.front().uvRect.size.x == 1.0f);
    CHECK(commands.front().uvRect.size.y == 1.0f);
    float rightmostGlyph = 0.0f;
    for (const auto& command : commands) {
        if (command.kind == sokoban::UiDrawKind::FontGlyph) {
            rightmostGlyph = std::max(
                rightmostGlyph,
                command.rect.position.x + command.rect.size.x);
        }
    }
    CHECK(rightmostGlyph > 0.0f);
    CHECK(rightmostGlyph < 640.0f);

    // Active slot has a save: rows are Continue, Save Slot, Options, Quit.
    CHECK(isAction<sokoban::title::Continue>(draw({ .confirm = true })));
    draw({ .down = true });
    draw({ .down = true });
    CHECK(isAction<sokoban::title::OpenOptions>(draw({ .confirm = true })));
    draw({ .down = true });
    CHECK(isAction<sokoban::title::Quit>(draw({ .confirm = true })));

    // Wrap-around navigation: up from the first row reaches the last.
    title.open(sampleLevels());
    draw({ .up = true });
    CHECK(isAction<sokoban::title::Quit>(draw({ .confirm = true })));

    // Active slot empty but another save exists: New Game starts here.
    title.setSaveSlots({
        {},
        { .state = sokoban::SaveSlotState::Ready, .currentLevel = 0 },
        {},
    }, 0);
    title.open(sampleLevels());
    CHECK(isAction<sokoban::title::NewGame>(draw({ .confirm = true })));
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);

    // No saves anywhere: New Game asks for a slot first.
    title.setSaveSlots({ {}, {}, {} }, 0);
    title.open(sampleLevels());
    CHECK(!isAction<sokoban::title::NewGame>(draw({ .confirm = true })));
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);
    draw({ .down = true });
    const auto pick = draw({ .confirm = true });
    const auto* pickSlot = actionAs<sokoban::title::NewGameOnSlot>(pick);
    CHECK(pickSlot != nullptr && pickSlot->slot == 1);

    // With no saves, the Save Slot row is hidden: New Game, Options, Quit.
    title.open(sampleLevels());
    draw({ .down = true });
    CHECK(isAction<sokoban::title::OpenOptions>(draw({ .confirm = true })));
    draw({ .down = true });
    CHECK(isAction<sokoban::title::Quit>(draw({ .confirm = true })));

    // Backing out of the slot pick cancels the new-game intent.
    title.open(sampleLevels());
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);
    title.back();
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
}

void testLevelSelectScreensAndLocking()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::TitleScreen title;

    auto draw = [&](sokoban::TitleScreenInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::TitleAction> result =
            title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // The main menu has no level-select entry; the page opens directly.
    title.openLevelSelect(sampleLevels());
    CHECK(title.page() == sokoban::TitleScreen::Page::LevelSelect);

    // Completed level: every screen is selectable and choice clamps.
    draw({ .right = true });
    draw({ .right = true });
    draw({ .right = true });
    CHECK(title.selectedScreen() == 2);
    const auto startLate = draw({ .confirm = true });
    const auto* lateStart = actionAs<sokoban::title::StartLevel>(startLate);
    CHECK(lateStart != nullptr && lateStart->level == 0 && lateStart->screen == 2);

    // Screen choice resets when moving to another level.
    title.openLevelSelect(sampleLevels());
    draw({ .right = true });
    draw({ .down = true });
    CHECK(title.selectedScreen() == 0);

    // Unfinished level: only reached screens are selectable.
    draw({ .right = true });
    draw({ .right = true });
    CHECK(title.selectedScreen() == 1);
    const auto startSecond = draw({ .confirm = true });
    const auto* secondStart = actionAs<sokoban::title::StartLevel>(startSecond);
    CHECK(secondStart != nullptr && secondStart->level == 1 && secondStart->screen == 1);

    // Locked level: confirm does nothing.
    title.openLevelSelect(sampleLevels());
    draw({ .down = true });
    draw({ .down = true });
    CHECK(!draw({ .confirm = true }).has_value());

    // Back row (after the levels) returns to Main.
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);

    // Standalone level select closes on back() instead of showing Main.
    title.openLevelSelect(sampleLevels());
    CHECK(title.page() == sokoban::TitleScreen::Page::LevelSelect);
    title.back();
    CHECK(!title.isOpen());

    // From the full title, back() leaves sub-pages but not Main.
    title.setSaveSlots({
        { .state = sokoban::SaveSlotState::Ready }, {}, {}
    }, 0);
    title.open(sampleLevels());
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);
    title.back();
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
    title.back();
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
    CHECK(title.isOpen());
}

void testSaveSlotsPage()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::TitleScreen title;
    title.setSaveSlots({
        {
            .state = sokoban::SaveSlotState::Ready,
            .completed = false,
            .currentLevel = 2,
            .completedLevels = 2,
        },
        {},
        {
            .state = sokoban::SaveSlotState::Ready,
            .completed = true,
            .currentLevel = 0,
            .completedLevels = 4,
        },
    }, 0);
    title.open(sampleLevels());

    auto draw = [&](sokoban::TitleScreenInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::TitleAction> result =
            title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // Second main row opens the Save Slots page.
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);

    // Confirming the active slot just returns to Main.
    CHECK(!draw({ .confirm = true }).has_value());
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);

    // Selecting another (non-empty) slot reports its index.
    draw({ .down = true });
    draw({ .confirm = true });
    draw({ .down = true });
    draw({ .down = true });
    const auto third = draw({ .confirm = true });
    const auto* thirdSwitch = actionAs<sokoban::title::SwitchSlot>(third);
    CHECK(thirdSwitch != nullptr && thirdSwitch->slot == 2);

    // Deleting: Right focuses the inline Delete button, then a confirmation
    // page guards the actual request.
    draw({ .up = true });
    draw({ .up = true });
    draw({ .right = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SlotDeleteConfirmation);
    draw({ .confirm = true }); // Cancel row
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);
    draw({ .right = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SlotDeleteConfirmation);
    title.back();
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);
    draw({ .right = true });
    draw({ .confirm = true });
    draw({ .down = true });
    const auto deleted = draw({ .confirm = true });
    const auto* deletedSlot = actionAs<sokoban::title::DeleteSlot>(deleted);
    CHECK(deletedSlot != nullptr && deletedSlot->slot == 0);
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);

    // Empty slots have no delete column: Right stays on the slot button.
    draw({ .down = true });
    draw({ .right = true });
    const auto empty = draw({ .confirm = true });
    const auto* emptySwitch = actionAs<sokoban::title::SwitchSlot>(empty);
    CHECK(emptySwitch != nullptr && emptySwitch->slot == 1);

    // Back row (after the slots) returns to Main.
    title.setSaveSlots({ {}, {}, {} }, 0);
    draw({ .up = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::Main);
}

void testDamagedSaveSlotBehavior()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::TitleScreen title;
    title.setSaveSlots({
        {},
        { .state = sokoban::SaveSlotState::Corrupt },
        { .state = sokoban::SaveSlotState::Recoverable, .currentLevel = 2 },
    }, 0);
    title.open(sampleLevels());

    auto draw = [&](sokoban::TitleScreenInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::TitleAction> result =
            title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // Damaged data counts as stored data: New Game uses the healthy active
    // slot directly and the Save Slots row remains available for cleanup.
    CHECK(isAction<sokoban::title::NewGame>(draw({ .confirm = true })));
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);

    // Corrupt slots cannot be selected, but can still be deleted.
    draw({ .down = true });
    CHECK(!draw({ .confirm = true }).has_value());
    CHECK(title.page() == sokoban::TitleScreen::Page::SaveSlots);
    draw({ .right = true });
    draw({ .confirm = true });
    CHECK(title.page() == sokoban::TitleScreen::Page::SlotDeleteConfirmation);
    title.back();

    // A valid backup is safe to select; the manager performs recovery only
    // after this explicit switch action.
    draw({ .down = true });
    draw({ .down = true });
    const auto recoverable = draw({ .confirm = true });
    const auto* switchSlot =
        actionAs<sokoban::title::SwitchSlot>(recoverable);
    CHECK(switchSlot != nullptr && switchSlot->slot == 2);
}

void testGameCompleteOverlay()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::LevelCompleteOverlay overlay;
    overlay.openGameComplete({
        { .bestMoves = 30, .bestTimeSeconds = 60.0 },
        { .bestMoves = 40, .bestTimeSeconds = 90.5 },
        { .bestMoves = std::nullopt, .bestTimeSeconds = std::nullopt },
    });
    CHECK(overlay.isOpen());
    CHECK(overlay.mode() == sokoban::LevelCompleteOverlay::Mode::Game);

    auto draw = [&](sokoban::LevelCompleteInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::OverlayAction> result =
            overlay.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // Rows: Level Select, Title Screen. No continue in game-complete mode.
    CHECK(isAction<sokoban::overlay::ToLevelSelect>(draw({ .confirm = true })));
    draw({ .down = true });
    CHECK(isAction<sokoban::overlay::ToTitle>(draw({ .confirm = true })));

    // Reopening the per-level mode switches back.
    overlay.open({ .level = 0, .moves = 5, .timeSeconds = 3.0 });
    CHECK(overlay.mode() == sokoban::LevelCompleteOverlay::Mode::Level);
    CHECK(isAction<sokoban::overlay::Continue>(draw({ .confirm = true })));
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
        const std::optional<sokoban::OverlayAction> result =
            overlay.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    CHECK(isAction<sokoban::overlay::Continue>(draw({ .confirm = true })));
    draw({ .down = true });
    CHECK(isAction<sokoban::overlay::ToTitle>(draw({ .confirm = true })));
    draw({ .up = true });
    CHECK(isAction<sokoban::overlay::Continue>(draw({ .confirm = true })));

    overlay.close();
    CHECK(!overlay.isOpen());
    CHECK(!draw({ .confirm = true }).has_value());
}

void testOptionsTitleExitRow()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::OptionsMenu menu;

    auto draw = [&](sokoban::OptionsMenuInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::OptionsAction> result =
            menu.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    // Pause context: Graphics, Audio, Controls, Exit To Title, Quit.
    menu.open({}, true);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(isAction<sokoban::options::ExitToTitle>(draw({ .confirm = true })));

    // Title context: the row is absent and the fourth row is Quit.
    menu.open({}, false);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(!isAction<sokoban::options::ExitToTitle>(draw({ .confirm = true })));
    CHECK(menu.page() == sokoban::OptionsMenu::Page::QuitConfirmation);

    // Pause context after beating the game: Graphics, Audio, Controls,
    // Level Select, Exit To Title, Quit.
    menu.open({}, true, true);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(isAction<sokoban::options::OpenLevelSelect>(draw({ .confirm = true })));
    draw({ .down = true });
    CHECK(isAction<sokoban::options::ExitToTitle>(draw({ .confirm = true })));

    // Level Select never appears outside the pause context.
    menu.open({}, false, true);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
    CHECK(!isAction<sokoban::options::OpenLevelSelect>(draw({ .confirm = true })));
    CHECK(menu.page() == sokoban::OptionsMenu::Page::QuitConfirmation);
}

} // namespace

int main()
{
    testTitleNavigationAndResults();
    testLevelSelectScreensAndLocking();
    testLevelCompleteOverlay();
    testSaveSlotsPage();
    testDamagedSaveSlotBehavior();
    testGameCompleteOverlay();
    testOptionsTitleExitRow();

    if (failures == 0) {
        std::cout << "TitleScreenTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "TitleScreenTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
