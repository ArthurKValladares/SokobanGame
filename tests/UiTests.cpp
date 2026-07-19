#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"

#include <algorithm>
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

    bool checked = false;
    ui.beginFrame({ 400.0f, 200.0f }, { 20.0f, 135.0f }, true, true);
    CHECK(sokoban::uiControls::checkbox(
        ui, "check", { { 10.0f, 120.0f }, { 180.0f, 48.0f } },
        "Enabled", checked));
    CHECK(checked);
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
    testOptionsNavigationAndSettings();

    if (failures == 0) {
        std::cout << "UiTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "UiTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
