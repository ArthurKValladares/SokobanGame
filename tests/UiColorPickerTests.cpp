#include "TestHarness.hpp"

#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/UiColorPicker.hpp"
#include "engine/ui/UiColorValues.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

using namespace sokoban;
using Command = UiColorPickerAction::Command;
using ui::color::Format;

constexpr Vec2 desktop { 640.0f, 480.0f };
constexpr Vec2 hexField { 444.0f, 95.0f };
constexpr Vec2 rgbField { 444.0f, 144.0f };
constexpr Vec2 cmykField { 444.0f, 193.0f };
constexpr Vec2 hsvField { 444.0f, 242.0f };
constexpr Vec2 hslField { 444.0f, 291.0f };
constexpr Vec2 copyButton { 560.0f, 95.0f };
constexpr Vec2 applyButton { 552.0f, 426.0f };
constexpr Vec2 cancelButton { 442.0f, 426.0f };

UiPointerInput click(Vec2 at)
{
    return { .position = at, .pressedAt = at, .releasedAt = at };
}

UiColorPickerAction draw(UiContext& ui, UiColorPicker& picker, UiPointerInput pointer = {},
    UiColorPickerInput input = {}, Vec2 viewport = desktop)
{
    ui.beginFrame(viewport, pointer);
    const auto action = picker.draw(ui, viewport, 1.0f, input);
    ui.endFrame();
    return action;
}

void testColorFieldEditing(const FontAtlas& font)
{
    TEST("pickerSynchronizesTypedFormatsAndAppliesOnlyValidValues");
    UiContext ui(font);
    UiColorPicker picker;
    picker.open({ 1.0f, 0.0f, 0.0f }, "Entrance Color");
    CHECK(draw(ui, picker).command == Command::None);

    // SDL forwards the press before text input and before this frame's draw.
    CHECK(picker.handleTextPress(ui, hexField, false));
    auto* edit = picker.activeTextEdit();
    CHECK(edit && edit->text() == "#FF0000");
    if (!edit) return;
    edit->setText("#112233");
    CHECK(draw(ui, picker, click(hexField)).command == Command::None);
    CHECK(picker.textFocused());
    picker.submitText();
    CHECK(!picker.textFocused());

    (void)draw(ui, picker, click(rgbField));
    edit = picker.activeTextEdit();
    CHECK(edit && edit->text() == "17, 34, 51");
    if (!edit) return;
    edit->setText("256, 10, 20");
    picker.submitText();
    CHECK(picker.textFocused());
    CHECK(draw(ui, picker, click(applyButton)).command == Command::None);
    edit->setText("0, 128, 255");
    const auto copy = draw(ui, picker, click(copyButton));
    CHECK(copy.command == Command::CopyHex && copy.hex == "#0080FF");

    (void)draw(ui, picker, click(cmykField));
    edit = picker.activeTextEdit();
    CHECK(edit && edit->text() == ui::color::format({ 0.0f, 128.0f / 255.0f, 1.0f }, Format::Cmyk));
    if (!edit) return;
    edit->setText("0%, 100%, 100%, 0%");
    picker.submitText();
    (void)draw(ui, picker, click(hsvField));
    edit = picker.activeTextEdit();
    CHECK(edit && edit->text() == ui::color::format({ 1.0f, 0.0f, 0.0f }, Format::Hsv));
    if (!edit) return;
    edit->setText("120, 100, 100");
    picker.submitText();
    (void)draw(ui, picker, click(hslField));
    edit = picker.activeTextEdit();
    CHECK(edit && edit->text() == ui::color::format({ 0.0f, 1.0f, 0.0f }, Format::Hsl));
    if (!edit) return;
    edit->setText("240, 100, 50");
    const auto apply = draw(ui, picker, click(applyButton));
    CHECK(apply.command == Command::Apply);
    CHECK(ui::color::format(apply.color, Format::Hex) == "#0000FF");
    CHECK(draw(ui, picker, click(cancelButton)).command == Command::Cancel);
}

void testColorPanelDrag(const FontAtlas& font)
{
    TEST("pickerDragsHueAndSaturationValueAndKeepsItsPointerCapture");
    UiContext ui(font);
    UiColorPicker picker;
    picker.open({ 1.0f, 0.0f, 0.0f });
    (void)draw(ui, picker, click({ 174.0f, 325.0f }));
    (void)draw(ui, picker, {
        .position = { 200.0f, 140.0f }, .down = true, .pressedAt = Vec2 { 200.0f, 140.0f },
    });
    (void)draw(ui, picker, {
        .position = { 400.0f, 60.0f }, .releasedAt = Vec2 { 400.0f, 60.0f },
    });
    const auto copy = draw(ui, picker, click(copyButton));
    CHECK(copy.command == Command::CopyHex && copy.hex == "#00FFFF");
    CHECK(ui.droppedCommands() == 0);
    CHECK(std::ranges::count_if(ui.drawData().commands, [](const UiDrawCommand& command) {
        return command.kind == UiDrawKind::ColorGradient;
    }) == 7);

    picker.open({ 0.4f, 0.0f, 0.0f });
    (void)draw(ui, picker, {}, { .navigation = { .activate = true } });
    (void)draw(ui, picker, {}, { .verticalStep = 1 });
    (void)draw(ui, picker, {}, { .navigation = { .activate = true } });
    const auto keyboard = draw(ui, picker, click(applyButton));
    CHECK(keyboard.command == Command::Apply);
    CHECK(keyboard.color.x > 0.4f && keyboard.color.y == 0.0f && keyboard.color.z == 0.0f);
}

void testCompactColorPanel(const FontAtlas& font)
{
    TEST("pickerFitsPortraitAndPreservesExactInitialColor");
    UiContext ui(font);
    UiColorPicker picker;
    constexpr Vec2 portrait { 360.0f, 720.0f };
    constexpr Vec3 initial { 0.7134f, 0.2397f, 0.5318f };
    picker.open(initial);
    CHECK(draw(ui, picker, {}, {}, portrait).command == Command::None);
    const auto action = draw(ui, picker, click({ 284.0f, 620.0f }), {}, portrait);
    CHECK(action.command == Command::Apply);
    CHECK(action.color == initial);
    CHECK(ui.droppedCommands() == 0);
}

} // namespace

int main()
{
    const auto font = FontAtlas::loadDefault(std::filesystem::path(SOKOBAN_TEST_ASSET_DIR));
    testColorFieldEditing(font);
    testColorPanelDrag(font);
    testCompactColorPanel(font);
    std::cout << "UiColorPickerTests: " << checks - failures << " of " << checks << " checks passed\n";
    return failures != 0 ? 1 : 0;
}
