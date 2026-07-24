#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <variant>

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

[[nodiscard]] bool settingsChanged(const std::optional<sokoban::OptionsAction>& action)
{
    return action.has_value() &&
        std::holds_alternative<sokoban::options::SettingsChanged>(*action);
}

void applySettingsChange(
    const std::optional<sokoban::OptionsAction>& action,
    sokoban::UserSettings& settings)
{
    if (!action) {
        return;
    }
    if (const auto* changed =
            std::get_if<sokoban::options::SettingsChanged>(&*action)) {
        settings = changed->settings;
    }
}

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

    ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
    ui.image(
        { { 0.0f, 0.0f }, { 1280.0f, 720.0f } },
        { { 0.1f, 0.2f }, { 0.8f, 0.6f } });
    ui.endFrame();
    CHECK(ui.drawData().commands.size() == 1);
    CHECK(ui.drawData().commands.front().kind == sokoban::UiDrawKind::Image);
    CHECK(ui.drawData().commands.front().uvRect.position.x == 0.1f);
    CHECK(ui.drawData().commands.front().uvRect.size.y == 0.6f);
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
    sokoban::OptionsMenuView view;
    sokoban::UserSettings settings;
    menu.open();

    auto draw = [&](sokoban::OptionsMenuInput input = {}) {
        std::optional<sokoban::OptionsAction> result =
            menu.handleInput(settings, input);
        applySettingsChange(result, settings);
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::OptionsMenuIntent> intent =
            view.draw(
                ui,
                { 1280.0f, 720.0f },
                menu.state(),
                settings);
        ui.endFrame();
        if (intent) {
            const std::optional<sokoban::OptionsAction> pointerResult =
                menu.dispatch(settings, *intent);
            applySettingsChange(pointerResult, settings);
            if (pointerResult) {
                result = pointerResult;
            }
        }
        return result;
    };

    draw({ .confirm = true });
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Graphics);
    const auto graphicsChange = draw({ .left = true });
    CHECK(settingsChanged(graphicsChange));
    CHECK(settings.video.antiAliasingSamples == 4);
    draw({ .down = true });
    const auto scaleChange = draw({ .right = true });
    CHECK(settingsChanged(scaleChange));
    CHECK(settings.video.renderScalePercent == 75);
    draw({ .down = true });
    const auto customEnabled = draw({ .confirm = true });
    CHECK(settingsChanged(customEnabled));
    CHECK(settings.video.customRenderScale);

    settings.video.renderScalePercent = 75;
    settings.video.customRenderScale = true;
    settings.video.customRenderScalePercent = 100;
    menu.open();
    draw({ .confirm = true });
    draw({ .down = true });
    draw({ .down = true });
    const auto customChange = draw({ .left = true });
    CHECK(settingsChanged(customChange));
    CHECK(settings.video.customRenderScalePercent == 99);
    const auto customDisabled = draw({ .confirm = true });
    CHECK(settingsChanged(customDisabled));
    CHECK(!settings.video.customRenderScale);
    menu.back();
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Main);

    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Audio);
    const float oldMaster = settings.audio.masterVolume;
    const auto audioChange = draw({ .left = true });
    CHECK(settingsChanged(audioChange));
    CHECK(settings.audio.masterVolume < oldMaster);

    menu.requestQuitConfirmation();
    draw({ .down = true });
    const std::optional<sokoban::OptionsAction> quit = draw({ .confirm = true });
    CHECK(quit.has_value() && std::holds_alternative<sokoban::options::Quit>(*quit));
    menu.back();
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Main);
}

