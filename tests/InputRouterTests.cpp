#include "TestHarness.hpp"

#include "engine/InputRouter.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <optional>

namespace {

SDL_Event keyEvent(Uint32 type, SDL_Scancode scancode)
{
    SDL_Event event {};
    event.type = type;
    event.key.scancode = scancode;
    return event;
}

SDL_Event gamepadButtonEvent(Uint32 type, SDL_GamepadButton button)
{
    SDL_Event event {};
    event.type = type;
    event.gbutton.which = 42;
    event.gbutton.button = static_cast<Uint8>(button);
    return event;
}

SDL_Event gamepadAxisEvent(SDL_GamepadAxis axis, Sint16 value)
{
    SDL_Event event {};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = 42;
    event.gaxis.axis = static_cast<Uint8>(axis);
    event.gaxis.value = value;
    return event;
}

void pressKey(
    sokoban::InputRouter& router,
    sokoban::InputState& input,
    SDL_Scancode scancode)
{
    (void)router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, scancode), input, {});
}

void testBindingCaptureAndUiCaptureAdmission()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();

    sokoban::InputRouter::EventResult result = router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_BACKSPACE),
        input,
        { .bindingCapture = true });
    CHECK(result.bindingCandidate.has_value());
    CHECK(result.forwardedToInput);
    CHECK(input.keyDown(SDL_SCANCODE_BACKSPACE));
    CHECK(!input.keyPressed(SDL_SCANCODE_BACKSPACE));

    result = router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_ESCAPE),
        input,
        { .bindingCapture = true, .keyboardCaptured = true });
    CHECK(result.bindingCandidate.has_value());
    CHECK(result.forwardedToInput);
    CHECK(input.actionPressed(sokoban::InputAction::MenuBack));

    input.beginFrame();
    result = router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Z),
        input,
        { .keyboardCaptured = true, .editorEditing = true });
    CHECK(!result.forwardedToInput);
    result = router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_D),
        input,
        { .keyboardCaptured = true, .editorEditing = true });
    CHECK(result.forwardedToInput);
}

void testBindingCaptureSynchronizesKeyboardRelease()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_W);
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));

    input.beginFrame();
    const sokoban::InputRouter::EventResult release = router.routeEvent(
        keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_W),
        input,
        { .bindingCapture = true });
    CHECK(release.forwardedToInput);
    CHECK(!release.bindingCandidate.has_value());
    CHECK(!input.actionDown(sokoban::InputAction::MoveUp));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveUp));

    // Completion and cancellation both leave capture by changing the routing
    // context. Neither transition can restore the released key.
    CHECK(!router.routeFrame(input, {}).gameplay.up.down);
    CHECK(!router.routeFrame(input, { .optionsOpen = true }).options.up);
}

void testBindingCaptureSynchronizesGamepadButtonStateWithoutAnEdge()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    (void)router.routeEvent(
        gamepadButtonEvent(
            SDL_EVENT_GAMEPAD_BUTTON_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_UP),
        input,
        {});
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));

    input.beginFrame();
    (void)router.routeEvent(
        gamepadButtonEvent(
            SDL_EVENT_GAMEPAD_BUTTON_UP,
            SDL_GAMEPAD_BUTTON_DPAD_UP),
        input,
        { .bindingCapture = true });
    CHECK(!input.actionDown(sokoban::InputAction::MoveUp));

    (void)router.routeEvent(
        gamepadButtonEvent(
            SDL_EVENT_GAMEPAD_BUTTON_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_UP),
        input,
        { .bindingCapture = true });
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveUp));

    input.beginFrame();
    CHECK(input.actionDown(sokoban::InputAction::MoveUp));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveUp));
}

void testBindingCaptureSynchronizesAxisStateWithoutAnEdge()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    (void)router.routeEvent(
        gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, -24000), input, {});
    CHECK(input.actionDown(sokoban::InputAction::MoveLeft));

    input.beginFrame();
    (void)router.routeEvent(
        gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, 0),
        input,
        { .bindingCapture = true });
    CHECK(!input.actionDown(sokoban::InputAction::MoveLeft));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveRight));

    (void)router.routeEvent(
        gamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, 24000),
        input,
        { .bindingCapture = true });
    CHECK(input.actionDown(sokoban::InputAction::MoveRight));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveRight));

    input.beginFrame();
    CHECK(input.actionDown(sokoban::InputAction::MoveRight));
    CHECK(!input.actionPressed(sokoban::InputAction::MoveRight));
}

