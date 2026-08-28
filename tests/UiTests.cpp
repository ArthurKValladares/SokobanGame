#include "engine/AssetManifest.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/InputPrompts.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiConfig.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
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
const std::filesystem::path assetRoot =
    std::filesystem::path(SOKOBAN_TEST_ASSET_DIR);

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
        ui, { { 10.0f, 10.0f }, { 100.0f, 40.0f } }, "Button"));

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
        ui, { { 10.0f, 110.0f }, { 300.0f, 40.0f } },
        choices, selectedChoice, { .selectNext = true }));
    CHECK(selectedChoice == 30);

    ui.beginFrame({ 400.0f, 200.0f }, { 50.0f, 130.0f }, true, true);
    CHECK(sokoban::uiControls::segmentedControl(
        ui, { { 10.0f, 110.0f }, { 300.0f, 40.0f } },
        choices, selectedChoice));
    CHECK(selectedChoice == 10);

    bool checked = false;
    ui.beginFrame({ 400.0f, 200.0f }, { 20.0f, 135.0f }, true, true);
    CHECK(sokoban::uiControls::checkbox(
        ui, { { 10.0f, 120.0f }, { 180.0f, 48.0f } },
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
    CHECK(std::ranges::all_of(
        ui.drawData().commands,
        [](const sokoban::UiDrawCommand& command) {
            if (command.kind != sokoban::UiDrawKind::FontGlyph) {
                return true;
            }
            constexpr float tolerance = 0.01f;
            return command.rect.position.x >= -tolerance &&
                command.rect.position.y >= -tolerance &&
                command.rect.position.x + command.rect.size.x <=
                    1280.0f + tolerance &&
                command.rect.position.y + command.rect.size.y <=
                    720.0f + tolerance;
        }));
    const auto graphicsChange = draw({ .left = true });
    CHECK(settingsChanged(graphicsChange));
    CHECK(settings.video.antiAliasingSamples == 2);
    draw({ .down = true });
    draw({ .down = true });
    draw({ .down = true });
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
    draw({ .down = true });
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
    CHECK(menu.state().controlsBindingDevice ==
        sokoban::BindingDeviceClass::Keyboard);

    ui.beginFrame({ 580.0f, 718.0f }, {}, false, false);
    (void)view.draw(ui, { 580.0f, 718.0f }, menu.state(), settings);
    ui.endFrame();
    CHECK(std::ranges::all_of(
        ui.drawData().commands,
        [](const sokoban::UiDrawCommand& command) {
            if (command.kind != sokoban::UiDrawKind::FontGlyph) {
                return true;
            }
            constexpr float tolerance = 0.01f;
            return command.rect.position.x >= -tolerance &&
                command.rect.position.y >= -tolerance &&
                command.rect.position.x + command.rect.size.x <=
                    580.0f + tolerance &&
                command.rect.position.y + command.rect.size.y <=
                    718.0f + tolerance;
        }));

    // The tabs are the first row; move into the first keyboard binding.
    draw({ .down = true });

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
    for (int i = 0; i < 9; ++i) {
        draw({ .down = true });
    }
    const auto reset = draw({ .confirm = true });
    CHECK(settingsChanged(reset));
    CHECK(settings.input == sokoban::defaultInputBindings());
}

void drawRects(sokoban::UiContext& ui, std::size_t count)
{
    ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
    for (std::size_t i = 0; i < count; ++i) {
        ui.rect(
            { { static_cast<float>(i), 0.0f }, { 1.0f, 1.0f } },
            { 1.0f, 1.0f, 1.0f, 1.0f });
    }
    ui.endFrame();
}

void testSelectorPromptShowsInteractAndPreviewBindings()
{
    sokoban::InputBindings bindings = sokoban::defaultInputBindings();
    CHECK(sokoban::SelectorPrompt::bindingLabel(
        bindings, sokoban::InputAction::MenuConfirm,
        sokoban::BindingDeviceClass::Keyboard) == "Space");
    CHECK(sokoban::SelectorPrompt::bindingLabel(
        bindings, sokoban::InputAction::MenuConfirm,
        sokoban::BindingDeviceClass::Gamepad) == "A");
    CHECK(sokoban::SelectorPrompt::bindingLabel(
        bindings, sokoban::InputAction::PreviewScreen,
        sokoban::BindingDeviceClass::Keyboard) == "V");
    CHECK(sokoban::SelectorPrompt::bindingLabel(
        bindings, sokoban::InputAction::PreviewScreen,
        sokoban::BindingDeviceClass::Gamepad) == "RB");

    sokoban::assignBinding(
        bindings,
        sokoban::InputAction::MenuConfirm,
        sokoban::KeyboardBinding { "X" });
    CHECK(sokoban::SelectorPrompt::bindingLabel(
        bindings, sokoban::InputAction::MenuConfirm,
        sokoban::BindingDeviceClass::Keyboard) == "X");

    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    ui.beginFrame({ 400.0f, 240.0f }, {}, false, false);
    sokoban::SelectorPrompt::draw(ui, { 200.0f, 160.0f }, "X", "V");
    ui.endFrame();

    const auto& commands = ui.drawData().commands;
    CHECK(std::ranges::count_if(commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::FontGlyph;
    }) == 2);
    CHECK(std::ranges::count_if(commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::Solid;
    }) >= 16);
    CHECK(std::ranges::any_of(commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::Solid &&
            command.rect.position.y + command.rect.size.y == 160.0f;
    }));
}

