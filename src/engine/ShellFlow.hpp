#pragma once

#include "engine/Flow.hpp"
#include "engine/ui/LevelCompleteOverlay.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"

#include <variant>
#include <vector>

namespace sokoban {

// ---- Events: everything the shell reacts to. -------------------------------

struct ShellBackPressed {};
// The window itself asked to close (e.g. SDL_EVENT_QUIT).
struct ShellCloseRequested {};
struct ShellTitleAction {
    TitleAction action;
};
struct ShellOptionsAction {
    OptionsAction action;
};
struct ShellOverlayAction {
    OverlayAction action;
};

using ShellEvent = std::variant<
    ShellBackPressed,
    ShellCloseRequested,
    ShellTitleAction,
    ShellOptionsAction,
    ShellOverlayAction>;

// ---- Facts: a snapshot of the world the flow may consult. ------------------

struct ShellFacts {
    bool gameLoaded = false;
    bool optionsOpen = false;
    bool overlayOpen = false;
    bool titleOpen = false;
    bool titleAtMainPage = false;
    bool allLevelsCompleted = false;
};

// ---- Commands: effects for the caller to execute, in order. ----------------

namespace shell {

struct LoadCurrentScreen {};
struct CloseTitle {};
struct OpenTitle {};
struct TitleBack {};
struct StartNewGame {};
struct SwitchSlot {
    int slot = 0;
};
struct DeleteSlot {
    int slot = 0;
};
struct StartLevel {
    int level = 0;
    int screen = 0;
};
struct OpenOptions {
    bool pauseContext = false;
    bool allowLevelSelect = false;
};
struct CloseOptions {};
struct OptionsBack {};
struct ApplySettings {};
struct RequestQuitConfirmation {};
struct Quit {};
struct ResolveLevelComplete {
    bool toTitle = false;
};
struct OpenStandaloneLevelSelect {};

} // namespace shell

using ShellCommand = std::variant<
    shell::LoadCurrentScreen,
    shell::CloseTitle,
    shell::OpenTitle,
    shell::TitleBack,
    shell::StartNewGame,
    shell::SwitchSlot,
    shell::DeleteSlot,
    shell::StartLevel,
    shell::OpenOptions,
    shell::CloseOptions,
    shell::OptionsBack,
    shell::ApplySettings,
    shell::RequestQuitConfirmation,
    shell::Quit,
    shell::ResolveLevelComplete,
    shell::OpenStandaloneLevelSelect>;

struct ShellFlowState {
    // Reserved: the shell currently derives everything from facts and menu
    // results; future flows may hold page-independent state here.
};

// Pure routing for the game shell: menu actions, Back presses, and window
// close requests go in; ordered commands come out. Owns every menu-precedence
// and context rule (pause vs. title Options, level-select gating, the
// no-saves new-game slot pick chain), so those rules are unit-tested without
// menus, SDL, or a world. `Application` executes the commands.
class ShellFlow : public flow::Machine<ShellFlow, ShellFlowState, ShellEvent,
                      ShellCommand, ShellFacts> {
public:
    void reduce(
        ShellFlowState& state,
        const ShellEvent& event,
        const ShellFacts& facts,
        std::vector<ShellCommand>& commands);
};

} // namespace sokoban
