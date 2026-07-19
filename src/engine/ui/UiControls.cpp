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
    std::string_view,
    UiRect rect,
    std::string_view label,
    ButtonOptions options)
{
    const bool hovered = ui.hovered(rect);
    const bool pressed = hovered && ui.mouseDown();
    const Vec4 border = options.focused
        ? Vec4 { 0.30f, 0.80f, 0.72f, 1.0f }
        : Vec4 { 0.32f, 0.35f, 0.35f, 0.94f };
    ui.rect(rect, border);
    ui.rect({
        { rect.position.x + 2.0f, rect.position.y + 2.0f },
        { rect.size.x - 4.0f, rect.size.y - 4.0f },
    }, buttonColor(options.tone, hovered, pressed));
    ui.centeredText(rect, label, textColor, 24.0f);
    return ui.clicked(rect) || options.activate;
}

bool slider(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    float& value,
    float minimum,
    float maximum,
    bool focused,
    bool enabled)
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
    const UiRect track {
        { rect.position.x, rect.position.y + (rect.size.y - 8.0f) * 0.5f },
        { rect.size.x, 8.0f },
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
    ui.rect({
        { knobX - 8.0f, rect.position.y + 3.0f },
        { 16.0f, rect.size.y - 6.0f },
    }, !enabled
        ? Vec4 { 0.48f, 0.50f, 0.49f, 0.45f }
        : (focused
            ? Vec4 { 0.62f, 0.93f, 0.84f, 1.0f }
            : Vec4 { 0.82f, 0.86f, 0.82f, 1.0f }));
    return std::abs(value - oldValue) > 0.0001f;
}

bool checkbox(
    UiContext& ui,
    std::string_view,
    UiRect rect,
    std::string_view label,
    bool& value,
    bool focused,
    bool activate)
{
    const bool clicked = ui.clicked(rect) || activate;
    if (clicked) {
        value = !value;
    }
    const UiRect box {
        { rect.position.x, rect.position.y + (rect.size.y - 26.0f) * 0.5f },
        { 26.0f, 26.0f },
    };
    ui.rect(box, focused
        ? Vec4 { 0.30f, 0.80f, 0.72f, 1.0f }
        : Vec4 { 0.38f, 0.41f, 0.40f, 1.0f });
    ui.rect({
        { box.position.x + 2.0f, box.position.y + 2.0f },
        { 22.0f, 22.0f },
    }, { 0.12f, 0.14f, 0.14f, 1.0f });
    if (value) {
        ui.rect({
            { box.position.x + 6.0f, box.position.y + 6.0f },
            { 14.0f, 14.0f },
        }, accentColor);
    }
    ui.text({ rect.position.x + 40.0f, rect.position.y + 8.0f }, label, textColor, 23.0f);
    return clicked;
}

bool segmentedControl(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    std::span<const std::string_view> labels,
    int& selected,
    bool focused)
{
    if (labels.empty()) {
        return false;
    }
    const int oldSelected = selected;
    selected = std::clamp(selected, 0, static_cast<int>(labels.size()) - 1);
    const float segmentWidth = rect.size.x / static_cast<float>(labels.size());
    for (size_t index = 0; index < labels.size(); ++index) {
        const UiRect segment {
            { rect.position.x + segmentWidth * static_cast<float>(index), rect.position.y },
            { segmentWidth, rect.size.y },
        };
        const std::string segmentId = std::string(id) + "." + std::to_string(index);
        if (button(ui, segmentId, segment, labels[index], {
                .tone = static_cast<int>(index) == selected ? ButtonTone::Accent : ButtonTone::Normal,
                .focused = focused && static_cast<int>(index) == selected,
            })) {
            selected = static_cast<int>(index);
        }
    }
    return selected != oldSelected;
}

bool choiceStepper(
    UiContext& ui,
    std::string_view id,
    UiRect rect,
    std::span<const std::string_view> labels,
    int& selected,
    bool focused)
{
    if (labels.empty()) {
        return false;
    }
    const int oldSelected = selected;
    selected = std::clamp(selected, 0, static_cast<int>(labels.size()) - 1);
    constexpr float arrowWidth = 52.0f;
    if (button(ui, std::string(id) + ".previous", {
            rect.position, { arrowWidth, rect.size.y } }, "<", { .focused = focused })) {
        selected = (selected + static_cast<int>(labels.size()) - 1) % static_cast<int>(labels.size());
    }
    ui.rect({
        { rect.position.x + arrowWidth + 2.0f, rect.position.y },
        { rect.size.x - arrowWidth * 2.0f - 4.0f, rect.size.y },
    }, { 0.15f, 0.17f, 0.17f, 1.0f });
    ui.centeredText({
        { rect.position.x + arrowWidth, rect.position.y },
        { rect.size.x - arrowWidth * 2.0f, rect.size.y },
    }, labels[static_cast<size_t>(selected)], textColor, 22.0f);
    if (button(ui, std::string(id) + ".next", {
            { rect.position.x + rect.size.x - arrowWidth, rect.position.y },
            { arrowWidth, rect.size.y } }, ">", { .focused = focused })) {
        selected = (selected + 1) % static_cast<int>(labels.size());
    }
    return selected != oldSelected;
}

} // namespace sokoban::uiControls
