#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

const std::filesystem::path fontPath =
    std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "ui/Karla-Regular.ttf";

void testFontAtlasAndText()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    CHECK(font.width() == 512);
    CHECK(font.height() == 512);
    CHECK(font.pixels().size() == 512 * 512);
    CHECK(std::ranges::any_of(font.pixels(), [](std::byte value) {
        return value != std::byte { 0 };
    }));
    CHECK(font.measureText("Graphics", 24.0f).x > 60.0f);

    sokoban::UiContext ui(font);
    ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
    ui.text({ 10.0f, 10.0f }, "Audio", { 1.0f, 1.0f, 1.0f, 1.0f });
    ui.endFrame();
    CHECK(!ui.drawData().commands.empty());
    CHECK(std::ranges::all_of(ui.drawData().commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::FontGlyph;
    }));
}

void testReusableControls()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    ui.beginFrame({ 400.0f, 200.0f }, { 50.0f, 25.0f }, true, true);
    CHECK(sokoban::uiControls::button(
        ui, "button", { { 10.0f, 10.0f }, { 100.0f, 40.0f } }, "Button"));

    float value = 0.0f;
    ui.beginFrame({ 400.0f, 200.0f }, { 110.0f, 85.0f }, true, true);
    CHECK(sokoban::uiControls::slider(
        ui, "slider", { { 10.0f, 70.0f }, { 200.0f, 30.0f } },
        value, 0.0f, 1.0f));
    CHECK(value > 0.45f && value < 0.55f);

    value = 0.25f;
    ui.beginFrame({ 400.0f, 200.0f }, { 110.0f, 85.0f }, true, true);
    CHECK(!sokoban::uiControls::slider(
        ui, "disabled-slider", { { 10.0f, 70.0f }, { 200.0f, 30.0f } },
        value, 0.0f, 1.0f, false, false));
    CHECK(value == 0.25f);
    CHECK(std::ranges::all_of(ui.drawData().commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::Solid && command.color.w < 0.5f;
    }));

    constexpr std::array choices {
        sokoban::uiControls::ChoiceOption { 10, "Ten" },
        sokoban::uiControls::ChoiceOption { 20, "Twenty" },
        sokoban::uiControls::ChoiceOption { 30, "Thirty" },
    };
    int selectedChoice = 20;
    ui.beginFrame({ 400.0f, 200.0f }, {}, false, false);
    CHECK(sokoban::uiControls::segmentedControl(
        ui, "choices", { { 10.0f, 110.0f }, { 300.0f, 40.0f } },
        choices, selectedChoice, { .selectNext = true }));
    CHECK(selectedChoice == 30);

    ui.beginFrame({ 400.0f, 200.0f }, { 50.0f, 130.0f }, true, true);
    CHECK(sokoban::uiControls::segmentedControl(
        ui, "choices", { { 10.0f, 110.0f }, { 300.0f, 40.0f } },
        choices, selectedChoice));
    CHECK(selectedChoice == 10);

    bool checked = false;
    ui.beginFrame({ 400.0f, 200.0f }, { 20.0f, 135.0f }, true, true);
    CHECK(sokoban::uiControls::checkbox(
        ui, "check", { { 10.0f, 120.0f }, { 180.0f, 48.0f } },
        "Enabled", checked));
    CHECK(checked);
}

