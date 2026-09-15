// Headless tests for the shell routing rules: menu precedence, Options
// context, the new-game slot-pick operation, and completion resolution.

#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/SaveSlotManager.hpp"
#include "engine/ShellFlow.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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

    // In gameplay, Back opens the pause menu. Completion never enables a
    // shortcut around the overworld.
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
        CHECK(open != nullptr && open->pauseContext && !open->allowLevelSelect);
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
            sokoban::ShellTitleAction { sokoban::title::Continue {} },
            { .gameLoaded = false, .titleOpen = true });
        CHECK(commands.size() == 2);
        CHECK(commandAt<sokoban::shell::LoadCurrentScreen>(commands, 0) != nullptr);
        CHECK(commandAt<sokoban::shell::CloseTitle>(commands, 1) != nullptr);
    }
    CHECK(only<sokoban::shell::CloseTitle>(flow.handle(
        sokoban::ShellTitleAction { sokoban::title::Continue {} },
        { .gameLoaded = true, .titleOpen = true })));

    // New game on the active slot, and the no-saves slot-pick operation.
    CHECK(only<sokoban::shell::StartNewGame>(flow.handle(
        sokoban::ShellTitleAction { sokoban::title::NewGame {} }, {})));
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleAction { sokoban::title::NewGameOnSlot { 2 } }, {});
        CHECK(commands.size() == 1);
        const auto* start =
            commandAt<sokoban::shell::StartNewGameOnSlot>(commands, 0);
        CHECK(start != nullptr && start->slot == 2);
    }

    // Slot switching and deletion pass their indexes through.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleAction { sokoban::title::SwitchSlot { 1 } }, {});
        const auto* switchSlot = commandAt<sokoban::shell::SwitchSlot>(commands, 0);
        CHECK(commands.size() == 1 && switchSlot != nullptr && switchSlot->slot == 1);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleAction { sokoban::title::DeleteSlot { 0 } }, {});
        const auto* deleteSlot = commandAt<sokoban::shell::DeleteSlot>(commands, 0);
        CHECK(commands.size() == 1 && deleteSlot != nullptr && deleteSlot->slot == 0);
    }

    // Level-select starts close the title behind them.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleAction { sokoban::title::StartLevel { 2, 1 } }, {});
        CHECK(commands.size() == 2);
        const auto* start = commandAt<sokoban::shell::StartLevel>(commands, 0);
        CHECK(start != nullptr && start->level == 2 && start->screen == 1);
        CHECK(commandAt<sokoban::shell::CloseTitle>(commands, 1) != nullptr);
    }

    // Options from the title never carries pause-only rows.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleAction { sokoban::title::OpenOptions {} },
            { .titleOpen = true, .allLevelsCompleted = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(open != nullptr && !open->pauseContext && !open->allowLevelSelect);
    }
    CHECK(only<sokoban::shell::RequestQuitConfirmation>(flow.handle(
        sokoban::ShellTitleAction { sokoban::title::Quit {} }, {})));
}

void testNewGameOnSlotStopsAfterSwitchFailure()
{
    ScopedTestDirectory directory("sokoban-new-game-slot");
    sokoban::SaveSlotManager manager(
        directory.path(), std::chrono::milliseconds(0));
    sokoban::PlayerProfile liveProfile = manager.loadActiveProfile();
    liveProfile.unlockedLevel = 2;
    liveProfile.setCurrentScreen(2, 1);
    const sokoban::PlayerProfile profileBeforeFailure = liveProfile;
    bool titleOpen = true;
    bool gameStarted = false;
    std::string titleError;

    // A non-empty directory at the marker writer's temporary path injects a
    // deterministic failure at the slot-selection commit point.
    const std::filesystem::path blockedTemporary =
        directory.path() / "active-slot.txt.tmp";
    std::filesystem::create_directories(blockedTemporary);
    std::ofstream(blockedTemporary / "blocker.txt") << "blocked";

    const sokoban::shell::StartNewGameOnSlot command { .slot = 1 };
    const auto switchSlot = [&](int slot) {
        try {
            std::optional<sokoban::PlayerProfile> switched =
                manager.switchTo(slot, liveProfile);
            if (!switched) {
                return slot == manager.activeSlot();
            }
            liveProfile = std::move(*switched);
            titleError.clear();
            return true;
        } catch (const std::exception& error) {
            titleError = error.what();
            return false;
        }
    };
    const auto startNewGame = [&] {
        gameStarted = true;
        titleOpen = false;
        liveProfile.resetProgress();
    };

    CHECK(!sokoban::executeNewGameOnSlot(
        command, switchSlot, startNewGame));
    CHECK(manager.activeSlot() == 0);
    CHECK(liveProfile == profileBeforeFailure);
    CHECK(titleOpen);
    CHECK(!titleError.empty());
    CHECK(!gameStarted);

    std::filesystem::remove_all(blockedTemporary);
    CHECK(sokoban::executeNewGameOnSlot(
        command, switchSlot, startNewGame));
    CHECK(manager.activeSlot() == 1);
    CHECK(gameStarted);
    CHECK(!titleOpen);
    CHECK(titleError.empty());
    CHECK(liveProfile == sokoban::PlayerProfile {});
}

void testOptionsAndOverlayResults()
{
    ShellFlow flow;

    {
        sokoban::UserSettings settings;
        settings.audio.masterVolume = 0.35f;
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOptionsAction {
                sokoban::options::SettingsChanged { settings } },
            {});
        const auto* apply =
            commandAt<sokoban::shell::ApplySettings>(commands, 0);
        CHECK(commands.size() == 1);
        CHECK(apply != nullptr);
        CHECK(apply && apply->settings == settings);
    }
    CHECK(only<sokoban::shell::Quit>(flow.handle(
        sokoban::ShellOptionsAction { sokoban::options::Quit {} }, {})));
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOptionsAction { sokoban::options::ExitToTitle {} }, {});
        CHECK(commands.size() == 2);
        CHECK(commandAt<sokoban::shell::CloseOptions>(commands, 0) != nullptr);
        CHECK(commandAt<sokoban::shell::OpenTitle>(commands, 1) != nullptr);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOptionsAction { sokoban::options::OpenLevelSelect {} }, {});
        CHECK(commands.size() == 2);
        CHECK(commandAt<sokoban::shell::CloseOptions>(commands, 0) != nullptr);
        CHECK(commandAt<sokoban::shell::OpenStandaloneLevelSelect>(commands, 1) != nullptr);
    }

    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOverlayAction { sokoban::overlay::Continue {} }, {});
        const auto* resolve =
            commandAt<sokoban::shell::ResolveLevelComplete>(commands, 0);
        CHECK(commands.size() == 1 && resolve != nullptr && !resolve->toTitle);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOverlayAction { sokoban::overlay::ToTitle {} }, {});
        const auto* resolve =
            commandAt<sokoban::shell::ResolveLevelComplete>(commands, 0);
        CHECK(commands.size() == 1 && resolve != nullptr && resolve->toTitle);
    }
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellOverlayAction { sokoban::overlay::ToLevelSelect {} }, {});
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
    testNewGameOnSlotStopsAfterSwitchFailure();
    testOptionsAndOverlayResults();

    if (failures == 0) {
        std::cout << "ShellFlowTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ShellFlowTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
