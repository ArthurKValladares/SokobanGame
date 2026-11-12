#pragma once

#include "engine/ui/Ui.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace sokoban {

class UiTextEdit;

struct UiTextFieldOptions {
    bool focused = false;
    bool enabled = true;
    bool extendSelection = false;
    bool pressAlreadyHandled = false;
    std::string_view placeholder {};
    float textSize = 18.0f;
};

struct UiTextFieldResult {
    bool pressed = false;
    UiRect caret {}; // Visible caret in UI viewport pixels, for the IME candidate area.
};

// One presentation state per field. The caller owns focus and the edit model.
class UiTextField {
public:
    // Use the last drawn bounds and text size to handle a press before
    // dispatching subsequent text events in the same poll.
    void placeCaret(UiContext& ui, UiRect bounds, UiTextEdit& edit, float textSize,
        Vec2 pointer, bool extend);
    [[nodiscard]] UiTextFieldResult draw(UiContext& ui, std::string_view id, UiRect bounds,
        UiTextEdit& edit, UiTextFieldOptions options = {});

private:
    float scrollX_ = 0.0f;
    std::optional<float> lastDragX_;
    // A bidi boundary can have two visual locations for the same byte offset.
    std::optional<std::size_t> trailingCaret_;
};

} // namespace sokoban
