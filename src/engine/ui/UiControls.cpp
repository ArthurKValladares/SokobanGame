#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace sokoban::uiControls {
namespace {

constexpr Vec4 textColor { 0.92f, 0.94f, 0.92f, 1.0f };
constexpr Vec4 accentColor { 0.18f, 0.62f, 0.58f, 1.0f };

Vec4 buttonColor(ButtonTone tone, bool hovered, bool pressed)
{
    Vec4 base;
    switch (tone) {
    case ButtonTone::Accent: base = { 0.14f, 0.45f, 0.43f, 0.98f }; break;
    case ButtonTone::Danger: base = { 0.50f, 0.17f, 0.14f, 0.98f }; break;
    default: base = { 0.19f, 0.22f, 0.22f, 0.98f }; break;
    }
    const float lift = pressed ? -0.04f : (hovered ? 0.07f : 0.0f);
    base.x = std::clamp(base.x + lift, 0.0f, 1.0f);
    base.y = std::clamp(base.y + lift, 0.0f, 1.0f);
    base.z = std::clamp(base.z + lift, 0.0f, 1.0f);
    return base;
}

} // namespace

bool button(
    UiContext& ui,
    UiRect rect,
    std::string_view label,
    ButtonOptions options)
{
    const float scale = std::clamp(options.contentScale, 0.5f, 1.0f);
    const bool hovered = options.enabled && ui.hovered(rect);
    const bool pressed = hovered && ui.mouseDown();
    const Vec4 border = !options.enabled
        ? Vec4 { 0.24f, 0.26f, 0.26f, 0.65f }
        : (options.focused
                ? Vec4 { 0.30f, 0.80f, 0.72f, 1.0f }
                : Vec4 { 0.32f, 0.35f, 0.35f, 0.94f });
    ui.rect(rect, border);
    ui.rect({
        { rect.position.x + 2.0f * scale, rect.position.y + 2.0f * scale },
        { rect.size.x - 4.0f * scale, rect.size.y - 4.0f * scale },
    }, options.enabled
        ? buttonColor(options.tone, hovered, pressed)
        : Vec4 { 0.14f, 0.16f, 0.16f, 0.72f });
    ui.centeredText(rect, label,
        options.enabled ? textColor : Vec4 { 0.50f, 0.52f, 0.51f, 0.72f },
        24.0f * scale);
    return options.enabled && (ui.clicked(rect) || options.activate);
}

bool slider(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    float& value,
    float minimum,
    float maximum,
    bool focused,
    bool enabled,
    float contentScale)
{
    if (maximum <= minimum) {
        return false;
    }
    const float oldValue = value;
    if (enabled && ui.drag(id, rect)) {
        const float fraction = std::clamp(
            (ui.mousePosition().x - rect.position.x) / rect.size.x,
            0.0f,
            1.0f);
        value = minimum + fraction * (maximum - minimum);
    }
    value = std::clamp(value, minimum, maximum);
    const float fraction = (value - minimum) / (maximum - minimum);
    const float scale = std::clamp(contentScale, 0.5f, 1.0f);
    const float trackHeight = 8.0f * scale;
    const UiRect track {
        { rect.position.x, rect.position.y + (rect.size.y - trackHeight) * 0.5f },
        { rect.size.x, trackHeight },
    };
    const Vec4 trackColor = enabled
        ? Vec4 { 0.20f, 0.23f, 0.23f, 1.0f }
        : Vec4 { 0.20f, 0.22f, 0.22f, 0.45f };
    const Vec4 fillColor = enabled
        ? accentColor
        : Vec4 { 0.34f, 0.38f, 0.37f, 0.45f };
    ui.rect(track, trackColor);
    ui.rect({ track.position, { track.size.x * fraction, track.size.y } }, fillColor);
    const float knobX = rect.position.x + rect.size.x * fraction;
    const float knobWidth = 16.0f * scale;
    const float knobInset = 3.0f * scale;
    ui.rect({
        { knobX - knobWidth * 0.5f, rect.position.y + knobInset },
        { knobWidth, rect.size.y - knobInset * 2.0f },
    }, !enabled
        ? Vec4 { 0.48f, 0.50f, 0.49f, 0.45f }
        : (focused
            ? Vec4 { 0.62f, 0.93f, 0.84f, 1.0f }
            : Vec4 { 0.82f, 0.86f, 0.82f, 1.0f }));
    return std::abs(value - oldValue) > 0.0001f;
}

