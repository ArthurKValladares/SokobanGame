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
};

struct ChoiceOption {
    int value = 0;
    std::string_view label;
};

struct SegmentedControlOptions {
    bool focused = false;
    bool selectPrevious = false;
    bool selectNext = false;
};

[[nodiscard]] bool button(
    UiContext& ui,
    std::string_view id,
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
    bool enabled = true);
[[nodiscard]] bool checkbox(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    std::string_view label,
    bool& value,
    bool focused = false,
    bool activate = false);
[[nodiscard]] bool segmentedControl(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    std::span<const ChoiceOption> choices,
    int& selectedValue,
    SegmentedControlOptions options = {});
[[nodiscard]] bool choiceStepper(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    std::span<const std::string_view> labels,
    int& selected,
    bool focused = false);

} // namespace sokoban::uiControls
