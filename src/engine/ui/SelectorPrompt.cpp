#include "engine/ui/SelectorPrompt.hpp"

#include "engine/ui/Ui.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <variant>

namespace sokoban {
namespace {

std::string uppercase(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string compactBindingLabel(const InputBinding& binding)
{
    if (const auto* keyboard = std::get_if<KeyboardBinding>(&binding)) {
        return keyboard->scancode;
    }
    if (const auto* button = std::get_if<GamepadButtonBinding>(&binding)) {
        if (button->button == "south") return "A";
        if (button->button == "east") return "B";
        if (button->button == "west") return "X";
        if (button->button == "north") return "Y";
        if (button->button == "rightshoulder") return "RB";
        if (button->button == "leftshoulder") return "LB";
        return uppercase(button->button);
    }
    const auto& axis = std::get<GamepadAxisBinding>(binding);
    return uppercase(axis.axis) +
        (axis.direction == AxisDirection::Negative ? "-" : "+");
}

} // namespace

std::optional<std::string> SelectorPrompt::bindingLabel(
    const InputBindings& bindings,
    InputAction action,
    BindingDeviceClass activeDevice)
{
    const std::vector<InputBinding>& actionBindings =
        bindings.forAction(action);
    auto found = action == InputAction::MenuConfirm &&
            activeDevice == BindingDeviceClass::Keyboard
        ? std::ranges::find_if(
              actionBindings,
              [](const InputBinding& binding) {
                  const auto* keyboard =
                      std::get_if<KeyboardBinding>(&binding);
                  return keyboard && keyboard->scancode == "Space";
              })
        : actionBindings.end();
    if (found == actionBindings.end()) {
        found = std::ranges::find_if(
            actionBindings,
            [&](const InputBinding& binding) {
                return bindingDeviceClass(binding) == activeDevice;
            });
    }
    if (found == actionBindings.end()) {
        found = actionBindings.begin();
    }
    if (found == actionBindings.end()) {
        return std::nullopt;
    }
    std::string label = compactBindingLabel(*found);
    return label.empty()
        ? std::nullopt
        : std::optional<std::string> { std::move(label) };
}

void SelectorPrompt::draw(
    UiContext& ui,
    Vec2 arrowTip,
    std::string_view enterBindingLabel,
    std::string_view previewBindingLabel)
{
    if (enterBindingLabel.empty() || previewBindingLabel.empty()) {
        return;
    }

    constexpr float keyHeight = 34.0f;
    constexpr float iconHeight = 20.0f;
    constexpr float labelSize = 20.0f;
    constexpr float optionGap = 14.0f;
    const float enterWidth = std::max(
        38.0f, ui.measureText(enterBindingLabel, labelSize).x + 18.0f);
    const float previewWidth = std::max(
        38.0f, ui.measureText(previewBindingLabel, labelSize).x + 18.0f);
    const float totalWidth = enterWidth + optionGap + previewWidth;
    const float keyY = arrowTip.y - iconHeight - keyHeight - 5.0f;

    auto drawKey = [&](float x, float width, std::string_view label) {
        const UiRect key { { x, keyY }, { width, keyHeight } };
        ui.rect({
            { key.position.x + 2.0f, key.position.y + 3.0f }, key.size,
        }, { 0.01f, 0.015f, 0.018f, 0.55f });
        ui.rect(key, { 0.72f, 0.78f, 0.76f, 0.98f });
        ui.rect({
            { key.position.x + 2.0f, key.position.y + 2.0f },
            { key.size.x - 4.0f, key.size.y - 4.0f },
        }, { 0.075f, 0.09f, 0.095f, 0.98f });
        ui.rect({
            { key.position.x + 4.0f, key.position.y + 4.0f },
            { key.size.x - 8.0f, 1.0f },
        }, { 0.58f, 0.76f, 0.70f, 0.82f });
        ui.centeredText(
            key, label, { 0.96f, 0.98f, 0.97f, 1.0f }, labelSize);
        return key.position.x + key.size.x * 0.5f;
    };

    const float left = arrowTip.x - totalWidth * 0.5f;
    const float enterCenter = drawKey(left, enterWidth, enterBindingLabel);
    const float previewCenter = drawKey(
        left + enterWidth + optionGap, previewWidth, previewBindingLabel);

    constexpr Vec4 arrowColor { 0.96f, 0.78f, 0.31f, 1.0f };
    ui.rect({ { enterCenter - 2.0f, arrowTip.y - 15.0f }, { 4.0f, 7.0f } },
        arrowColor);
    ui.rect({ { enterCenter - 8.0f, arrowTip.y - 9.0f }, { 16.0f, 3.0f } },
        arrowColor);
    ui.rect({ { enterCenter - 5.0f, arrowTip.y - 6.0f }, { 10.0f, 3.0f } },
        arrowColor);
    ui.rect({ { enterCenter - 2.0f, arrowTip.y - 3.0f }, { 4.0f, 3.0f } },
        arrowColor);

    // A small pixel-built eye stays crisp at every render scale and uses the
    // same geometry-only language as the existing arrow.
    const float eyeY = arrowTip.y - 13.0f;
    ui.rect({ { previewCenter - 9.0f, eyeY + 4.0f }, { 18.0f, 6.0f } },
        arrowColor);
    ui.rect({ { previewCenter - 6.0f, eyeY + 1.0f }, { 12.0f, 12.0f } },
        arrowColor);
    ui.rect({ { previewCenter - 3.0f, eyeY + 4.0f }, { 6.0f, 6.0f } },
        { 0.075f, 0.09f, 0.095f, 1.0f });
    ui.rect({ { previewCenter - 1.0f, eyeY + 6.0f }, { 2.0f, 2.0f } },
        { 0.96f, 0.98f, 0.97f, 1.0f });
}

UiRect ScreenPreviewOverlay::previewRect(Vec2 viewport)
{
    const Vec2 size { viewport.x * scale, viewport.y * scale };
    return {
        { (viewport.x - size.x) * 0.5f, (viewport.y - size.y) * 0.5f },
        size,
    };
}

void ScreenPreviewOverlay::draw(UiContext& ui, Vec2 viewport)
{
    const UiRect inset = previewRect(viewport);
    constexpr Vec4 veil { 0.01f, 0.015f, 0.018f, 0.16f };
    ui.rect({ {}, { viewport.x, inset.position.y } }, veil);
    ui.rect({
        { 0.0f, inset.position.y + inset.size.y },
        { viewport.x, viewport.y - inset.position.y - inset.size.y },
    }, veil);
    ui.rect({
        { 0.0f, inset.position.y }, { inset.position.x, inset.size.y },
    }, veil);
    ui.rect({
        { inset.position.x + inset.size.x, inset.position.y },
        { viewport.x - inset.position.x - inset.size.x, inset.size.y },
    }, veil);

    constexpr int fadeSteps = 8;
    constexpr float step = 3.0f;
    for (int index = 0; index < fadeSteps; ++index) {
        const float distance = static_cast<float>(fadeSteps - index) * step;
        const float alpha = 0.012f + 0.010f * static_cast<float>(index);
        const Vec4 shadow { 0.005f, 0.008f, 0.010f, alpha };
        const UiRect outer {
            { inset.position.x - distance, inset.position.y - distance },
            { inset.size.x + distance * 2.0f, inset.size.y + distance * 2.0f },
        };
        ui.rect({ outer.position, { outer.size.x, step } }, shadow);
        ui.rect({
            { outer.position.x, outer.position.y + outer.size.y - step },
            { outer.size.x, step },
        }, shadow);
        ui.rect({ outer.position, { step, outer.size.y } }, shadow);
        ui.rect({
            { outer.position.x + outer.size.x - step, outer.position.y },
            { step, outer.size.y },
        }, shadow);
    }

    // Fade a few pixels into the preview as well, so the composited scene
    // does not end on a single hard pixel row.
    for (int index = 0; index < fadeSteps; ++index) {
        const float offset = static_cast<float>(index) * step;
        const float alpha = 0.075f *
            (1.0f - static_cast<float>(index) / fadeSteps);
        const Vec4 shade { 0.01f, 0.015f, 0.018f, alpha };
        ui.rect({
            { inset.position.x + offset, inset.position.y + offset },
            { inset.size.x - offset * 2.0f, step },
        }, shade);
        ui.rect({
            { inset.position.x + offset,
              inset.position.y + inset.size.y - offset - step },
            { inset.size.x - offset * 2.0f, step },
        }, shade);
        ui.rect({
            { inset.position.x + offset, inset.position.y + offset },
            { step, inset.size.y - offset * 2.0f },
        }, shade);
        ui.rect({
            { inset.position.x + inset.size.x - offset - step,
              inset.position.y + offset },
            { step, inset.size.y - offset * 2.0f },
        }, shade);
    }
}

} // namespace sokoban