void testInputPromptCatalogUsesKeyboardAndControllerSpecificGlyphs()
{
    const sokoban::AssetManifest manifest =
        sokoban::AssetManifest::loadFromFile(assetRoot / "manifest.json");
    const sokoban::InputPromptCatalog prompts(assetRoot, manifest);

    const auto space = prompts.glyphForBinding(
        sokoban::KeyboardBinding { "Space" });
    const auto letter = prompts.glyphForBinding(
        sokoban::KeyboardBinding { "V" });
    const auto moveUp = prompts.glyphForBinding(
        sokoban::KeyboardBinding { "W" });
    CHECK(space.has_value());
    CHECK(letter.has_value());
    CHECK(moveUp.has_value());
    CHECK(space->texture == manifest.textureIdByName("InputPromptsKeyboard"));
    CHECK(space->uvRect.position.x != letter->uvRect.position.x ||
        space->uvRect.position.y != letter->uvRect.position.y);
    // Kenney XML uses bottom-origin Y. W is declared at y=832 in a 1024px
    // sheet, so its top-origin PNG row begins at 128, not 832.
    CHECK(std::abs(
        moveUp->uvRect.position.y - (128.5f / 1024.0f)) < 0.00001f);
    CHECK(std::abs(
        space->uvRect.position.y - (256.5f / 1024.0f)) < 0.00001f);

    sokoban::GamepadPresentation xbox {
        .type = SDL_GAMEPAD_TYPE_XBOXONE,
        .name = "Xbox Wireless Controller",
        .faceButtonLabels = {
            SDL_GAMEPAD_BUTTON_LABEL_A,
            SDL_GAMEPAD_BUTTON_LABEL_B,
            SDL_GAMEPAD_BUTTON_LABEL_X,
            SDL_GAMEPAD_BUTTON_LABEL_Y,
        },
    };
    const auto xboxSouth = prompts.glyphForBinding(
        sokoban::GamepadButtonBinding { "south" }, xbox);
    const auto xboxShoulder = prompts.glyphForBinding(
        sokoban::GamepadButtonBinding { "rightshoulder" }, xbox);
    CHECK(prompts.themeForGamepad(xbox) == sokoban::InputPromptTheme::Xbox);
    CHECK(xboxSouth.has_value());
    CHECK(xboxShoulder.has_value());
    CHECK(xboxSouth->texture == manifest.textureIdByName("InputPromptsXbox"));
    // Xbox A is declared at bottom-origin y=0 in a 640px sheet, while RB is
    // at y=448. Both must be converted to their top-origin PNG rows.
    CHECK(std::abs(
        xboxSouth->uvRect.position.y - (576.5f / 640.0f)) < 0.00001f);
    CHECK(std::abs(
        xboxShoulder->uvRect.position.y - (128.5f / 640.0f)) < 0.00001f);

    sokoban::GamepadPresentation playStation = xbox;
    playStation.type = SDL_GAMEPAD_TYPE_PS5;
    playStation.name = "DualSense Wireless Controller";
    playStation.faceButtonLabels = {
        SDL_GAMEPAD_BUTTON_LABEL_CROSS,
        SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
        SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
        SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE,
    };
    const auto cross = prompts.glyphForBinding(
        sokoban::GamepadButtonBinding { "south" }, playStation);
    CHECK(cross.has_value());
    CHECK(cross->texture ==
        manifest.textureIdByName("InputPromptsPlayStation"));

    sokoban::GamepadPresentation switchPad = xbox;
    switchPad.type = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    switchPad.name = "Nintendo Switch Pro Controller";
    switchPad.faceButtonLabels = {
        SDL_GAMEPAD_BUTTON_LABEL_B,
        SDL_GAMEPAD_BUTTON_LABEL_A,
        SDL_GAMEPAD_BUTTON_LABEL_Y,
        SDL_GAMEPAD_BUTTON_LABEL_X,
    };
    const auto switchSouth = prompts.glyphForBinding(
        sokoban::GamepadButtonBinding { "south" }, switchPad);
    CHECK(switchSouth.has_value());
    CHECK(switchSouth->texture == manifest.textureIdByName("InputPromptsSwitch"));

    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    ui.beginFrame({ 400.0f, 240.0f }, {}, false, false);
    sokoban::SelectorPrompt::draw(ui, { 200.0f, 160.0f }, *space, *letter);
    ui.endFrame();
    CHECK(std::ranges::count_if(ui.drawData().commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::TextureImage;
    }) == 2);
    CHECK(std::ranges::all_of(ui.drawData().commands, [](const auto& command) {
        return command.kind != sokoban::UiDrawKind::TextureImage ||
            !command.texture.isNone();
    }));

    sokoban::OptionsMenuState controls {
        .open = true,
        .page = sokoban::OptionsMenuPage::Controls,
    };
    sokoban::UserSettings settings;
    settings.input = sokoban::defaultInputBindings();
    sokoban::OptionsMenuView optionsView;
    ui.beginFrame({ 1280.0f, 900.0f }, {}, false, false);
    (void)optionsView.draw(
        ui, { 1280.0f, 900.0f }, controls, settings, &prompts, &xbox);
    ui.endFrame();
    CHECK(std::ranges::count_if(ui.drawData().commands, [](const auto& command) {
        return command.kind == sokoban::UiDrawKind::TextureImage;
    }) >= 8);
}

