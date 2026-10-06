#pragma once

#include "engine/ui/Ui.hpp"
#include "engine/ui/InputPrompts.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

class LecternDialog {
public:
    struct PromptContext {
        const InputBindings* bindings = nullptr;
        const InputPromptCatalog* prompts = nullptr;
        BindingDeviceClass deviceClass = BindingDeviceClass::Keyboard;
        GamepadPresentation gamepad;
    };

    void reset() { page_ = 0; pageCount_ = 1; }
    void turnPage(bool previous, bool next);
    // Preserves paragraphs and splits oversized words so every page fits.
    [[nodiscard]] static std::vector<std::string> wrapText(
        std::string_view text, float width,
        const std::function<float(std::string_view)>& measure);
    void draw(UiContext& ui, Vec2 viewport, std::string_view text);
    void draw(UiContext& ui, Vec2 viewport, std::string_view text,
        const PromptContext& context);

private:
    struct Run {
        std::string text;
        std::optional<InputPromptGlyph> glyph;
        float width = 0.0f;
        float scale = 1.0f;
        bool error = false;
    };
    void rebuildLines(UiContext& ui, std::string_view text, float width,
        float fontSize, const PromptContext& context);

    std::size_t page_ = 0;
    std::size_t pageCount_ = 1;
    std::string cachedText_;
    std::vector<std::vector<Run>> lines_;
    float cachedWidth_ = 0.0f;
    float cachedFontSize_ = 0.0f;
    std::optional<InputBindings> cachedBindings_;
    const InputPromptCatalog* cachedPrompts_ = nullptr;
    BindingDeviceClass cachedDeviceClass_ = BindingDeviceClass::Keyboard;
    InputPromptTheme cachedTheme_ = InputPromptTheme::Generic;
    SDL_GamepadType cachedGamepadType_ = SDL_GAMEPAD_TYPE_UNKNOWN;
    std::array<SDL_GamepadButtonLabel, 4> cachedFaceButtonLabels_ {};
};

} // namespace sokoban