bool checkbox(
    UiContext& ui,
    UiRect rect,
    std::string_view label,
    bool& value,
    bool focused,
    bool activate,
    float contentScale)
{
    const float scale = std::clamp(contentScale, 0.5f, 1.0f);
    const bool clicked = ui.clicked(rect) || activate;
    if (clicked) {
        value = !value;
    }
    const float boxSize = 26.0f * scale;
    const UiRect box {
        { rect.position.x, rect.position.y + (rect.size.y - boxSize) * 0.5f },
        { boxSize, boxSize },
    };
    ui.rect(box, focused
        ? Vec4 { 0.30f, 0.80f, 0.72f, 1.0f }
        : Vec4 { 0.38f, 0.41f, 0.40f, 1.0f });
    ui.rect({
        { box.position.x + 2.0f * scale, box.position.y + 2.0f * scale },
        { 22.0f * scale, 22.0f * scale },
    }, { 0.12f, 0.14f, 0.14f, 1.0f });
    if (value) {
        ui.rect({
            { box.position.x + 6.0f * scale, box.position.y + 6.0f * scale },
            { 14.0f * scale, 14.0f * scale },
        }, accentColor);
    }
    const float textSize = 23.0f * scale;
    const Vec2 textExtent = ui.measureText(label, textSize);
    ui.text({
        rect.position.x + 40.0f * scale,
        rect.position.y + (rect.size.y - textExtent.y) * 0.5f,
    }, label, textColor, textSize);
    return clicked;
}

bool segmentedControl(
    UiContext& ui,
    UiRect rect,
    std::span<const ChoiceOption> choices,
    int& selectedValue,
    SegmentedControlOptions options)
{
    if (choices.empty()) {
        return false;
    }
    const int oldValue = selectedValue;
    const auto selected = std::ranges::find(
        choices, selectedValue, &ChoiceOption::value);
    int selectedIndex = selected == choices.end()
        ? 0
        : static_cast<int>(selected - choices.begin());
    if (options.selectPrevious) {
        selectedIndex = (selectedIndex + static_cast<int>(choices.size()) - 1) %
            static_cast<int>(choices.size());
    }
    if (options.selectNext) {
        selectedIndex = (selectedIndex + 1) % static_cast<int>(choices.size());
    }
    selectedValue = choices[static_cast<size_t>(selectedIndex)].value;

    const float segmentWidth = rect.size.x / static_cast<float>(choices.size());
    for (size_t index = 0; index < choices.size(); ++index) {
        const UiRect segment {
            { rect.position.x + segmentWidth * static_cast<float>(index), rect.position.y },
            { segmentWidth, rect.size.y },
        };
        if (button(ui, segment, choices[index].label, {
                .tone = static_cast<int>(index) == selectedIndex
                    ? ButtonTone::Accent
                    : ButtonTone::Normal,
                .focused = options.focused &&
                    static_cast<int>(index) == selectedIndex,
                .contentScale = options.contentScale,
            })) {
            selectedIndex = static_cast<int>(index);
            selectedValue = choices[index].value;
        }
    }
    return selectedValue != oldValue;
}

bool choiceStepper(
    UiContext& ui,
    UiRect rect,
    std::span<const ChoiceOption> choices,
    int& selectedValue,
    bool focused,
    float contentScale)
{
    if (choices.empty()) {
        return false;
    }
    const int oldValue = selectedValue;
    const auto selected = std::ranges::find(
        choices, selectedValue, &ChoiceOption::value);
    int selectedIndex = selected == choices.end()
        ? 0
        : static_cast<int>(selected - choices.begin());
    const float scale = std::clamp(contentScale, 0.5f, 1.0f);
    const float arrowWidth = 52.0f * scale;
    if (button(ui, {
            rect.position, { arrowWidth, rect.size.y } }, "<", {
                .focused = focused,
                .contentScale = scale,
            })) {
        selectedIndex =
            (selectedIndex + static_cast<int>(choices.size()) - 1) %
            static_cast<int>(choices.size());
    }
    ui.rect({
        { rect.position.x + arrowWidth + 2.0f, rect.position.y },
        { rect.size.x - arrowWidth * 2.0f - 4.0f, rect.size.y },
    }, { 0.15f, 0.17f, 0.17f, 1.0f });
    ui.centeredText({
        { rect.position.x + arrowWidth, rect.position.y },
        { rect.size.x - arrowWidth * 2.0f, rect.size.y },
    }, choices[static_cast<size_t>(selectedIndex)].label, textColor, 22.0f * scale);
    if (button(ui, {
            { rect.position.x + rect.size.x - arrowWidth, rect.position.y },
            { arrowWidth, rect.size.y } }, ">", {
                .focused = focused,
                .contentScale = scale,
            })) {
        selectedIndex =
            (selectedIndex + 1) % static_cast<int>(choices.size());
    }
    selectedValue = choices[static_cast<size_t>(selectedIndex)].value;
    return selectedValue != oldValue;
}

} // namespace sokoban::uiControls