void testScreenPreviewOverlayUsesCenteredSeventyFivePercentInset()
{
    const sokoban::UiRect inset =
        sokoban::ScreenPreviewOverlay::previewRect({ 1280.0f, 720.0f });
    CHECK(inset.position.x == 160.0f);
    CHECK(inset.position.y == 90.0f);
    CHECK(inset.size.x == 960.0f);
    CHECK(inset.size.y == 540.0f);

    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);
    ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
    sokoban::ScreenPreviewOverlay::draw(ui, { 1280.0f, 720.0f });
    ui.endFrame();
    CHECK(ui.drawData().commands.size() == 1);
    const sokoban::UiDrawCommand& command =
        ui.drawData().commands.front();
    CHECK(command.kind == sokoban::UiDrawKind::SceneImage);
    CHECK(command.rect.position.x == inset.position.x);
    CHECK(command.rect.position.y == inset.position.y);
    CHECK(command.rect.size.x == inset.size.x);
    CHECK(command.rect.size.y == inset.size.y);
    CHECK(command.color.w == 1.0f);
    CHECK(command.effectOptions.x == inset.size.x);
    CHECK(command.effectOptions.y == inset.size.y);
    CHECK(command.effectOptions.z == 64.0f);
    CHECK(command.effectOptions.w == 96.0f);
    CHECK(command.uvRect.position.x >= 0.0f);
    CHECK(command.uvRect.position.y >= 0.0f);
    CHECK(command.uvRect.position.x + command.uvRect.size.x <= 1.0f);
    CHECK(command.uvRect.position.y + command.uvRect.size.y <= 1.0f);
}

