#include "engine/ShellFlow.hpp"

namespace sokoban {
namespace {

void reduceBack(const ShellFacts& facts, std::vector<ShellCommand>& commands)
{
    if (facts.optionsOpen) {
        commands.push_back(shell::OptionsBack {});
        return;
    }
    if (facts.overlayOpen) {
        // The completion overlays require an explicit choice.
        return;
    }
    if (facts.titleOpen) {
        if (facts.titleAtMainPage) {
            commands.push_back(shell::OpenOptions { .pauseContext = false });
        } else {
            commands.push_back(shell::TitleBack {});
        }
        return;
    }
    commands.push_back(shell::OpenOptions {
        .pauseContext = true,
        .allowLevelSelect = facts.allLevelsCompleted,
    });
}

void reduceTitle(
    const TitleAction& action,
    const ShellFacts& facts,
    std::vector<ShellCommand>& commands)
{
    std::visit(flow::Overloaded {
        [&](const title::Continue&) {
            if (!facts.gameLoaded) {
                commands.push_back(shell::LoadCurrentScreen {});
            }
            commands.push_back(shell::CloseTitle {});
        },
        [&](const title::NewGame&) {
            commands.push_back(shell::StartNewGame {});
        },
        [&](const title::NewGameOnSlot& newGame) {
            // Switching to the already-active slot no-ops in the executor,
            // so one command pair covers both first-run cases.
            commands.push_back(shell::SwitchSlot { newGame.slot });
            commands.push_back(shell::StartNewGame {});
        },
        [&](const title::SwitchSlot& switchSlot) {
            commands.push_back(shell::SwitchSlot { switchSlot.slot });
        },
        [&](const title::DeleteSlot& deleteSlot) {
            commands.push_back(shell::DeleteSlot { deleteSlot.slot });
        },
        [&](const title::StartLevel& start) {
            commands.push_back(shell::StartLevel { start.level, start.screen });
            commands.push_back(shell::CloseTitle {});
        },
        [&](const title::OpenOptions&) {
            commands.push_back(shell::OpenOptions { .pauseContext = false });
        },
        [&](const title::Quit&) {
            commands.push_back(shell::RequestQuitConfirmation {});
        },
    }, action);
}

void reduceOptions(
    const OptionsAction& action,
    std::vector<ShellCommand>& commands)
{
    std::visit(flow::Overloaded {
        [&](const options::SettingsChanged&) {
            commands.push_back(shell::ApplySettings {});
        },
        [&](const options::Quit&) {
            commands.push_back(shell::Quit {});
        },
        [&](const options::ExitToTitle&) {
            commands.push_back(shell::CloseOptions {});
            commands.push_back(shell::OpenTitle {});
        },
        [&](const options::OpenLevelSelect&) {
            commands.push_back(shell::CloseOptions {});
            commands.push_back(shell::OpenStandaloneLevelSelect {});
        },
    }, action);
}

void reduceOverlay(
    const OverlayAction& action,
    std::vector<ShellCommand>& commands)
{
    std::visit(flow::Overloaded {
        [&](const overlay::Continue&) {
            commands.push_back(shell::ResolveLevelComplete { .toTitle = false });
        },
        [&](const overlay::ToTitle&) {
            commands.push_back(shell::ResolveLevelComplete { .toTitle = true });
        },
        [&](const overlay::ToLevelSelect&) {
            commands.push_back(shell::ResolveLevelComplete { .toTitle = false });
            commands.push_back(shell::OpenStandaloneLevelSelect {});
        },
    }, action);
}

} // namespace

void ShellFlow::reduce(
    ShellFlowState&,
    const ShellEvent& event,
    const ShellFacts& facts,
    std::vector<ShellCommand>& commands)
{
    std::visit(flow::Overloaded {
        [&](const ShellBackPressed&) { reduceBack(facts, commands); },
        [&](const ShellCloseRequested&) {
            commands.push_back(shell::RequestQuitConfirmation {});
        },
        [&](const ShellTitleAction& titleEvent) {
            reduceTitle(titleEvent.action, facts, commands);
        },
        [&](const ShellOptionsAction& optionsEvent) {
            reduceOptions(optionsEvent.action, commands);
        },
        [&](const ShellOverlayAction& overlayEvent) {
            reduceOverlay(overlayEvent.action, commands);
        },
    }, event);
}

} // namespace sokoban