void testControlsRemapping()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    sokoban::OptionsMenu menu;
    sokoban::OptionsMenuView view;
    sokoban::UserSettings settings;
    menu.open();

    auto draw = [&](sokoban::OptionsMenuInput input = {}) {
        std::optional<sokoban::OptionsAction> result =
            menu.handleInput(settings, input);
        applySettingsChange(result, settings);
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const std::optional<sokoban::OptionsMenuIntent> intent =
            view.draw(
                ui,
                { 1280.0f, 720.0f },
                menu.state(),
                settings);
        ui.endFrame();
        if (intent) {
            const std::optional<sokoban::OptionsAction> pointerResult =
                menu.dispatch(settings, *intent);
            applySettingsChange(pointerResult, settings);
            if (pointerResult) {
                result = pointerResult;
            }
        }
        return result;
    };

    draw({ .down = true });
    draw({ .down = true });
    draw({ .confirm = true });
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Controls);
    CHECK(!menu.capturingBinding());

    // Rebind Move up to P: same-kind keyboard binding replaced, pad kept.
    draw({ .confirm = true });
    CHECK(menu.capturingBinding());
    CHECK(menu.capturingAction() == sokoban::InputAction::MoveUp);
    applySettingsChange(
        menu.provideBindingCandidate(
            settings, sokoban::KeyboardBinding { "P" }),
        settings);
    CHECK(!menu.capturingBinding());
    const auto& moveUp =
        settings.input.forAction(sokoban::InputAction::MoveUp);
    CHECK(std::ranges::count(moveUp, sokoban::InputBinding {
        sokoban::KeyboardBinding { "P" } }) == 1);
    CHECK(std::ranges::count(moveUp, sokoban::InputBinding {
        sokoban::KeyboardBinding { "W" } }) == 0);
    CHECK(std::ranges::count(moveUp, sokoban::InputBinding {
        sokoban::GamepadButtonBinding { "dpup" } }) == 1);

    // Binding P to Move down steals it from Move up.
    draw({ .down = true });
    draw({ .confirm = true });
    const auto rebound = menu.provideBindingCandidate(
        settings, sokoban::KeyboardBinding { "P" });
    CHECK(settingsChanged(rebound));
    applySettingsChange(rebound, settings);
    CHECK(std::ranges::count(
        settings.input.forAction(sokoban::InputAction::MoveDown),
        sokoban::InputBinding { sokoban::KeyboardBinding { "P" } }) == 1);
    CHECK(std::ranges::count(
        settings.input.forAction(sokoban::InputAction::MoveUp),
        sokoban::InputBinding { sokoban::KeyboardBinding { "P" } }) == 0);
    CHECK(sokoban::actionBindingsDisplay(
        settings.input, sokoban::InputAction::MoveUp) ==
        "Pad dpup / Pad lefty-");

    // Escape is never bound; back() cancels the capture but stays on the page.
    draw({ .confirm = true });
    CHECK(menu.capturingBinding());
    (void)menu.provideBindingCandidate(
        settings, sokoban::KeyboardBinding { "Escape" });
    CHECK(menu.capturingBinding());
    menu.back();
    CHECK(!menu.capturingBinding());
    CHECK(menu.page() == sokoban::OptionsMenu::Page::Controls);

    // Start cancels directly without binding.
    const sokoban::UserSettings beforeStart = settings;
    draw({ .confirm = true });
    (void)menu.provideBindingCandidate(
        settings, sokoban::GamepadButtonBinding { "start" });
    CHECK(!menu.capturingBinding());
    CHECK(settings == beforeStart);

    // Navigation freezes during capture.
    const int rowBefore = menu.selectedRow();
    draw({ .confirm = true });
    draw({ .down = true });
    CHECK(menu.selectedRow() == rowBefore);
    menu.back();

    // Reset restores the defaults.
    for (int i = 0; i < 5; ++i) {
        draw({ .down = true });
    }
    const auto reset = draw({ .confirm = true });
    CHECK(settingsChanged(reset));
    CHECK(settings.input == sokoban::defaultInputBindings());
}