void testModalFrameRouting()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_W);
    pressKey(router, input, SDL_SCANCODE_T);
    pressKey(router, input, SDL_SCANCODE_TAB);
    pressKey(router, input, SDL_SCANCODE_V);
    pressKey(router, input, SDL_SCANCODE_Q);
    pressKey(router, input, SDL_SCANCODE_SPACE);

    sokoban::InputRouter::Frame frame = router.routeFrame(
        input,
        { .optionsOpen = true, .titleOpen = true });
    CHECK(frame.options.up);
    CHECK(frame.options.confirm);
    CHECK(!frame.title.up);
    CHECK(!frame.gameplay.up.pressed);
    CHECK(!frame.gameplay.interactPressed);
    CHECK(!frame.gameplay.cycleHeroPressed);
    CHECK(!frame.showTopDownView);
    CHECK(!frame.showOverworldMap);
    CHECK(!frame.previewScreen);

    frame = router.routeFrame(input, { .titleOpen = true });
    CHECK(frame.title.up);
    CHECK(frame.title.confirm);
    CHECK(!frame.options.up);
    CHECK(!frame.gameplay.up.pressed);
    CHECK(!frame.gameplay.interactPressed);
    CHECK(!frame.gameplay.cycleHeroPressed);
    CHECK(!frame.showTopDownView);
    CHECK(!frame.showOverworldMap);
    CHECK(!frame.previewScreen);

    frame = router.routeFrame(input, {});
    CHECK(frame.gameplay.up.pressed);
    CHECK(frame.gameplay.up.down);
    CHECK(frame.gameplay.interactPressed);
    CHECK(frame.gameplay.cycleHeroPressed);
    CHECK(frame.showTopDownView);
    CHECK(frame.showOverworldMap);
    CHECK(frame.previewScreen);
}

void testBackPriority()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_ESCAPE);

    CHECK(router.backAction(input, { .draftPlaying = true, .lecternOpen = true }) ==
        sokoban::InputRouter::BackAction::None);
    CHECK(router.routeFrame(input, { .lecternOpen = true }).gameplay.dismissPressed);
    CHECK(router.backAction(input, { .draftPlaying = true }) ==
        sokoban::InputRouter::BackAction::OpenDraftConfirmation);
    CHECK(router.backAction(
        input,
        { .draftPlaying = true, .draftExitConfirmationOpen = true }) ==
        sokoban::InputRouter::BackAction::CloseDraftConfirmation);
    CHECK(router.backAction(input, {}) ==
        sokoban::InputRouter::BackAction::ShellBack);
    CHECK(router.backAction(
        input,
        { .editorEditing = true, .decorationPlacementReady = true }) ==
        sokoban::InputRouter::BackAction::CancelDecorationPlacement);

    input.beginFrame();
    CHECK(router.backAction(input, {}) ==
        sokoban::InputRouter::BackAction::None);
}

void testEditorFrameUsesConfiguredControls()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_Z);
    pressKey(router, input, SDL_SCANCODE_D);
    pressKey(router, input, SDL_SCANCODE_R);
    pressKey(router, input, SDL_SCANCODE_M);
    pressKey(router, input, SDL_SCANCODE_T);
    pressKey(router, input, SDL_SCANCODE_S);

    const sokoban::InputRouter::Frame frame = router.routeFrame(
        input,
        { .editorEditing = true, .mouseCaptured = true });
    CHECK(frame.editor.undoPressed);
    CHECK(frame.editor.deleting);
    CHECK(frame.editor.replaceLayer);
    CHECK(frame.editor.moving);
    CHECK(frame.editor.rotateGizmoPressed);
    CHECK(frame.editor.translateGizmoPressed);
    CHECK(frame.editor.scaleGizmoPressed);
    CHECK(frame.editor.pointerCaptured);
    CHECK(!frame.gameplay.undoPressed);
}

void testEditorFrameRespectsRemappedTileControls()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    sokoban::assignBinding(
        bindings,
        sokoban::InputAction::EditorMoveTile,
        sokoban::KeyboardBinding { "P" });
    input.setBindings(bindings);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_P);

    const sokoban::InputRouter::Frame frame = router.routeFrame(
        input, { .editorEditing = true });
    CHECK(frame.editor.moving);
}

