#pragma once

#include "engine/ui/Ui.hpp"
#include "engine/ui/UiTextEdit.hpp"
#include "engine/ui/UiTextField.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sokoban {

struct UiColorPickerInput {
    UiNavigation navigation;
    bool extendTextSelection = false;
    int horizontalStep = 0;
    int verticalStep = 0;
};

struct UiColorPickerAction {
    enum class Command { None, Cancel, Apply, CopyHex };

    Command command = Command::None;
    Vec3 color {};
    std::string hex;
};

// A native modal whose pending color belongs to the picker until Apply.
class UiColorPicker {
public:
    void open(Vec3 initial, std::string_view title = "Group Color");
    void setError(std::string_view error);
    [[nodiscard]] UiColorPickerAction draw(UiContext& ui, Vec2 viewport, float scale,
        const UiColorPickerInput& input = {});

    // Call before dispatching text events so clicks and typing in the same
    // event poll reach the intended field.
    [[nodiscard]] bool handleTextPress(UiContext& ui, Vec2 position, bool extendSelection);
    [[nodiscard]] UiTextEdit* activeTextEdit();
    [[nodiscard]] bool textFocused() const { return activeField_.has_value(); }
    [[nodiscard]] UiRect textCaret() const { return caret_; }
    void submitText();
    void blurText();
    void endAdjustment() { adjusting_ = false; }

private:
    static constexpr std::size_t fieldCount = 5;

    void focusField(std::size_t index);
    bool updateField(bool showError);
    void resetFieldFocus();
    void syncFields();
    void setHsv(float hue, float saturation, float value);

    Vec3 color_ {};
    float hue_ = 0.0f;
    float saturation_ = 0.0f;
    float value_ = 0.0f;
    std::array<UiTextEdit, fieldCount> edits_ { UiTextEdit(64), UiTextEdit(64), UiTextEdit(64),
        UiTextEdit(64), UiTextEdit(64) };
    std::array<UiTextField, fieldCount> fields_;
    std::array<UiRect, fieldCount> fieldBounds_ {};
    std::optional<std::size_t> activeField_;
    std::string parsedText_;
    std::string title_;
    std::string error_;
    UiControlId focus_ = 0;
    UiRect caret_ {};
    float textSize_ = 16.0f;
    bool adjusting_ = false;
    bool pressHandled_ = false;
    bool fieldError_ = false;
};

} // namespace sokoban
