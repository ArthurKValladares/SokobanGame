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
    const bool editorEditModifier = keyboardEvent &&
        context.editorEditing &&
        (event.key.scancode == SDL_SCANCODE_R ||
            event.key.scancode == SDL_SCANCODE_D);
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
    const bool suppressForBindingCapture = context.bindingCapture &&
        ((keyboardEvent && !menuBackKey) ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
            event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION);

    if (!suppressForBindingCapture && allowKeyboard && allowMouse) {
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
    const bool shellOpen =
        context.optionsOpen || context.titleOpen || context.overlayOpen;
    const bool gameplayActive = !shellOpen &&
        !context.editorEditing &&
        !context.draftExitConfirmationOpen;

    Frame frame;
    if (gameplayActive) {
        frame.gameplay = {
            .up = buttonState(input, InputAction::MoveUp),
            .down = buttonState(input, InputAction::MoveDown),
            .left = buttonState(input, InputAction::MoveLeft),
            .right = buttonState(input, InputAction::MoveRight),
            .undoPressed = input.actionPressed(InputAction::Undo),
            .undoDown = input.actionDown(InputAction::Undo),
            .restartPressed = input.actionPressed(InputAction::Restart),
        };
    }
    if (context.titleOpen && !context.optionsOpen) {
        frame.title = { up, down, left, right, confirm };
    }
    if (context.overlayOpen && !context.optionsOpen) {
        frame.overlay = { up, down, confirm };
    }
    if (context.optionsOpen) {
        frame.options = { up, down, left, right, confirm };
    }

    frame.pointer = {
        .position = input.mousePosition(),
        .primaryDown = input.mouseButtonDown(SDL_BUTTON_LEFT),
        .primaryPressed = input.mouseButtonPressed(SDL_BUTTON_LEFT),
    };
    if (context.editorEditing && !shellOpen) {
        frame.editor = {
            .pointerPosition = input.mousePosition(),
            .primaryPressed = input.mouseButtonPressed(SDL_BUTTON_LEFT),
            .undoPressed = input.keyPressed(SDL_SCANCODE_Z),
            .deleting = input.keyDown(SDL_SCANCODE_D),
            .replaceLayer = input.keyDown(SDL_SCANCODE_R),
            .pointerCaptured = context.mouseCaptured,
        };
    }
    return frame;
}

} // namespace sokoban