void testEditorGizmoShortcutsRespectKeyboardCapture()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_R);
    pressKey(router, input, SDL_SCANCODE_T);
    pressKey(router, input, SDL_SCANCODE_S);

    const sokoban::InputRouter::Frame frame = router.routeFrame(
        input,
        { .editorEditing = true, .keyboardCaptured = true });
    CHECK(!frame.editor.rotateGizmoPressed);
    CHECK(!frame.editor.translateGizmoPressed);
    CHECK(!frame.editor.scaleGizmoPressed);
}

void testTileDecorationRotationUsesOneExclusivePress()
{
    TEST("tileDecorationRotationUsesOneExclusivePress");
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    const sokoban::InputRouter::RoutingContext placing {
        .editorEditing = true,
        .decorationPlacementReady = true,
        .tileDecorationPlacementReady = true,
        .editorViewportShortcutsAllowed = true,
    };
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_R);
    auto frame = router.routeFrame(input, placing);
    CHECK(frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);
    CHECK(!frame.gameplay.restartPressed);

    // Holding R, including an OS key-repeat event, must not turn again.
    input.beginFrame();
    frame = router.routeFrame(input, placing);
    CHECK(!frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);
    SDL_Event repeat = keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_R);
    repeat.key.repeat = true;
    (void)router.routeEvent(repeat, input, { .editorEditing = true });
    CHECK(!router.routeFrame(input, placing).editor.rotateTileDecorationPressed);

    (void)router.routeEvent(
        keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_R), input, {});
    pressKey(router, input, SDL_SCANCODE_R);
    frame = router.routeFrame(input, placing);
    CHECK(frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);

    // Canceling placement restores R's existing gizmo behavior. The mesh
    // decoration tool continues to use its gizmo even while a mesh is armed.
    frame = router.routeFrame(input, { .editorEditing = true });
    CHECK(frame.editor.rotateGizmoPressed);
    CHECK(!frame.editor.rotateTileDecorationPressed);
    frame = router.routeFrame(input, {
        .editorEditing = true, .decorationPlacementReady = true,
    });
    CHECK(frame.editor.rotateGizmoPressed);
    CHECK(!frame.editor.rotateTileDecorationPressed);
}

void testTileDecorationRotationRespectsRebindingAndCapture()
{
    TEST("tileDecorationRotationRespectsRebindingAndCapture");
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    sokoban::assignBinding(
        bindings, sokoban::InputAction::EditorGizmoRotate,
        sokoban::KeyboardBinding { "P" });
    input.setBindings(bindings);
    sokoban::InputRouter::RoutingContext placing {
        .editorEditing = true,
        .decorationPlacementReady = true,
        .tileDecorationPlacementReady = true,
        .editorViewportShortcutsAllowed = true,
    };
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_R);
    CHECK(!router.routeFrame(input, placing).editor.rotateTileDecorationPressed);
    pressKey(router, input, SDL_SCANCODE_P);
    auto frame = router.routeFrame(input, placing);
    CHECK(frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);

    placing.keyboardCaptured = true;
    placing.editorViewportShortcutsAllowed = false;
    frame = router.routeFrame(input, placing);
    CHECK(!frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);
    placing.keyboardCaptured = false;
    placing.editorViewportShortcutsAllowed = true;
    placing.optionsOpen = true;
    frame = router.routeFrame(input, placing);
    CHECK(!frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);
    placing.optionsOpen = false;
    placing.titleOpen = true;
    CHECK(!router.routeFrame(input, placing).editor.rotateTileDecorationPressed);
    placing.titleOpen = false;

    // Capture updates physical key state without queuing a rotation for when
    // focus returns to the viewport.
    input.beginFrame();
    (void)router.routeEvent(keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_P), input, {});
    (void)router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_P), input,
        { .bindingCapture = true, .editorEditing = true });
    CHECK(!router.routeFrame(input, placing).editor.rotateTileDecorationPressed);
}

