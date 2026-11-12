#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>

namespace sokoban {

class UiTextEdit;

enum class SdlTextInputResult {
    Ignored,
    Consumed,
    Submit,
    Blur,
};

// Owns SDL text input while a native text field has focus. The caller keeps
// the window alive through destruction and the model alive until detached.
// Call from the main thread. The area and caret offset use SDL window units.
class SdlTextInput {
public:
    SdlTextInput() = default;
    ~SdlTextInput();

    SdlTextInput(const SdlTextInput&) = delete;
    SdlTextInput& operator=(const SdlTextInput&) = delete;

    void setTarget(
        SDL_Window* window, UiTextEdit* target,
        SDL_Rect area = {}, int caretX = 0);

    // Keyboard releases and focus-loss events pass through so the application's
    // usual routing still clears physical input state. Submit and Blur
    // consume the event and detach the target.
    [[nodiscard]] SdlTextInputResult handleEvent(const SDL_Event& event);

private:
    void clearComposition();
    [[nodiscard]] SdlTextInputResult handleKeyDown(const SDL_KeyboardEvent& key);

    SDL_Window* window_ = nullptr;
    SDL_WindowID windowId_ = 0;
    UiTextEdit* target_ = nullptr;
    SDL_Rect area_ {};
    int caretX_ = 0;
    bool compositionActive_ = false;
};

} // namespace sokoban
