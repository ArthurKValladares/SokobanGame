#include "engine/InputRouter.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

namespace sokoban {
namespace {

GameplayLoop::ButtonState buttonState(
    const InputState& input,
    InputAction action)
{
    return {
        .pressed = input.actionPressed(action),
        .down = input.actionDown(action),
    };
}

} // namespace

InputRouter::EventResult InputRouter::routeEvent(
    const SDL_Event& event,
    InputState& input,
    const EventContext& context) const
{
    EventResult result;
    result.closeRequested = event.type == SDL_EVENT_QUIT;

    if (context.bindingCapture) {
        result.bindingCandidate = InputState::bindingCandidate(event);
    }

    const bool keyboardEvent =
        event.type == SDL_EVENT_KEY_DOWN ||
        event.type == SDL_EVENT_KEY_UP;
    const bool mouseEvent =
        event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    // Held editor modifiers, the modifier keys chords need, and the gizmo
    // keys keep their physical state accurate while a panel has keyboard
    // focus; they only change what the next viewport click does.
    const auto boundTo = [&](InputAction action) {
        return input.keyBoundToAction(event.key.scancode, action);
    };
    const bool editorEditModifier = keyboardEvent &&
        context.editorEditing &&
        (boundTo(InputAction::EditorReplaceTile) ||
            boundTo(InputAction::EditorDeleteTile) ||
            boundTo(InputAction::EditorMoveTile) ||
            boundTo(InputAction::EditorPickTile) ||
            boundTo(InputAction::EditorStraightLine) ||
            boundTo(InputAction::EditorGizmoTranslate) ||
            boundTo(InputAction::EditorGizmoRotate) ||
            boundTo(InputAction::EditorGizmoScale) ||
            InputState::isModifierKey(event.key.scancode));
    const bool menuBackKey = keyboardEvent &&
        input.keyBoundToAction(event.key.scancode, InputAction::MenuBack);

    const bool allowKeyboard = !keyboardEvent ||
        context.shellMenuOpen ||
        !context.keyboardCaptured ||
        event.type == SDL_EVENT_KEY_UP ||
        editorEditModifier ||
        menuBackKey;
    const bool allowMouse = !mouseEvent ||
        context.shellMenuOpen ||
        !context.mouseCaptured ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    const bool suppressPressForBindingCapture = context.bindingCapture &&
        ((keyboardEvent && !menuBackKey) ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
            event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION);

    if (suppressPressForBindingCapture) {
        // Capture owns the edge, but InputState still needs the current
        // physical value. In particular, dropping releases or neutral axis
        // motion would leave an action held after capture ends.
        input.handleEvent(event, InputState::PressPolicy::Suppress);
        result.forwardedToInput = true;
    } else if (allowKeyboard && allowMouse) {
        input.handleEvent(event);
        result.forwardedToInput = true;
    }
    return result;
}

InputRouter::BackAction InputRouter::backAction(
    const InputState& input,
    const RoutingContext& context) const
{
    if (!input.actionPressed(InputAction::MenuBack)) {
        return BackAction::None;
    }
    if (context.draftExitConfirmationOpen) {
        return BackAction::CloseDraftConfirmation;
    }
    if (context.draftPlaying) {
        return BackAction::OpenDraftConfirmation;
    }
    if (context.editorEditing && context.decorationPlacementReady) {
        return BackAction::CancelDecorationPlacement;
    }
    return BackAction::ShellBack;
}

InputRouter::Frame InputRouter::routeFrame(
    const InputState& input,
    const RoutingContext& context) const
{
    const bool up = input.actionPressed(InputAction::MoveUp);
    const bool down = input.actionPressed(InputAction::MoveDown);
    const bool left = input.actionPressed(InputAction::MoveLeft);
    const bool right = input.actionPressed(InputAction::MoveRight);
    const bool confirm = input.actionPressed(InputAction::MenuConfirm);
    const bool shellOpen = context.optionsOpen || context.titleOpen;
    const bool gameplayActive = !shellOpen &&
        !context.editorEditing &&
        !context.draftExitConfirmationOpen;

    Frame frame;
    if (gameplayActive) {
        frame.showTopDownView =
            input.actionDown(InputAction::ShowTopDownView);
        frame.showOverworldMap =
            input.actionDown(InputAction::ShowOverworldMap);
        frame.previewScreen =
            input.actionDown(InputAction::PreviewScreen);
        frame.gameplay = {
            .up = buttonState(input, InputAction::MoveUp),
            .down = buttonState(input, InputAction::MoveDown),
            .left = buttonState(input, InputAction::MoveLeft),
            .right = buttonState(input, InputAction::MoveRight),
            .undoPressed = input.actionPressed(InputAction::Undo),
            .undoDown = input.actionDown(InputAction::Undo),
            .restartPressed = input.actionPressed(InputAction::Restart),
            .cycleHeroPressed = input.actionPressed(InputAction::CycleHero),
            .interactPressed = confirm,
        };
    }
    if (context.titleOpen && !context.optionsOpen) {
        frame.title = { up, down, left, right, confirm };
    }
    if (context.optionsOpen) {
        frame.options = { up, down, left, right, confirm };
    }

    if ((context.editorEditing || context.draftPlaying) && !shellOpen &&
        !context.keyboardCaptured) {
        const bool fromCursor =
            input.actionPressed(InputAction::EditorPlayFromCursor);
        frame.toggleDraftPlaybackPressed =
            input.actionPressed(InputAction::EditorPlayDraft) ||
            // While playing, either key returns to the editor.
            (context.draftPlaying && fromCursor);
        frame.playDraftFromCursorPressed =
            context.editorEditing && fromCursor;
    }

    frame.pointer = {
        .position = input.mousePosition(),
        .primaryDown = input.mouseButtonDown(SDL_BUTTON_LEFT),
        .primaryPressed = input.mouseButtonPressed(SDL_BUTTON_LEFT),
    };
    if (context.editorEditing && !shellOpen) {
        // A focused text field or panel owns the keyboard. Held modifiers
        // still apply so a panel click does not strand them.
        const bool shortcuts = !context.keyboardCaptured;
        const auto pressed = [&](InputAction action) {
            return shortcuts && input.actionPressed(action);
        };
        std::optional<std::size_t> recentTileSlot;
        for (int slot = 0; slot < editorRecentTileActionCount; ++slot) {
            if (pressed(editorRecentTileAction(slot))) {
                recentTileSlot = static_cast<std::size_t>(slot);
                break;
            }
        }
        frame.editor = {
            .pointerPosition = input.mousePosition(),
            .primaryPressed = input.mouseButtonPressed(SDL_BUTTON_LEFT),
            .primaryDown = input.mouseButtonDown(SDL_BUTTON_LEFT),
            .secondaryPressed = input.mouseButtonPressed(SDL_BUTTON_RIGHT),
            .undoPressed = input.actionPressed(InputAction::Undo),
            .redoPressed = pressed(InputAction::EditorRedo),
            .savePressed = pressed(InputAction::EditorSave),
            .layerUpPressed = pressed(InputAction::EditorLayerUp),
            .layerDownPressed = pressed(InputAction::EditorLayerDown),
            .cycleToolPressed = pressed(InputAction::EditorCycleTool),
            .toggleLayerLockPressed =
                pressed(InputAction::EditorToggleLayerLock),
            .recentTileSlot = recentTileSlot,
            .pickModifier = input.actionDown(InputAction::EditorPickTile),
            .lineConstraint =
                input.actionDown(InputAction::EditorStraightLine),
            .deleting = input.actionDown(InputAction::EditorDeleteTile),
            .replaceLayer = input.actionDown(InputAction::EditorReplaceTile),
            .moving = input.actionDown(InputAction::EditorMoveTile),
            .translateGizmoPressed =
                pressed(InputAction::EditorGizmoTranslate),
            .rotateGizmoPressed = pressed(InputAction::EditorGizmoRotate),
            .scaleGizmoPressed = pressed(InputAction::EditorGizmoScale),
            .pointerCaptured = context.mouseCaptured,
        };
    }
    return frame;
}

} // namespace sokoban