void testTileDecorationRotationAfterPaletteFocus()
{
    TEST("tileDecorationRotationAfterPaletteFocus");
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    sokoban::InputRouter::RoutingContext placing {
        .editorEditing = true,
        .decorationPlacementReady = true,
        .tileDecorationPlacementReady = true,
        // Clicking a palette radio button or Place Selected leaves passive
        // keyboard focus in the panel until the first viewport click.
        .keyboardCaptured = true,
        .editorViewportShortcutsAllowed = true,
    };
    input.beginFrame();
    const auto result = router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_R), input,
        { .keyboardCaptured = true, .editorEditing = true });
    CHECK(result.forwardedToInput);
    SDL_Event click {};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_LEFT;
    (void)router.routeEvent(click, input, {});
    auto frame = router.routeFrame(input, placing);
    CHECK(frame.editor.rotateTileDecorationPressed);
    CHECK(frame.editor.primaryPressed);
    CHECK(!frame.editor.rotateGizmoPressed);
    CHECK(!frame.gameplay.restartPressed);

    // Hovering the palette or an active text/numeric field or popup keeps
    // ownership of the key. Permission only bypasses passive focus.
    placing.editorViewportShortcutsAllowed = false;
    frame = router.routeFrame(input, placing);
    CHECK(!frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);
    placing.keyboardCaptured = false;
    frame = router.routeFrame(input, placing);
    CHECK(!frame.editor.rotateTileDecorationPressed);
    CHECK(!frame.editor.rotateGizmoPressed);

    // Returning to the viewport with R held must not queue a missed turn.
    input.beginFrame();
    placing.editorViewportShortcutsAllowed = true;
    CHECK(!router.routeFrame(input, placing).editor.rotateTileDecorationPressed);
    (void)router.routeEvent(
        keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_R), input, {});
    pressKey(router, input, SDL_SCANCODE_R);
    CHECK(router.routeFrame(input, placing).editor.rotateTileDecorationPressed);
}

void testEditorPointerExposesPressAndHold()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();

    SDL_Event press {};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.button = SDL_BUTTON_LEFT;
    (void)router.routeEvent(press, input, {});

    // On the press frame both are set.
    sokoban::InputRouter::Frame frame =
        router.routeFrame(input, { .editorEditing = true });
    CHECK(frame.editor.primaryPressed);
    CHECK(frame.editor.primaryDown);

    // On the next frame the button is still held but no longer newly pressed.
    // Brush strokes key off the held state: driving them from primaryPressed
    // ends every stroke one frame after it starts, so dragging is impossible
    // and a click paints at most a single dot.
    input.beginFrame();
    frame = router.routeFrame(input, { .editorEditing = true });
    CHECK(!frame.editor.primaryPressed);
    CHECK(frame.editor.primaryDown);

    SDL_Event release {};
    release.type = SDL_EVENT_MOUSE_BUTTON_UP;
    release.button.button = SDL_BUTTON_LEFT;
    (void)router.routeEvent(release, input, {});
    frame = router.routeFrame(input, { .editorEditing = true });
    CHECK(!frame.editor.primaryPressed);
    CHECK(!frame.editor.primaryDown);
}

void testEditorPointerExposesSecondaryPress()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();

    SDL_Event press {};
    press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    press.button.button = SDL_BUTTON_RIGHT;
    (void)router.routeEvent(press, input, {});

    sokoban::InputRouter::Frame frame =
        router.routeFrame(input, { .editorEditing = true });
    CHECK(frame.editor.secondaryPressed);

    input.beginFrame();
    frame = router.routeFrame(input, { .editorEditing = true });
    CHECK(!frame.editor.secondaryPressed);
}

sokoban::InputRouter::Frame editorFrameAfter(
    std::initializer_list<SDL_Scancode> keys,
    sokoban::InputRouter::RoutingContext context = { .editorEditing = true })
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    for (const SDL_Scancode key : keys) {
        pressKey(router, input, key);
    }
    return router.routeFrame(input, context);
}

