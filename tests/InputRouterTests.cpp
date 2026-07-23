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
    pressKey(router, input, SDL_SCANCODE_RETURN);

    sokoban::InputRouter::Frame frame = router.routeFrame(
        input,
        { .optionsOpen = true, .titleOpen = true });
    CHECK(frame.options.up);
    CHECK(frame.options.confirm);
    CHECK(!frame.title.up);
    CHECK(!frame.gameplay.up.pressed);

    frame = router.routeFrame(input, { .titleOpen = true });
    CHECK(frame.title.up);
    CHECK(frame.title.confirm);
    CHECK(!frame.options.up);
    CHECK(!frame.gameplay.up.pressed);

    frame = router.routeFrame(input, {});
    CHECK(frame.gameplay.up.pressed);
    CHECK(frame.gameplay.up.down);
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

void testEditorFrameUsesRawControls()
{
    sokoban::InputRouter router;
    sokoban::InputState input(false);
    input.beginFrame();
    pressKey(router, input, SDL_SCANCODE_Z);
    pressKey(router, input, SDL_SCANCODE_D);
    pressKey(router, input, SDL_SCANCODE_R);

    const sokoban::InputRouter::Frame frame = router.routeFrame(
        input,
        { .editorEditing = true, .mouseCaptured = true });
    CHECK(frame.editor.undoPressed);
    CHECK(frame.editor.deleting);
    CHECK(frame.editor.replaceLayer);
    CHECK(frame.editor.pointerCaptured);
    CHECK(!frame.gameplay.undoPressed);
}

} // namespace

int main()
{
    testBindingCaptureAndUiCaptureAdmission();
    testModalFrameRouting();
    testBackPriority();
    testEditorFrameUsesRawControls();

    if (failures == 0) {
        std::cout << "InputRouterTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "InputRouterTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
