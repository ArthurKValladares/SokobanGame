#pragma once

#include "engine/ui/Ui.hpp"

#include <span>
#include <string_view>

namespace sokoban::uiControls {

enum class ButtonTone {
    Normal,
    Accent,
    Danger,
};

struct ButtonOptions {
    ButtonTone tone = ButtonTone::Normal;
    bool focused = false;
    bool activate = false;
    bool enabled = true;
    // Scales internal strokes and type when a responsive layout shortens the
    // supplied rectangle. Hit testing always uses the full rectangle.
    float contentScale = 1.0f;
};

struct ChoiceOption {
    int value = 0;
    std::string_view label;
};

struct SegmentedControlOptions {
    bool focused = false;
    bool selectPrevious = false;
    bool selectNext = false;
    float contentScale = 1.0f;
};

[[nodiscard]] bool button(
    UiContext& ui,
    UiRect rect,
    std::string_view label,
    ButtonOptions options = {});
[[nodiscard]] bool slider(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    float& value,
    float minimum,
    float maximum,
    bool focused = false,
    bool enabled = true,
    float contentScale = 1.0f);
[[nodiscard]] bool checkbox(
    UiContext& ui,
    UiRect rect,
    std::string_view label,
    bool& value,
    bool focused = false,
    bool activate = false,
    float contentScale = 1.0f);
[[nodiscard]] bool segmentedControl(
    UiContext& ui,
    UiRect rect,
    std::span<const ChoiceOption> choices,
    int& selectedValue,
    SegmentedControlOptions options = {});
[[nodiscard]] bool choiceStepper(
    UiContext& ui,
    UiRect rect,
    std::span<const ChoiceOption> choices,
    int& selectedValue,
    bool focused = false,
    float contentScale = 1.0f);

} // namespace sokoban::uiControls