void testEditorShortcuts()
{
    TEST("editorShortcuts");
    auto frame = editorFrameAfter({ SDL_SCANCODE_LCTRL, SDL_SCANCODE_S });
    CHECK(frame.editor.savePressed);
    CHECK(!frame.editor.scaleGizmoPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_S });
    CHECK(!frame.editor.savePressed);
    CHECK(frame.editor.scaleGizmoPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_Y });
    CHECK(frame.editor.redoPressed);
    CHECK(!frame.editor.undoPressed);

    frame = editorFrameAfter(
        { SDL_SCANCODE_RCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_Z });
    CHECK(frame.editor.redoPressed);
    CHECK(!frame.editor.undoPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z });
    CHECK(frame.editor.undoPressed);
    CHECK(!frame.editor.redoPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_PAGEUP });
    CHECK(frame.editor.layerUpPressed);
    CHECK(!frame.editor.layerDownPressed);
    frame = editorFrameAfter({ SDL_SCANCODE_PAGEDOWN });
    CHECK(frame.editor.layerDownPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_TAB });
    CHECK(frame.editor.cycleToolPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_L });
    CHECK(frame.editor.toggleLayerLockPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_3 });
    CHECK(frame.editor.recentTileSlot == std::optional<std::size_t> { 2 });
    // Extra modifiers do not block a plain key unless a chord claims them.
    frame = editorFrameAfter({ SDL_SCANCODE_LCTRL, SDL_SCANCODE_3 });
    CHECK(frame.editor.recentTileSlot == std::optional<std::size_t> { 2 });

    frame = editorFrameAfter({ SDL_SCANCODE_LALT, SDL_SCANCODE_LSHIFT });
    CHECK(frame.editor.pickModifier);
    CHECK(frame.editor.lineConstraint);
    CHECK(!frame.editor.paintLinkColorModifier);

    // Ctrl alone holds the link-color brush, and still takes part in chords.
    frame = editorFrameAfter({ SDL_SCANCODE_RCTRL });
    CHECK(frame.editor.paintLinkColorModifier);
    CHECK(!frame.editor.pickModifier);
    frame = editorFrameAfter({ SDL_SCANCODE_LCTRL, SDL_SCANCODE_S });
    CHECK(frame.editor.paintLinkColorModifier);
    CHECK(frame.editor.savePressed);

    // A focused text field owns the keyboard.
    frame = editorFrameAfter(
        { SDL_SCANCODE_Y, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_TAB,
            SDL_SCANCODE_1 },
        { .editorEditing = true, .keyboardCaptured = true });
    CHECK(!frame.editor.redoPressed);
    CHECK(!frame.editor.layerUpPressed);
    CHECK(!frame.editor.cycleToolPressed);
    CHECK(!frame.editor.recentTileSlot);

    // Y stays Undo for a player who bound Undo to it.
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    sokoban::assignBinding(
        bindings, sokoban::InputAction::Undo, sokoban::KeyboardBinding { "Y" });
    input.setBindings(bindings);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_Y);
    frame = router.routeFrame(input, { .editorEditing = true });
    CHECK(frame.editor.undoPressed);
    CHECK(!frame.editor.redoPressed);

    // Likewise L stays the delete modifier for a player who put it there.
    sokoban::InputState remapped(false);
    bindings = sokoban::defaultInputBindings();
    sokoban::assignBinding(
        bindings,
        sokoban::InputAction::EditorDeleteTile,
        sokoban::KeyboardBinding { "L" });
    remapped.setBindings(bindings);
    remapped.beginFrame();
    pressKey(router, remapped, SDL_SCANCODE_L);
    frame = router.routeFrame(remapped, { .editorEditing = true });
    CHECK(frame.editor.deleting);
    CHECK(!frame.editor.toggleLayerLockPressed);
}

