// Headless tests for shell menu routing and the new-game slot-pick operation.

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

    // Options wins over the title screen when both are open.
    CHECK(only<sokoban::shell::OptionsBack>(flow.handle(
        sokoban::ShellBackPressed {},
        { .optionsOpen = true, .titleOpen = true })));

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
        CHECK(open != nullptr && !open->pauseContext);
    }

    // In gameplay, Back opens the pause menu.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellBackPressed {}, { .gameLoaded = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(open != nullptr && open->pauseContext);
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

    // Options from the title does not carry pause context.
    {
        const std::vector<ShellCommand> commands = flow.handle(
            sokoban::ShellTitleAction { sokoban::title::OpenOptions {} },
            { .titleOpen = true });
        const auto* open = commandAt<sokoban::shell::OpenOptions>(commands, 0);
        CHECK(open != nullptr && !open->pauseContext);
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

void testOptionsResults()
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
}

} // namespace

int main()
{
    testBackRouting();
    testTitleResults();
    testNewGameOnSlotStopsAfterSwitchFailure();
    testOptionsResults();

    if (failures == 0) {
        std::cout << "ShellFlowTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ShellFlowTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
