#include "engine/ui/SdlTextInput.hpp"

#include "engine/Log.hpp"
#include "engine/ui/UiTextEdit.hpp"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

bool startSingleLineInput(SDL_Window* window)
{
    const SDL_PropertiesID properties = SDL_CreateProperties();
    const bool singleLine = properties &&
        SDL_SetBooleanProperty(properties, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
    const bool started = singleLine
        ? SDL_StartTextInputWithProperties(window, properties)
        : SDL_StartTextInput(window);
    if (properties) {
        SDL_DestroyProperties(properties);
    }
    return started;
}

bool hasClipboardModifier(SDL_Keymod modifiers)
{
#ifdef __APPLE__
    constexpr SDL_Keymod shortcut = SDL_KMOD_GUI;
#else
    constexpr SDL_Keymod shortcut = SDL_KMOD_CTRL;
#endif
    return (modifiers & shortcut) != 0 &&
        (modifiers & (SDL_KMOD_ALT | SDL_KMOD_MODE)) == 0;
}

void copySelection(UiTextEdit& target, bool cut)
{
    const std::string selection(target.selectedText());
    if (!selection.empty() && SDL_SetClipboardText(selection.c_str()) && cut) {
        (void)target.eraseSelection();
    }
}

void paste(UiTextEdit& target)
{
    const std::unique_ptr<char, decltype(&SDL_free)> text(
        SDL_GetClipboardText(), SDL_free);
    if (text && text.get()[0] != '\0') {
        (void)target.insert(text.get());
    }
}

void setComposition(UiTextEdit& target, const SDL_TextEditingEvent& event)
{
    const std::string_view text = event.text ? event.text : "";
    const char* backend = SDL_GetCurrentVideoDriver();
    // SDL's Windows IME forwards offsets into a UTF-16 buffer.
    const UiTextOffsetUnit unit = backend && std::string_view(backend) == "windows"
        ? UiTextOffsetUnit::Utf16 : UiTextOffsetUnit::Codepoints;
    target.setComposition(text, event.start, event.length, unit);
}

} // namespace

SdlTextInput::~SdlTextInput()
{
    if (window_) {
        (void)SDL_StopTextInput(window_);
    }
}

void SdlTextInput::setTarget(
    SDL_Window* window, UiTextEdit* target, SDL_Rect area, int caretX)
{
    if (!target) {
        window = nullptr;
    }
    if (!window) {
        target = nullptr;
    }
    const bool changedTarget = target_ != target || window_ != window;
    if (changedTarget || (compositionActive_ && target_->composition().text.empty())) {
        clearComposition();
    }
    if (window_ && window_ != window) {
        (void)SDL_StopTextInput(window_);
        (void)SDL_SetTextInputArea(window_, nullptr, 0);
    }
    window_ = window;
    target_ = target;
    if (!window_) {
        windowId_ = 0;
        return;
    }

    const bool alreadyActive = SDL_TextInputActive(window_);
    caretX = std::clamp(caretX, 0, std::max(area.w, 0));
    if (changedTarget || !alreadyActive ||
        !SDL_RectsEqual(&area_, &area) || caretX_ != caretX) {
        windowId_ = SDL_GetWindowID(window_);
        area_ = area;
        caretX_ = caretX;
        (void)SDL_SetTextInputArea(window_, &area_, caretX_);
    }
    if (!alreadyActive && !startSingleLineInput(window_) && changedTarget) {
        log::warning(log::Category::Input)
            << "Could not start text input: " << SDL_GetError();
    }
}

void SdlTextInput::clearComposition()
{
    if (target_) {
        target_->clearComposition();
    }
    compositionActive_ = false;
    if (window_) {
        (void)SDL_ClearComposition(window_);
    }
}

SdlTextInputResult SdlTextInput::handleEvent(const SDL_Event& event)
{
    if (!target_) {
        return SdlTextInputResult::Ignored;
    }
    switch (event.type) {
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.windowID == windowId_) {
            compositionActive_ = false;
            target_->clearComposition();
            if (event.text.text) {
                (void)target_->insert(event.text.text);
            }
            return SdlTextInputResult::Consumed;
        }
        break;
    case SDL_EVENT_TEXT_EDITING:
        if (event.edit.windowID == windowId_) {
            setComposition(*target_, event.edit);
            compositionActive_ = !target_->composition().text.empty();
            return SdlTextInputResult::Consumed;
        }
        break;
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        if (event.edit_candidates.windowID == windowId_) {
            return SdlTextInputResult::Consumed;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.windowID == windowId_) {
            return handleKeyDown(event.key);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event.window.windowID == windowId_) {
            setTarget(nullptr, nullptr);
        }
        break;
    default:
        break;
    }
    return SdlTextInputResult::Ignored;
}

SdlTextInputResult SdlTextInput::handleKeyDown(const SDL_KeyboardEvent& key)
{
    if (compositionActive_) {
        if (key.key == SDLK_ESCAPE && !key.repeat) {
            clearComposition();
        }
        // While preedit is active, editing commands belong to the IME.
        return SdlTextInputResult::Consumed;
    }

    if (hasClipboardModifier(key.mod)) {
        switch (key.key) {
        case SDLK_A:
            target_->selectAll();
            return SdlTextInputResult::Consumed;
        case SDLK_C:
        case SDLK_X:
            if (!key.repeat) {
                copySelection(*target_, key.key == SDLK_X);
            }
            return SdlTextInputResult::Consumed;
        case SDLK_V:
            if (!key.repeat) {
                paste(*target_);
            }
            return SdlTextInputResult::Consumed;
        default:
            break;
        }
    }

    const bool extend = (key.mod & SDL_KMOD_SHIFT) != 0;
    switch (key.key) {
    case SDLK_LEFT: target_->moveCaret(UiTextMovement::Previous, extend); break;
    case SDLK_RIGHT: target_->moveCaret(UiTextMovement::Next, extend); break;
    case SDLK_HOME: target_->moveCaret(UiTextMovement::Home, extend); break;
    case SDLK_END: target_->moveCaret(UiTextMovement::End, extend); break;
    case SDLK_BACKSPACE: (void)target_->backspace(); break;
    case SDLK_DELETE: (void)target_->deleteForward(); break;
    case SDLK_ESCAPE:
    case SDLK_TAB:
        if (!key.repeat) {
            setTarget(nullptr, nullptr);
            return SdlTextInputResult::Blur;
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!key.repeat) {
            setTarget(nullptr, nullptr);
            return SdlTextInputResult::Submit;
        }
        break;
    default:
        break;
    }
    // Printable input arrives separately as SDL_EVENT_TEXT_INPUT. Its
    // physical key must still never become a game or shell action.
    return SdlTextInputResult::Consumed;
}

} // namespace sokoban