void testEditorShortcutsFollowRebinding()
{
    TEST("editorShortcutsFollowRebinding");
    sokoban::InputRouter router;
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    sokoban::assignBinding(
        bindings,
        sokoban::InputAction::EditorSave,
        sokoban::KeyboardBinding { "F2" });
    sokoban::assignBinding(
        bindings,
        sokoban::InputAction::EditorRecentTile3,
        sokoban::KeyboardBinding { "3", sokoban::keyModifierCtrl });
    sokoban::assignBinding(
        bindings,
        sokoban::InputAction::EditorPickTile,
        sokoban::KeyboardBinding { "Left Ctrl" });

    const auto frameAfter = [&](std::initializer_list<SDL_Scancode> keys) {
        sokoban::InputState input(false);
        input.setBindings(bindings);
        input.beginFrame();
        for (const SDL_Scancode key : keys) {
            pressKey(router, input, key);
        }
        return router.routeFrame(input, { .editorEditing = true });
    };
    CHECK(frameAfter({ SDL_SCANCODE_F2 }).editor.savePressed);
    CHECK(!frameAfter({ SDL_SCANCODE_LCTRL, SDL_SCANCODE_S })
               .editor.savePressed);
    CHECK(!frameAfter({ SDL_SCANCODE_3 }).editor.recentTileSlot);
    CHECK(frameAfter({ SDL_SCANCODE_LCTRL, SDL_SCANCODE_3 })
              .editor.recentTileSlot == std::optional<std::size_t> { 2 });
    CHECK(frameAfter({ SDL_SCANCODE_LCTRL }).editor.pickModifier);
    CHECK(!frameAfter({ SDL_SCANCODE_LALT }).editor.pickModifier);
    // Taking Left Ctrl for the eyedropper leaves the brush on Right Ctrl.
    CHECK(!frameAfter({ SDL_SCANCODE_LCTRL }).editor.paintLinkColorModifier);
    CHECK(frameAfter({ SDL_SCANCODE_RCTRL }).editor.paintLinkColorModifier);
}

void testDraftPlaybackShortcuts()
{
    TEST("draftPlaybackShortcuts");
    auto frame = editorFrameAfter({ SDL_SCANCODE_F5 });
    CHECK(frame.toggleDraftPlaybackPressed);
    CHECK(!frame.playDraftFromCursorPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_LSHIFT, SDL_SCANCODE_F5 });
    CHECK(frame.playDraftFromCursorPressed);
    CHECK(!frame.toggleDraftPlaybackPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_F5 }, { .draftPlaying = true });
    CHECK(frame.toggleDraftPlaybackPressed);
    // Shift+F5 while playing just stops, like F5.
    frame = editorFrameAfter(
        { SDL_SCANCODE_LSHIFT, SDL_SCANCODE_F5 }, { .draftPlaying = true });
    CHECK(frame.toggleDraftPlaybackPressed);
    CHECK(!frame.playDraftFromCursorPressed);

    frame = editorFrameAfter({ SDL_SCANCODE_F5 }, {});
    CHECK(!frame.toggleDraftPlaybackPressed);
    frame = editorFrameAfter(
        { SDL_SCANCODE_F5 },
        { .editorEditing = true, .keyboardCaptured = true });
    CHECK(!frame.toggleDraftPlaybackPressed);
    frame = editorFrameAfter(
        { SDL_SCANCODE_F5 },
        { .optionsOpen = true, .editorEditing = true });
    CHECK(!frame.toggleDraftPlaybackPressed);
}

void testEditorModifiersPassUiKeyboardCapture()
{
    TEST("editorModifiersPassUiKeyboardCapture");
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    const sokoban::InputRouter::EventContext captured {
        .keyboardCaptured = true,
        .editorEditing = true,
    };
    CHECK(router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LALT), input, captured)
            .forwardedToInput);
    CHECK(router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RSHIFT), input, captured)
            .forwardedToInput);
    CHECK(!router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Y), input, captured)
            .forwardedToInput);
    CHECK(!router.routeEvent(
        keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F5), input, captured)
            .forwardedToInput);
}

} // namespace

int main()
{
    testBindingCaptureAndUiCaptureAdmission();
    testBindingCaptureSynchronizesKeyboardRelease();
    testBindingCaptureSynchronizesGamepadButtonStateWithoutAnEdge();
    testBindingCaptureSynchronizesAxisStateWithoutAnEdge();
    testModalFrameRouting();
    testBackPriority();
    testEditorFrameUsesConfiguredControls();
    testEditorFrameRespectsRemappedTileControls();
    testEditorPointerExposesPressAndHold();
    testEditorPointerExposesSecondaryPress();
    testEditorGizmoShortcutsRespectKeyboardCapture();
    testTileDecorationRotationUsesOneExclusivePress();
    testTileDecorationRotationRespectsRebindingAndCapture();
    testTileDecorationRotationAfterPaletteFocus();
    testEditorShortcuts();
    testDraftPlaybackShortcuts();
    testEditorShortcutsFollowRebinding();
    testEditorModifiersPassUiKeyboardCapture();

    if (failures == 0) {
        std::cout << "InputRouterTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "InputRouterTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