void testLayoutTree()
{
    sokoban::UiLayoutTree layout(
        sokoban::UiLayoutAxis::Vertical,
        { 10.0f, 10.0f, 10.0f, 10.0f });
    const sokoban::UiLayoutNode first = layout.item(layout.root(), 20.0f);
    const sokoban::UiLayoutNode group = layout.column(
        layout.root(), sokoban::UiLayoutSize::content(), 5.0f);
    (void)layout.item(group, 10.0f);
    (void)layout.item(group, 15.0f);
    const sokoban::UiLayoutNode afterGroup = layout.item(layout.root(), 20.0f);
    layout.flexibleSpacer(layout.root());
    const sokoban::UiLayoutNode bottom = layout.item(layout.root(), 30.0f);

    layout.arrange({ { 0.0f, 0.0f }, { 200.0f, 200.0f } });
    CHECK(!layout.overflowed());
    CHECK(layout.rect(first).position.y == 10.0f);
    CHECK(layout.rect(afterGroup).position.y == 60.0f);
    CHECK(layout.rect(bottom).position.y == 160.0f);

    (void)layout.item(group, 10.0f);
    layout.arrange({ { 0.0f, 0.0f }, { 200.0f, 200.0f } });
    CHECK(layout.rect(afterGroup).position.y == 75.0f);
    CHECK(layout.rect(bottom).position.y == 160.0f);

    sokoban::UiLayoutTree rowLayout(
        sokoban::UiLayoutAxis::Vertical,
        { 10.0f, 10.0f, 10.0f, 10.0f });
    const sokoban::UiLayoutNode row = rowLayout.row(
        rowLayout.root(), sokoban::UiLayoutSize::fixed(40.0f), 10.0f);
    const sokoban::UiLayoutNode fixed = rowLayout.item(row, 50.0f);
    const sokoban::UiLayoutNode fill = rowLayout.item(
        row, sokoban::UiLayoutSize::fill(), sokoban::UiLayoutSize::fill());
    rowLayout.arrange({ { 0.0f, 0.0f }, { 200.0f, 80.0f } });
    CHECK(rowLayout.rect(fixed).size.x == 50.0f);
    CHECK(rowLayout.rect(fill).position.x == 70.0f);
    CHECK(rowLayout.rect(fill).size.x == 120.0f);

    sokoban::UiLayoutTree overflowing;
    (void)overflowing.item(overflowing.root(), 80.0f);
    (void)overflowing.item(overflowing.root(), 80.0f);
    overflowing.arrange({ { 0.0f, 0.0f }, { 100.0f, 100.0f } });
    CHECK(overflowing.overflowed());
}

void testOptionsNavigationAndSettings()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::OptionsMenu menu;
    menu.open({});

    auto draw = [&](sokoban::OptionsMenuInput input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const sokoban::OptionsMenuResult result =
            menu.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        return result;
    };

    draw({ .confirm = true });
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Graphics);
    const sokoban::OptionsMenuResult graphicsChange = draw({ .left = true });
    CHECK(graphicsChange.settingsChanged);
    CHECK(menu.settings().antiAliasingSamples == 4);
    draw({ .down = true });
    const sokoban::OptionsMenuResult scaleChange = draw({ .right = true });
    CHECK(scaleChange.settingsChanged);
    CHECK(menu.settings().renderScalePercent == 75);
    draw({ .down = true });
    const sokoban::OptionsMenuResult customEnabled = draw({ .confirm = true });
    CHECK(customEnabled.settingsChanged);
    CHECK(menu.settings().customRenderScale);

    ui.beginFrame({ 1280.0f, 720.0f }, { 640.0f, 386.0f }, true, true);
    const sokoban::OptionsMenuResult customDrag =
        menu.draw(ui, { 1280.0f, 720.0f }, {});
    ui.endFrame();
    CHECK(!customDrag.settingsChanged);
    CHECK(menu.settings().customRenderScalePercent > 60 &&
        menu.settings().customRenderScalePercent < 65);
    ui.beginFrame({ 1280.0f, 720.0f }, { 640.0f, 386.0f }, false, false);
    const sokoban::OptionsMenuResult customDragCommitted =
        menu.draw(ui, { 1280.0f, 720.0f }, {});
    ui.endFrame();
    CHECK(customDragCommitted.settingsChanged);

    menu.open({
        .renderScalePercent = 75,
        .customRenderScale = true,
        .customRenderScalePercent = 100,
    });
    draw({ .confirm = true });
    draw({ .down = true });
    draw({ .down = true });
    const sokoban::OptionsMenuResult customChange = draw({ .left = true });
    CHECK(customChange.settingsChanged);
    CHECK(menu.settings().customRenderScalePercent == 99);
    const sokoban::OptionsMenuResult customDisabled = draw({ .confirm = true });
    CHECK(customDisabled.settingsChanged);
    CHECK(!menu.settings().customRenderScale);
    menu.back();
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Main);

    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Audio);
    const float oldMaster = menu.settings().masterVolume;
    const sokoban::OptionsMenuResult audioChange = draw({ .left = true });
    CHECK(audioChange.settingsChanged);
    CHECK(menu.settings().masterVolume < oldMaster);

    menu.requestQuitConfirmation();
    draw({ .down = true });
    const sokoban::OptionsMenuResult quit = draw({ .confirm = true });
    CHECK(quit.quitRequested);
    menu.back();
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Main);
}

} // namespace

int main()
{
    testFontAtlasAndText();
    testReusableControls();
    testLayoutTree();
    testOptionsNavigationAndSettings();

    if (failures == 0) {
        std::cout << "UiTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "UiTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
