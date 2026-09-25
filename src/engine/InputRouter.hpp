#pragma once

#include "engine/GameplayLoop.hpp"
#include "engine/Input.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/TitleScreen.hpp"

#include <SDL3/SDL_events.h>

#include <cstddef>
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
        CancelDecorationPlacement,
        ShellBack,
    };

    struct RoutingContext {
        bool optionsOpen = false;
        bool titleOpen = false;
        bool editorEditing = false;
        bool decorationPlacementReady = false;
        bool draftPlaying = false;
        bool draftExitConfirmationOpen = false;
        bool keyboardCaptured = false;
        bool mouseCaptured = false;
    };

    struct PointerInput {
        Vec2 position;
        bool primaryDown = false;
        bool primaryPressed = false;
    };

    struct EditorInput {
        Vec2 pointerPosition;
        // Edge: true only on the frame the button goes down. Tile placement
        // wants this, so one click places one tile.
        bool primaryPressed = false;
        // Level: true for as long as the button is held. Brush strokes want
        // this, so a drag keeps painting instead of stopping after one frame.
        bool primaryDown = false;
        bool secondaryPressed = false;
        bool undoPressed = false;
        // Editor shortcuts; each is an InputAction the player can rebind
        // under Options > Controls > Editor Controls.
        bool redoPressed = false;
        bool savePressed = false;
        bool layerUpPressed = false;
        bool layerDownPressed = false;
        bool cycleToolPressed = false;
        bool toggleLayerLockPressed = false;
        // Recent-tile shortcuts select slots 0-8.
        std::optional<std::size_t> recentTileSlot;
        // Held: turns a click into the eyedropper.
        bool pickModifier = false;
        // Held: keeps a drag stroke on its starting row or column.
        bool lineConstraint = false;
        bool deleting = false;
        bool replaceLayer = false;
        bool moving = false;
        bool translateGizmoPressed = false;
        bool rotateGizmoPressed = false;
        bool scaleGizmoPressed = false;
        bool pointerCaptured = false;
    };

    struct Frame {
        GameplayLoop::InputFrame gameplay;
        bool showTopDownView = false;
        bool showOverworldMap = false;
        bool previewScreen = false;
        TitleScreenInput title;
        OptionsMenuInput options;
        // Play/Stop Draft plays the draft from the editor and returns to the
        // editor while it plays. Play From Cursor plays with the hero moved
        // to the cursor (and also stops a playing draft).
        bool toggleDraftPlaybackPressed = false;
        bool playDraftFromCursorPressed = false;
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