#if SOKOBAN_ENABLE_DEBUG_UI
void testDebugEditorControlBindings()
{
    sokoban::UserSettings settings;
    sokoban::OptionsMenuState state {
        .open = true,
        .page = sokoban::OptionsMenuPage::Controls,
    };
    const std::vector<sokoban::OptionsMenuRow> controlRows =
        sokoban::optionsMenuRows(state, settings);
    CHECK(std::ranges::find(
        controlRows,
        sokoban::OptionsMenuRowId::EditorControls,
        &sokoban::OptionsMenuRow::id) != controlRows.end());

    auto reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::ActivateRow {
            sokoban::OptionsMenuRowId::EditorControls });
    state = reduction.state;
    CHECK(state.page == sokoban::OptionsMenuPage::EditorControls);
    const std::vector<sokoban::OptionsMenuRow> editorRows =
        sokoban::optionsMenuRows(state, settings);
    CHECK(editorRows.size() == 4);
    CHECK(editorRows.front().id ==
        sokoban::OptionsMenuRowId::EditorReplaceTile);

    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::ActivateRow {
            sokoban::OptionsMenuRowId::EditorMoveTile });
    CHECK(reduction.state.capturingAction ==
        sokoban::InputAction::EditorMoveTile);
}
#endif

void testUiFrameArenaCommandBudget()
{
    const sokoban::FontAtlas font = sokoban::FontAtlas::load(fontPath);
    sokoban::UiContext ui(font);

    // A full frame is one bump, and the frame after it gets the same arena
    // back. Repeated many times because the arena's original failure was a
    // reset that did not actually reclaim the buffer: a budget-sized frame
    // worked exactly once, and the game died a few seconds later. Anything
    // short of a full frame drains it slowly enough to hide that.
    bool everyFrameFitTheArena = true;
    std::size_t bytesUsedByAFullFrame = 0;
    for (int frame = 0; frame < 500; ++frame) {
        drawRects(ui, sokoban::config::uiFrameCommandBudget);
        if (frame == 0) {
            bytesUsedByAFullFrame = ui.frameArenaBytesUsed();
        }
        everyFrameFitTheArena = everyFrameFitTheArena &&
            ui.drawData().commands.size() ==
                sokoban::config::uiFrameCommandBudget &&
            ui.frameArenaBytesUsed() == bytesUsedByAFullFrame &&
            ui.droppedCommands() == 0;
    }
    CHECK(everyFrameFitTheArena);
    CHECK(bytesUsedByAFullFrame > 0);
    CHECK(ui.frameArenaHighWaterBytes() == bytesUsedByAFullFrame);

    // Past the budget the extra commands are dropped and counted, so an
    // over-busy frame loses its tail rather than the session. The arena is
    // sized from the same budget, so it is never the thing that runs out.
    drawRects(ui, sokoban::config::uiFrameCommandBudget + 100);
    CHECK(ui.drawData().commands.size() ==
        sokoban::config::uiFrameCommandBudget);
    CHECK(ui.droppedCommands() == 100);
    CHECK(ui.frameArenaBytesUsed() == bytesUsedByAFullFrame);

    // And the next frame starts clean.
    drawRects(ui, sokoban::config::uiFrameCommandBudget);
    CHECK(ui.drawData().commands.size() ==
        sokoban::config::uiFrameCommandBudget);
    CHECK(ui.droppedCommands() == 0);
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
    CHECK(mainRows.size() == 5);
    CHECK(mainRows[3].id == sokoban::OptionsMenuRowId::ExitToTitle);
    CHECK(mainRows.back().tone == sokoban::OptionsMenuRowTone::Danger);
    CHECK(mainRows.back().dividerBefore);

    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::ActivateRow {
            sokoban::OptionsMenuRowId::Graphics });
    state = reduction.state;
    CHECK(state.page == sokoban::OptionsMenuPage::Graphics);
    const std::vector<sokoban::OptionsMenuRow> graphicsRows =
        sokoban::optionsMenuRows(state, settings);
    CHECK(graphicsRows.size() == 11);
    CHECK(graphicsRows[1].id == sokoban::OptionsMenuRowId::Vsync);
    CHECK(!graphicsRows[2].enabled);
    CHECK(graphicsRows[3].id == sokoban::OptionsMenuRowId::FrameRateLimit);
    CHECK(graphicsRows[6].id == sokoban::OptionsMenuRowId::Exposure);
    CHECK(graphicsRows[6].sliderMinimum == sokoban::minimumExposureEv);
    CHECK(graphicsRows[6].sliderMaximum == sokoban::maximumExposureEv);
    CHECK(graphicsRows[6].sliderDisplay ==
        sokoban::OptionsMenuSliderDisplay::ExposureEv);
    CHECK(graphicsRows[7].id ==
        sokoban::OptionsMenuRowId::AmbientOcclusion);
    CHECK(graphicsRows[8].id ==
        sokoban::OptionsMenuRowId::AmbientOcclusionStrength);
    CHECK(graphicsRows[8].enabled);

    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::AdjustSelected { -1 });
    CHECK(settings.video.antiAliasingSamples == 4);
    CHECK(settingsChanged(reduction.action));
    const auto* changed =
        std::get_if<sokoban::options::SettingsChanged>(
            &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(changed->settings.video.antiAliasingSamples == 2);

    reduction = sokoban::reduceOptionsMenu(
        state,
        settings,
        sokoban::options::intent::SetToggle {
            sokoban::OptionsMenuRowId::Vsync, false });
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(!changed->settings.video.vsync);

    reduction = sokoban::reduceOptionsMenu(
        state,
        changed->settings,
        sokoban::options::intent::SetToggle {
            sokoban::OptionsMenuRowId::AllowTearing, true });
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(changed->settings.video.allowTearing);

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

    reduction = sokoban::reduceOptionsMenu(
        graphics,
        settings,
        sokoban::options::intent::SetSlider {
            sokoban::OptionsMenuRowId::AmbientOcclusionStrength,
            0.8f,
            true,
        });
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(changed->settings.video.ambientOcclusionStrength == 0.8f);

    reduction = sokoban::reduceOptionsMenu(
        graphics,
        settings,
        sokoban::options::intent::SetSlider {
            sokoban::OptionsMenuRowId::Exposure,
            -1.5f,
            true,
        });
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(changed->settings.video.exposureEv == -1.5f);

    graphics.selectedRow = 8;
    reduction = sokoban::reduceOptionsMenu(
        graphics,
        settings,
        sokoban::options::intent::AdjustSelected { -1 });
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(std::abs(
        changed->settings.video.ambientOcclusionStrength - 0.5f) < 0.0001f);

    settings.video.ambientOcclusion = false;
    const std::vector<sokoban::OptionsMenuRow> disabledRows =
        sokoban::optionsMenuRows(graphics, settings);
    CHECK(!disabledRows[8].enabled);
    const auto disabledStrength = sokoban::reduceOptionsMenu(
        graphics,
        settings,
        sokoban::options::intent::SetSlider {
            sokoban::OptionsMenuRowId::AmbientOcclusionStrength,
            0.2f,
            true,
        });
    CHECK(!disabledStrength.action.has_value());

    sokoban::OptionsMenuState controls {
        .open = true,
        .page = sokoban::OptionsMenuPage::Controls,
    };
    const std::vector<sokoban::OptionsMenuRow> keyboardRows =
        sokoban::optionsMenuRows(controls, settings);
    CHECK(keyboardRows.front().id ==
        sokoban::OptionsMenuRowId::BindingDevice);
    CHECK(keyboardRows.front().kind ==
        sokoban::OptionsMenuRowKind::Tabs);
    CHECK(keyboardRows.front().choiceValue == 0);
    CHECK(sokoban::actionBindingsDisplay(
        settings.input,
        sokoban::InputAction::MoveUp,
        sokoban::BindingDeviceClass::Keyboard) == "W");
    CHECK(sokoban::actionBindingsDisplay(
        settings.input,
        sokoban::InputAction::MoveUp,
        sokoban::BindingDeviceClass::Gamepad) ==
        "Pad dpup / Pad lefty-");
    const auto adjustedTab = sokoban::reduceOptionsMenu(
        controls,
        settings,
        sokoban::options::intent::AdjustSelected { 1 });
    CHECK(adjustedTab.state.controlsBindingDevice ==
        sokoban::BindingDeviceClass::Gamepad);

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

    reduction = sokoban::reduceOptionsMenu(
        controls,
        settings,
        sokoban::options::intent::SelectChoice {
            sokoban::OptionsMenuRowId::BindingDevice,
            1,
        });
    CHECK(reduction.state.controlsBindingDevice ==
        sokoban::BindingDeviceClass::Gamepad);
    CHECK(!reduction.action.has_value());
    const std::vector<sokoban::OptionsMenuRow> controllerRows =
        sokoban::optionsMenuRows(reduction.state, settings);
    CHECK(controllerRows.front().choiceValue == 1);

    reduction = sokoban::reduceOptionsMenu(
        reduction.state,
        settings,
        sokoban::options::intent::ActivateRow {
            sokoban::OptionsMenuRowId::MoveUp });
    reduction = sokoban::reduceOptionsMenu(
        reduction.state,
        settings,
        sokoban::options::intent::ProvideBinding {
            sokoban::KeyboardBinding { "P" },
        });
    CHECK(reduction.state.capturingAction ==
        sokoban::InputAction::MoveUp);
    CHECK(!reduction.action.has_value());

    reduction = sokoban::reduceOptionsMenu(
        reduction.state,
        settings,
        sokoban::options::intent::ProvideBinding {
            sokoban::GamepadButtonBinding { "rightshoulder" },
        });
    CHECK(!reduction.state.capturingAction.has_value());
    changed = std::get_if<sokoban::options::SettingsChanged>(
        &*reduction.action);
    CHECK(changed != nullptr);
    CHECK(sokoban::actionBindingsDisplay(
        changed->settings.input,
        sokoban::InputAction::MoveUp,
        sokoban::BindingDeviceClass::Gamepad) ==
        "Pad lefty- / Pad rightshoulder");
    CHECK(sokoban::actionBindingsDisplay(
        changed->settings.input,
        sokoban::InputAction::MoveUp,
        sokoban::BindingDeviceClass::Keyboard) == "W");
}

} // namespace

int main()
{
    testFontAtlasAndText();
    testUiFrameArenaCommandBudget();
    testReusableControls();
    testSelectorPromptShowsInteractAndPreviewBindings();
    testInputPromptCatalogUsesKeyboardAndControllerSpecificGlyphs();
    testScreenPreviewOverlayUsesCenteredSeventyFivePercentInset();
    testLayoutTree();
    testOptionsNavigationAndSettings();
    testControlsRemapping();
    testOptionsReducerAndDeclarativeRows();
    testOptionsReducerDraftAndBindingSemantics();
#if SOKOBAN_ENABLE_DEBUG_UI
    testDebugEditorControlBindings();
#endif

    if (failures == 0) {
        std::cout << "UiTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "UiTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