void testOptionsReducerAndDeclarativeRows()
{
    sokoban::OptionsMenuState state;
    sokoban::UserSettings settings;

    auto reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::Open {
            .allowTitleExit = true,
            .allowLevelSelect = true,
        });
    state = reduction.state;
    CHECK(state.open);
    const std::vector<sokoban::OptionsMenuRow> mainRows =
        sokoban::optionsMenuRows(state, settings);
    CHECK(mainRows.size() == 6);
    CHECK(mainRows[3].id == sokoban::OptionsMenuRowId::LevelSelect);
    CHECK(mainRows[4].id == sokoban::OptionsMenuRowId::ExitToTitle);
    CHECK(mainRows.back().tone == sokoban::OptionsMenuRowTone::Danger);
    CHECK(mainRows.back().dividerBefore);

    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::ActivateRow {
            sokoban::OptionsMenuRowId::Graphics });
    state = reduction.state;
    CHECK(state.page == sokoban::OptionsMenuPage::Graphics);

    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::AdjustSelected { -1 });
    CHECK(settings.video.antiAliasingSamples == 8);
    CHECK(settingsChanged(reduction.action));
    const auto* changed =
        std::get_if<sokoban::options::SettingsChanged>(
            &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(changed->settings.video.antiAliasingSamples == 4);

    settings.video.customRenderScale = true;
    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::SelectChoice {
            sokoban::OptionsMenuRowId::RenderScalePreset,
            50,
        });
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed->settings.video.renderScalePercent == 50);
    CHECK(!changed->settings.video.customRenderScale);
}

void testOptionsReducerDraftAndBindingSemantics()
{
    sokoban::UserSettings settings;
    settings.video.customRenderScale = true;
    sokoban::OptionsMenuState graphics {
        .open = true,
        .page = sokoban::OptionsMenuPage::Graphics,
    };

    auto reduction = sokoban::reduceOptionsMenu(
        graphics,
        settings,
        sokoban::options::intent::SetSlider {
            sokoban::OptionsMenuRowId::CustomRenderScale,
            0.42f,
            false,
        });
    CHECK(!reduction.action.has_value());
    CHECK(reduction.state.customRenderScalePreview == 42);
    CHECK(settings.video.customRenderScalePercent == 100);

    reduction = sokoban::reduceOptionsMenu(
        reduction.state,
        settings,
        sokoban::options::intent::SetSlider {
            sokoban::OptionsMenuRowId::CustomRenderScale,
            0.42f,
            true,
        });
    CHECK(!reduction.state.customRenderScalePreview.has_value());
    const auto* changed =
        std::get_if<sokoban::options::SettingsChanged>(
            &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(changed->settings.video.customRenderScalePercent == 42);

    const auto noOp = sokoban::reduceOptionsMenu(
        graphics,
        settings,
        sokoban::options::intent::SetSlider {
            sokoban::OptionsMenuRowId::CustomRenderScale,
            1.0f,
            true,
        });
    CHECK(!noOp.action.has_value());

    sokoban::OptionsMenuState controls {
        .open = true,
        .page = sokoban::OptionsMenuPage::Controls,
    };
    reduction = sokoban::reduceOptionsMenu(
        controls,
        settings,
        sokoban::options::intent::ActivateRow {
            sokoban::OptionsMenuRowId::MoveUp });
    CHECK(reduction.state.capturingAction ==
        sokoban::InputAction::MoveUp);

    reduction = sokoban::reduceOptionsMenu(
        reduction.state,
        settings,
        sokoban::options::intent::ProvideBinding {
            sokoban::KeyboardBinding { "P" },
        });
    CHECK(!reduction.state.capturingAction.has_value());
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(std::ranges::count(
        changed->settings.input.forAction(
            sokoban::InputAction::MoveUp),
        sokoban::InputBinding {
            sokoban::KeyboardBinding { "P" } }) == 1);
}

} // namespace

int main()
{
    testFontAtlasAndText();
    testReusableControls();
    testLayoutTree();
    testOptionsNavigationAndSettings();
    testControlsRemapping();
    testOptionsReducerAndDeclarativeRows();
    testOptionsReducerDraftAndBindingSemantics();

    if (failures == 0) {
        std::cout << "UiTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "UiTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
