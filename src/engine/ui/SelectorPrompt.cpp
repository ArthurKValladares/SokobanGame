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
        return uppercase(button->button);
    }
    const auto& axis = std::get<GamepadAxisBinding>(binding);
    return uppercase(axis.axis) +
        (axis.direction == AxisDirection::Negative ? "-" : "+");
}

} // namespace

std::optional<std::string> SelectorPrompt::bindingLabel(
    const InputBindings& bindings,
    BindingDeviceClass activeDevice)
{
    const std::vector<InputBinding>& confirmBindings =
        bindings.forAction(InputAction::MenuConfirm);
    auto found = activeDevice == BindingDeviceClass::Keyboard
        ? std::ranges::find_if(
              confirmBindings,
              [](const InputBinding& binding) {
                  const auto* keyboard =
                      std::get_if<KeyboardBinding>(&binding);
                  return keyboard && keyboard->scancode == "Space";
              })
        : confirmBindings.end();
    if (found == confirmBindings.end()) {
        found = std::ranges::find_if(
            confirmBindings,
            [&](const InputBinding& binding) {
                return bindingDeviceClass(binding) == activeDevice;
            });
    }
    if (found == confirmBindings.end()) {
        found = confirmBindings.begin();
    }
    if (found == confirmBindings.end()) {
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
    std::string_view bindingLabel)
{
    if (bindingLabel.empty()) {
        return;
    }

    constexpr float keyHeight = 34.0f;
    constexpr float arrowHeight = 18.0f;
    constexpr float labelSize = 20.0f;
    const Vec2 measured = ui.measureText(bindingLabel, labelSize);
    const float keyWidth = std::max(38.0f, measured.x + 18.0f);
    const UiRect key {
        {
            arrowTip.x - keyWidth * 0.5f,
            arrowTip.y - arrowHeight - keyHeight,
        },
        { keyWidth, keyHeight },
    };

    ui.rect({
        { key.position.x + 2.0f, key.position.y + 3.0f },
        key.size,
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
        key, bindingLabel, { 0.96f, 0.98f, 0.97f, 1.0f }, labelSize);

    constexpr Vec4 arrowColor { 0.96f, 0.78f, 0.31f, 1.0f };
    const float center = arrowTip.x;
    ui.rect({ { center - 2.0f, arrowTip.y - 15.0f }, { 4.0f, 7.0f } },
        arrowColor);
    ui.rect({ { center - 8.0f, arrowTip.y - 9.0f }, { 16.0f, 3.0f } },
        arrowColor);
    ui.rect({ { center - 5.0f, arrowTip.y - 6.0f }, { 10.0f, 3.0f } },
        arrowColor);
    ui.rect({ { center - 2.0f, arrowTip.y - 3.0f }, { 4.0f, 3.0f } },
        arrowColor);
}

} // namespace sokoban
