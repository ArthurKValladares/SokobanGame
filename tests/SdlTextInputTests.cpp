#include "TestHarness.hpp"

#include "engine/ui/SdlTextInput.hpp"
#include "engine/ui/UiTextEdit.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <string_view>

namespace {

using namespace sokoban;

SDL_Event keyEvent(SDL_WindowID windowId, SDL_Keycode key,
    SDL_Keymod modifiers = SDL_KMOD_NONE, bool repeat = false)
{
    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = windowId;
    event.key.key = key;
    event.key.mod = modifiers;
    event.key.repeat = repeat;
    return event;
}

SDL_Event textEvent(SDL_WindowID windowId, const char* text)
{
    SDL_Event event {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowId;
    event.text.text = text;
    return event;
}

SDL_Event editingEvent(SDL_WindowID windowId, const char* text, int start, int length)
{
    SDL_Event event {};
    event.type = SDL_EVENT_TEXT_EDITING;
    event.edit.windowID = windowId;
    event.edit.text = text;
    event.edit.start = start;
    event.edit.length = length;
    return event;
}

void testTextInput(SDL_Window* window)
{
    TEST("nativeTextInputOwnsItsWindowAndHandlesIme");
    const SDL_WindowID windowId = SDL_GetWindowID(window);
    UiTextEdit editor;
    SdlTextInput input;
    const SDL_Rect area { 15, 22, 160, 28 };
    input.setTarget(window, &editor, area, 19);
    CHECK(SDL_TextInputActive(window));
    SDL_Rect imeArea {};
    int caretX = 0;
    CHECK(SDL_GetTextInputArea(window, &imeArea, &caretX));
    CHECK(SDL_RectsEqual(&area, &imeArea) && caretX == 19);
    input.setTarget(window, &editor, area, 44);
    CHECK(SDL_GetTextInputArea(window, &imeArea, &caretX));
    CHECK(caretX == 44);

    using Result = SdlTextInputResult;
    CHECK(input.handleEvent(textEvent(windowId + 1, "other window")) == Result::Ignored);
    CHECK(input.handleEvent(keyEvent(windowId, SDLK_E)) == Result::Consumed);
    CHECK(editor.text().empty());
    CHECK(input.handleEvent(textEvent(windowId, "eg")) == Result::Consumed);
    CHECK(editor.text() == "eg");

    editor.selectAll();
    constexpr const char* kana = "\xe3\x81\x8b\xe3\x81\xaa";
    CHECK(input.handleEvent(editingEvent(windowId, kana, 1, 1)) == Result::Consumed);
    CHECK(editor.composition().text == kana);
    CHECK(editor.composition().selection.start == 3);
    CHECK(editor.composition().selection.end == 6);
    CHECK(editor.text() == "eg");
    CHECK(input.handleEvent(keyEvent(windowId, SDLK_RETURN)) == Result::Consumed);
    CHECK(SDL_TextInputActive(window));
    CHECK(input.handleEvent(textEvent(windowId, kana)) == Result::Consumed);
    CHECK(editor.composition().text.empty());
    CHECK(editor.text() == kana);

    CHECK(input.handleEvent(editingEvent(windowId, kana, 1, 0)) == Result::Consumed);
    CHECK(input.handleEvent(keyEvent(windowId, SDLK_ESCAPE)) == Result::Consumed);
    CHECK(editor.composition().text.empty());
    CHECK(input.handleEvent(keyEvent(windowId, SDLK_ESCAPE, SDL_KMOD_NONE, true)) ==
        Result::Consumed);
    CHECK(SDL_TextInputActive(window));
    CHECK(input.handleEvent(editingEvent(windowId, kana, 1, 0)) == Result::Consumed);
    editor.setCaret(0);
    input.setTarget(window, &editor, area, 0);
    CHECK(input.handleEvent(keyEvent(windowId, SDLK_KP_ENTER)) == Result::Submit);
    CHECK(!SDL_TextInputActive(window));
    CHECK(input.handleEvent(textEvent(windowId, "late text")) == Result::Ignored);
    CHECK(editor.text() == kana);
}

void testEditingKeysAndClipboard(SDL_Window* window)
{
    TEST("nativeTextInputAppliesEditingShortcutsAndReleasesFocus");
    const SDL_WindowID windowId = SDL_GetWindowID(window);
    UiTextEdit editor;
    editor.setText("abcd");
    SdlTextInput input;
    input.setTarget(window, &editor);

    using Result = SdlTextInputResult;
    const auto press = [&](SDL_Keycode key, SDL_Keymod modifiers = SDL_KMOD_NONE,
                           bool repeat = false) {
        return input.handleEvent(keyEvent(windowId, key, modifiers, repeat));
    };
#ifdef __APPLE__
    constexpr SDL_Keymod shortcut = SDL_KMOD_GUI;
#else
    constexpr SDL_Keymod shortcut = SDL_KMOD_CTRL;
#endif
    CHECK(press(SDLK_END) == Result::Consumed);
    CHECK(press(SDLK_LEFT, SDL_KMOD_SHIFT) == Result::Consumed);
    CHECK(press(SDLK_LEFT, SDL_KMOD_SHIFT, true) == Result::Consumed);
    CHECK(editor.selectedText() == "cd");
    CHECK(press(SDLK_C, shortcut) == Result::Consumed);
    const std::unique_ptr<char, decltype(&SDL_free)> copied(SDL_GetClipboardText(), SDL_free);
    CHECK(copied && std::string_view(copied.get()) == "cd");
    CHECK(editor.text() == "abcd");
    CHECK(press(SDLK_X, shortcut) == Result::Consumed);
    CHECK(editor.text() == "ab");
    CHECK(press(SDLK_V, shortcut) == Result::Consumed);
    CHECK(editor.text() == "abcd");
    CHECK(press(SDLK_BACKSPACE, SDL_KMOD_NONE, true) == Result::Consumed);
    CHECK(editor.text() == "abc");
    CHECK(press(SDLK_HOME) == Result::Consumed);
    CHECK(press(SDLK_DELETE) == Result::Consumed);
    CHECK(editor.text() == "bc");
    CHECK(press(SDLK_A, shortcut) == Result::Consumed);
    CHECK(editor.selectedText() == "bc");
    editor.setCaret(1);
    CHECK(press(SDLK_A, shortcut | SDL_KMOD_ALT) == Result::Consumed);
    CHECK(editor.selectedText().empty());

    SDL_Event release = keyEvent(windowId, SDLK_LEFT);
    release.type = SDL_EVENT_KEY_UP;
    CHECK(input.handleEvent(release) == Result::Ignored);
    CHECK(press(SDLK_TAB) == Result::Blur);
    CHECK(!SDL_TextInputActive(window));

    input.setTarget(window, &editor);
    CHECK(input.handleEvent(editingEvent(windowId, "preedit", 0, 0)) == Result::Consumed);
    SDL_Event focusLost {};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focusLost.window.windowID = windowId;
    CHECK(input.handleEvent(focusLost) == Result::Ignored);
    CHECK(editor.composition().text.empty());
    CHECK(!SDL_TextInputActive(window));
}

} // namespace

int main()
{
    if (!SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE) ||
        !SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL test initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    struct SdlSession { ~SdlSession() { SDL_Quit(); } } session;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Native text input test", 320, 200, SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    if (!window) {
        std::cerr << "SDL test window failed: " << SDL_GetError() << '\n';
        return 1;
    }
    testTextInput(window.get());
    testEditingKeysAndClipboard(window.get());
    return failures != 0;
}
