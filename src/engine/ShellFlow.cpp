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
    const TitleScreenResult& result,
    const ShellFacts& facts,
    std::vector<ShellCommand>& commands)
{
    if (result.continueRequested) {
        if (!facts.gameLoaded) {
            commands.push_back(shell::LoadCurrentScreen {});
        }
        commands.push_back(shell::CloseTitle {});
    }
    if (result.newGameRequested) {
        commands.push_back(shell::StartNewGame {});
    }
    if (result.newGameSlotSelected) {
        // Switching to the already-active slot no-ops in the executor, so
        // one command pair covers both first-run cases.
        commands.push_back(shell::SwitchSlot { *result.newGameSlotSelected });
        commands.push_back(shell::StartNewGame {});
    }
    if (result.slotSelected) {
        commands.push_back(shell::SwitchSlot { *result.slotSelected });
    }
    if (result.slotDeleteRequested) {
        commands.push_back(shell::DeleteSlot { *result.slotDeleteRequested });
    }
    if (result.startRequested) {
        commands.push_back(shell::StartLevel {
            result.startRequested->level,
            result.startRequested->screen,
        });
        commands.push_back(shell::CloseTitle {});
    }
    if (result.optionsRequested) {
        commands.push_back(shell::OpenOptions { .pauseContext = false });
    }
    if (result.quitRequested) {
        commands.push_back(shell::RequestQuitConfirmation {});
    }
}

void reduceOptions(
    const OptionsMenuResult& result,
    std::vector<ShellCommand>& commands)
{
    if (result.settingsChanged) {
        commands.push_back(shell::ApplySettings {});
    }
    if (result.quitRequested) {
        commands.push_back(shell::Quit {});
    }
    if (result.titleRequested) {
        commands.push_back(shell::CloseOptions {});
        commands.push_back(shell::OpenTitle {});
    }
    if (result.levelSelectRequested) {
        commands.push_back(shell::CloseOptions {});
        commands.push_back(shell::OpenStandaloneLevelSelect {});
    }
}

void reduceOverlay(
    const LevelCompleteResult& result,
    std::vector<ShellCommand>& commands)
{
    if (result.continueRequested) {
        commands.push_back(shell::ResolveLevelComplete { .toTitle = false });
    }
    if (result.titleRequested) {
        commands.push_back(shell::ResolveLevelComplete { .toTitle = true });
    }
    if (result.levelSelectRequested) {
        commands.push_back(shell::ResolveLevelComplete { .toTitle = false });
        commands.push_back(shell::OpenStandaloneLevelSelect {});
    }
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
        [&](const ShellTitleResult& title) {
            reduceTitle(title.result, facts, commands);
        },
        [&](const ShellOptionsResult& options) {
            reduceOptions(options.result, commands);
        },
        [&](const ShellOverlayResult& overlay) {
            reduceOverlay(overlay.result, commands);
        },
    }, event);
}

} // namespace sokoban
