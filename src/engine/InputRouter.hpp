#pragma once

#include "engine/GameplayLoop.hpp"
#include "engine/Input.hpp"
#include "engine/ui/LevelCompleteOverlay.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"

#include <SDL3/SDL_events.h>

#include <optional>

namespace sokoban {

// Routes raw device state to the one active interaction context. It contains
// no gameplay or UI behavior; consumers execute the semantic frames it emits.
class InputRouter {
public:
    struct EventContext {
        bool bindingCapture = false;
        bool shellMenuOpen = false;
        bool keyboardCaptured = false;
        bool mouseCaptured = false;
        bool editorEditing = false;
    };

    struct EventResult {
        std::optional<InputBinding> bindingCandidate;
        bool closeRequested = false;
        bool forwardedToInput = false;
    };

    enum class BackAction {
        None,
        CloseDraftConfirmation,
        OpenDraftConfirmation,
        ShellBack,
    };

    struct RoutingContext {
        bool optionsOpen = false;
        bool titleOpen = false;
        bool overlayOpen = false;
        bool editorEditing = false;
        bool draftPlaying = false;
        bool draftExitConfirmationOpen = false;
        bool mouseCaptured = false;
    };

    struct PointerInput {
        Vec2 position;
        bool primaryDown = false;
        bool primaryPressed = false;
    };

    struct EditorInput {
        Vec2 pointerPosition;
        bool primaryPressed = false;
        bool undoPressed = false;
        bool deleting = false;
        bool replaceLayer = false;
        bool pointerCaptured = false;
    };

    struct Frame {
        GameplayLoop::InputFrame gameplay;
        TitleScreenInput title;
        LevelCompleteInput overlay;
        OptionsMenuInput options;
        PointerInput pointer;
        EditorInput editor;
    };

    [[nodiscard]] EventResult routeEvent(
        const SDL_Event& event,
        InputState& input,
        const EventContext& context) const;
    [[nodiscard]] BackAction backAction(
        const InputState& input,
        const RoutingContext& context) const;
    [[nodiscard]] Frame routeFrame(
        const InputState& input,
        const RoutingContext& context) const;
};

} // namespace sokoban
