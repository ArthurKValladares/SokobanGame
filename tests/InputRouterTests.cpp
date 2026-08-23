#include "engine/InputRouter.hpp"

#include <SDL3/SDL.h>

#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

SDL_Event keyEvent(Uint32 type, SDL_Scancode scancode)
{
    SDL_Event event {};
    event.type = type;
    event.key.scancode = scancode;
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
    CHECK(!result.forwardedToInput);
    CHECK(!input.keyDown(SDL_SCANCODE_BACKSPACE));

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

void testModalFrameRouting()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_W);
    pressKey(router, input, SDL_SCANCODE_T);
    pressKey(router, input, SDL_SCANCODE_TAB);
    pressKey(router, input, SDL_SCANCODE_V);
    pressKey(router, input, SDL_SCANCODE_SPACE);

    sokoban::InputRouter::Frame frame = router.routeFrame(
        input,
        { .optionsOpen = true, .titleOpen = true });
    CHECK(frame.options.up);
    CHECK(frame.options.confirm);
    CHECK(!frame.title.up);
    CHECK(!frame.gameplay.up.pressed);
    CHECK(!frame.gameplay.interactPressed);
    CHECK(!frame.showTopDownView);
    CHECK(!frame.showOverworldMap);
    CHECK(!frame.previewScreen);

    frame = router.routeFrame(input, { .titleOpen = true });
    CHECK(frame.title.up);
    CHECK(frame.title.confirm);
    CHECK(!frame.options.up);
    CHECK(!frame.gameplay.up.pressed);
    CHECK(!frame.gameplay.interactPressed);
    CHECK(!frame.showTopDownView);
    CHECK(!frame.showOverworldMap);
    CHECK(!frame.previewScreen);

    frame = router.routeFrame(input, {});
    CHECK(frame.gameplay.up.pressed);
    CHECK(frame.gameplay.up.down);
    CHECK(frame.gameplay.interactPressed);
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

    CHECK(router.backAction(input, { .draftPlaying = true }) ==
        sokoban::InputRouter::BackAction::OpenDraftConfirmation);
    CHECK(router.backAction(
        input,
        { .draftPlaying = true, .draftExitConfirmationOpen = true }) ==
        sokoban::InputRouter::BackAction::CloseDraftConfirmation);
    CHECK(router.backAction(input, {}) ==
        sokoban::InputRouter::BackAction::ShellBack);

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

} // namespace

int main()
{
    testBindingCaptureAndUiCaptureAdmission();
    testModalFrameRouting();
    testBackPriority();
    testEditorFrameUsesConfiguredControls();
    testEditorFrameRespectsRemappedTileControls();
    testEditorPointerExposesPressAndHold();
    testEditorGizmoShortcutsRespectKeyboardCapture();

    if (failures == 0) {
        std::cout << "InputRouterTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "InputRouterTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
