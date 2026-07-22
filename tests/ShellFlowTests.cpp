// Headless tests for the shell routing rules: menu precedence, Options
// context, the new-game slot-pick chain, and completion resolution.

#include "engine/ShellFlow.hpp"

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

using sokoban::ShellCommand;
using sokoban::ShellFacts;
using sokoban::ShellFlow;

template <typename Command>
[[nodiscard]] const Command* commandAt(
    const std::vector<ShellCommand>& commands,
    std::size_t index)
{
    if (index >= commands.size()) {
        return nullptr;
    }
    return std::get_if<Command>(&commands[index]);
}

template <typename Command>
[[nodiscard]] bool only(const std::vector<ShellCommand>& commands)
{
    return commands.size() == 1 && commandAt<Command>(commands, 0) != nullptr;
}

void testBackRouting()
{
    ShellFlow flow;

    // Options wins over everything else that is open.
    CHECK(only<sokoban::shell::OptionsBack>(flow.handle(
        sokoban::ShellBackPressed {},
        { .optionsOpen = true, .overlayOpen = true, .titleOpen = true })));

    // The completion overlay swallows Back: an explicit choice is required.
    CHECK(flow.handle(
        sokoban::ShellBackPressed {},
        { .overlayOpen = true, .titleOpen = true })
        .empty());

    // Title sub-pages step back; the main page opens title-context Options.
    CHECK(only<sokoban::shell::TitleBack>(flow.handle(
        sokoban::ShellBackPressed {},
        { .titleOpen = true, .titleAtMainPage = false })));
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellBackPressed {},
            { .titleOpen = true, .titleAtMainPage = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(commands.size() == 1 && open != nullptr);
        CHECK(open != nullptr && !open->pauseContext && !open->allowLevelSelect);
    }

    // In gameplay, Back opens the pause menu; Level Select only after the
    // save has beaten the game.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellBackPressed {}, { .gameLoaded = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(open != nullptr && open->pauseContext && !open->allowLevelSelect);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellBackPressed {},
            { .gameLoaded = true, .allLevelsCompleted = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(open != nullptr && open->pauseContext && open->allowLevelSelect);
    }

    // The window's close button always goes through the confirmation.
    CHECK(only<sokoban::shell::RequestQuitConfirmation>(flow.handle(
        sokoban::ShellCloseRequested {}, {})));
}

void testTitleResults()
{
    ShellFlow flow;

    // Continue: loads only when the world is not already loaded.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleResult { { .continueRequested = true } },
            { .gameLoaded = false, .titleOpen = true });
        CHECK(commands.size() == 2);
        CHECK(commandAt<sokoban::shell::LoadCurrentScreen>(commands, 0) != nullptr);
        CHECK(commandAt<sokoban::shell::CloseTitle>(commands, 1) != nullptr);
    }
    CHECK(only<sokoban::shell::CloseTitle>(flow.handle(
        sokoban::ShellTitleResult { { .continueRequested = true } },
        { .gameLoaded = true, .titleOpen = true })));

    // New game on the active slot, and the no-saves slot-pick chain.
    CHECK(only<sokoban::shell::StartNewGame>(flow.handle(
        sokoban::ShellTitleResult { { .newGameRequested = true } }, {})));
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleResult { { .newGameSlotSelected = 2 } }, {});
        CHECK(commands.size() == 2);
        const auto* switchSlot = commandAt<sokoban::shell::SwitchSlot>(commands, 0);
        CHECK(switchSlot != nullptr && switchSlot->slot == 2);
        CHECK(commandAt<sokoban::shell::StartNewGame>(commands, 1) != nullptr);
    }

    // Slot switching and deletion pass their indexes through.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleResult { { .slotSelected = 1 } }, {});
        const auto* switchSlot = commandAt<sokoban::shell::SwitchSlot>(commands, 0);
        CHECK(commands.size() == 1 && switchSlot != nullptr && switchSlot->slot == 1);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleResult { { .slotDeleteRequested = 0 } }, {});
        const auto* deleteSlot = commandAt<sokoban::shell::DeleteSlot>(commands, 0);
        CHECK(commands.size() == 1 && deleteSlot != nullptr && deleteSlot->slot == 0);
    }

    // Level-select starts close the title behind them.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleResult {
                { .startRequested = sokoban::TitleStartRequest { 2, 1 } } },
            {});
        CHECK(commands.size() == 2);
        const auto* start = commandAt<sokoban::shell::StartLevel>(commands, 0);
        CHECK(start != nullptr && start->level == 2 && start->screen == 1);
        CHECK(commandAt<sokoban::shell::CloseTitle>(commands, 1) != nullptr);
    }

    // Options from the title never carries pause-only rows.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleResult { { .optionsRequested = true } },
            { .titleOpen = true, .allLevelsCompleted = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(open != nullptr && !open->pauseContext && !open->allowLevelSelect);
    }
    CHECK(only<sokoban::shell::RequestQuitConfirmation>(flow.handle(
        sokoban::ShellTitleResult { { .quitRequested = true } }, {})));

    // An empty result emits nothing.
    CHECK(flow.handle(sokoban::ShellTitleResult {}, {}).empty());
}

void testOptionsAndOverlayResults()
{
    ShellFlow flow;

    CHECK(only<sokoban::shell::ApplySettings>(flow.handle(
        sokoban::ShellOptionsResult { { .settingsChanged = true } }, {})));
    CHECK(only<sokoban::shell::Quit>(flow.handle(
        sokoban::ShellOptionsResult { { .quitRequested = true } }, {})));
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOptionsResult { { .titleRequested = true } }, {});
        CHECK(commands.size() == 2);
        CHECK(commandAt<sokoban::shell::CloseOptions>(commands, 0) != nullptr);
        CHECK(commandAt<sokoban::shell::OpenTitle>(commands, 1) != nullptr);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOptionsResult { { .levelSelectRequested = true } }, {});
        CHECK(commands.size() == 2);
        CHECK(commandAt<sokoban::shell::CloseOptions>(commands, 0) != nullptr);
        CHECK(commandAt<sokoban::shell::OpenStandaloneLevelSelect>(commands, 1) != nullptr);
    }

    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOverlayResult { { .continueRequested = true } }, {});
        const auto* resolve =
            commandAt<sokoban::shell::ResolveLevelComplete>(commands, 0);
        CHECK(commands.size() == 1 && resolve != nullptr && !resolve->toTitle);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOverlayResult { { .titleRequested = true } }, {});
        const auto* resolve =
            commandAt<sokoban::shell::ResolveLevelComplete>(commands, 0);
        CHECK(commands.size() == 1 && resolve != nullptr && resolve->toTitle);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOverlayResult { { .levelSelectRequested = true } }, {});
        CHECK(commands.size() == 2);
        const auto* resolve =
            commandAt<sokoban::shell::ResolveLevelComplete>(commands, 0);
        CHECK(resolve != nullptr && !resolve->toTitle);
        CHECK(commandAt<sokoban::shell::OpenStandaloneLevelSelect>(commands, 1) != nullptr);
    }
}

} // namespace

int main()
{
    testBackRouting();
    testTitleResults();
    testOptionsAndOverlayResults();

    if (failures == 0) {
        std::cout << "ShellFlowTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ShellFlowTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
