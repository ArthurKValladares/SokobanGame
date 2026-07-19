#include "engine/ui/OptionsMenu.hpp"

#include "engine/render/RenderResolution.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

using uiControls::ButtonTone;

constexpr std::array<int, 4> sampleCounts { 1, 2, 4, 8 };
constexpr std::array<std::string_view, 4> sampleLabels { "Off", "2x", "4x", "8x" };
constexpr std::array<int, 5> renderScales { 100, 75, 67, 50, 25 };
constexpr std::array<std::string_view, 5> renderScaleLabels {
    "100%", "75%", "67%", "50%", "25%",
};

struct DisplayMode {
    std::string_view label;
    bool fullscreen;
    int width;
    int height;
};

constexpr std::array<DisplayMode, 5> displayModes {
    DisplayMode { "Fullscreen", true, 0, 0 },
    DisplayMode { "1920 x 1080", false, 1920, 1080 },
    DisplayMode { "1600 x 900", false, 1600, 900 },
    DisplayMode { "1280 x 720", false, 1280, 720 },
    DisplayMode { "1024 x 768", false, 1024, 768 },
};

constexpr std::array<std::string_view, displayModes.size()> displayLabels {
    "Fullscreen", "1920 x 1080", "1600 x 900", "1280 x 720", "1024 x 768",
};

int sampleIndex(int samples)
{
    const auto found = std::find(sampleCounts.begin(), sampleCounts.end(), samples);
    return found == sampleCounts.end() ? 3 : static_cast<int>(found - sampleCounts.begin());
}

int renderScaleIndex(int percent)
{
    const auto found = std::find(renderScales.begin(), renderScales.end(), percent);
    return found == renderScales.end()
        ? 0
        : static_cast<int>(found - renderScales.begin());
}

int displayIndex(const OptionsMenuSettings& settings)
{
    if (settings.fullscreen) {
        return 0;
    }
    for (size_t index = 1; index < displayModes.size(); ++index) {
        if (displayModes[index].width == settings.windowWidth &&
            displayModes[index].height == settings.windowHeight) {
            return static_cast<int>(index);
        }
    }
    return 3;
}

UiRect centeredPanel(Vec2 viewport, float desiredHeight)
{
    const float width = std::max(std::min(560.0f, viewport.x - 32.0f), 320.0f);
    const float height = std::max(std::min(desiredHeight, viewport.y - 32.0f), 400.0f);
    return {
        { (viewport.x - width) * 0.5f, (viewport.y - height) * 0.5f },
        { width, height },
    };
}

UiRect contentRect(UiRect panel, float y, float height)
{
    return {
        { panel.position.x + 42.0f, panel.position.y + y },
        { panel.size.x - 84.0f, height },
    };
}

void drawTitle(UiContext& ui, UiRect panel, std::string_view title)
{
    ui.centeredText(contentRect(panel, 30.0f, 48.0f), title,
        { 0.94f, 0.96f, 0.93f, 1.0f }, 36.0f);
    ui.divider(contentRect(panel, 92.0f, 1.0f));
}

} // namespace

void OptionsMenu::open(OptionsMenuSettings settings)
{
    settings_ = settings;
    open_ = true;
    setPage(Page::Main);
}

void OptionsMenu::close()
{
    open_ = false;
    setPage(Page::Main);
}

void OptionsMenu::back()
{
    if (!open_) {
        return;
    }
    if (page_ == Page::Main) {
        close();
    } else {
        setPage(Page::Main);
    }
}

void OptionsMenu::requestQuitConfirmation()
{
    open_ = true;
    setPage(Page::QuitConfirmation);
}

OptionsMenuResult OptionsMenu::draw(
    UiContext& ui,
    Vec2 viewport,
    const OptionsMenuInput& input)
{
    if (!open_) {
        return {};
    }

    ui.rect({ { 0.0f, 0.0f }, viewport }, { 0.015f, 0.020f, 0.021f, 0.78f });
    const float height = page_ == Page::Graphics ? 740.0f : 540.0f;
    const UiRect panel = centeredPanel(viewport, height);
    ui.panel(panel);

    switch (page_) {
    case Page::Main: return drawMain(ui, panel, input);
    case Page::Graphics: return drawGraphics(ui, panel, viewport, input);
    case Page::Audio: return drawAudio(ui, panel, input);
    case Page::QuitConfirmation: return drawQuitConfirmation(ui, panel, input);
    }
    return {};
}

void OptionsMenu::navigateRows(const OptionsMenuInput& input, int rowCount)
{
    if (input.up) {
        selectedRow_ = (selectedRow_ + rowCount - 1) % rowCount;
    }
    if (input.down) {
        selectedRow_ = (selectedRow_ + 1) % rowCount;
    }
}

void OptionsMenu::setPage(Page page)
{
    page_ = page;
    selectedRow_ = 0;
    customRenderScaleDragPending_ = false;
}

OptionsMenuResult OptionsMenu::drawMain(
    UiContext& ui,
    UiRect panel,
    const OptionsMenuInput& input)
{
    navigateRows(input, 3);
    drawTitle(ui, panel, "OPTIONS");

    const UiRect graphics = contentRect(panel, 132.0f, 62.0f);
    if (uiControls::button(ui, "options.graphics", graphics, "Graphics", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == 0,
            .activate = input.confirm && selectedRow_ == 0,
        })) {
        setPage(Page::Graphics);
    }
    const UiRect audio = contentRect(panel, 212.0f, 62.0f);
    if (uiControls::button(ui, "options.audio", audio, "Audio", {
            .focused = selectedRow_ == 1,
            .activate = input.confirm && selectedRow_ == 1,
        })) {
        setPage(Page::Audio);
    }

    ui.divider(contentRect(panel, panel.size.y - 156.0f, 1.0f));
    const UiRect quit = contentRect(panel, panel.size.y - 116.0f, 62.0f);
    if (uiControls::button(ui, "options.quit", quit, "Quit Game", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == 2,
            .activate = input.confirm && selectedRow_ == 2,
        })) {
        setPage(Page::QuitConfirmation);
    }
    return {};
}

OptionsMenuResult OptionsMenu::drawGraphics(
    UiContext& ui,
    UiRect panel,
    Vec2 viewport,
    const OptionsMenuInput& input)
{
    navigateRows(input, 6);
    OptionsMenuResult result;
    if (customRenderScaleDragPending_ && !ui.mouseDown()) {
        customRenderScaleDragPending_ = false;
        result.settingsChanged = true;
    }
    drawTitle(ui, panel, "GRAPHICS");

    int samples = sampleIndex(settings_.antiAliasingSamples);
    if (selectedRow_ == 0 && input.left) {
        samples = (samples + static_cast<int>(sampleCounts.size()) - 1) % static_cast<int>(sampleCounts.size());
    }
    if (selectedRow_ == 0 && input.right) {
        samples = (samples + 1) % static_cast<int>(sampleCounts.size());
    }
    ui.text(contentRect(panel, 122.0f, 30.0f).position, "Anti-aliasing",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    if (uiControls::segmentedControl(
            ui, "graphics.msaa", contentRect(panel, 158.0f, 52.0f),
            sampleLabels, samples, selectedRow_ == 0)) {
        result.settingsChanged = true;
    }
    if (settings_.antiAliasingSamples != sampleCounts[static_cast<size_t>(samples)]) {
        settings_.antiAliasingSamples = sampleCounts[static_cast<size_t>(samples)];
        result.settingsChanged = true;
    }

    int renderScale = renderScaleIndex(settings_.renderScalePercent);
    bool presetChosen = false;
    if (selectedRow_ == 1 && input.left) {
        renderScale = (renderScale + static_cast<int>(renderScales.size()) - 1) %
            static_cast<int>(renderScales.size());
        presetChosen = true;
    }
    if (selectedRow_ == 1 && input.right) {
        renderScale = (renderScale + 1) % static_cast<int>(renderScales.size());
        presetChosen = true;
    }
    ui.text(contentRect(panel, 220.0f, 30.0f).position, "Render scale",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    if (uiControls::segmentedControl(
            ui, "graphics.render-scale", contentRect(panel, 252.0f, 48.0f),
            renderScaleLabels, renderScale, selectedRow_ == 1)) {
        presetChosen = true;
        result.settingsChanged = true;
    }
    if (settings_.renderScalePercent != renderScales[static_cast<size_t>(renderScale)]) {
        settings_.renderScalePercent = renderScales[static_cast<size_t>(renderScale)];
        result.settingsChanged = true;
    }
    if (presetChosen && settings_.customRenderScale) {
        settings_.customRenderScale = false;
        result.settingsChanged = true;
    }

    if (uiControls::checkbox(
            ui, "graphics.custom-render-scale", contentRect(panel, 308.0f, 44.0f),
            "Custom", settings_.customRenderScale,
            selectedRow_ == 2, input.confirm && selectedRow_ == 2)) {
        result.settingsChanged = true;
    }
    if (settings_.customRenderScale && selectedRow_ == 2 && input.left) {
        --settings_.customRenderScalePercent;
        result.settingsChanged = true;
    }
    if (settings_.customRenderScale && selectedRow_ == 2 && input.right) {
        ++settings_.customRenderScalePercent;
        result.settingsChanged = true;
    }
    settings_.customRenderScalePercent = normalizedRenderScalePercent(
        settings_.customRenderScalePercent);
    float customRenderScale =
        static_cast<float>(settings_.customRenderScalePercent) / 100.0f;
    if (uiControls::slider(
            ui, "graphics.custom-render-scale-value",
            contentRect(panel, 354.0f, 32.0f), customRenderScale,
            0.25f, 1.0f,
            selectedRow_ == 2 && settings_.customRenderScale,
            settings_.customRenderScale)) {
        settings_.customRenderScalePercent = static_cast<int>(
            std::round(customRenderScale * 100.0f));
        customRenderScaleDragPending_ = true;
    }
    const std::string customScaleText =
        std::to_string(settings_.customRenderScalePercent) + "%";
    ui.text({ panel.position.x + panel.size.x - 82.0f, panel.position.y + 312.0f },
        customScaleText,
        settings_.customRenderScale
            ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
            : Vec4 { 0.58f, 0.61f, 0.60f, 0.45f },
        20.0f);

    const int effectiveRenderScale = settings_.customRenderScale
        ? settings_.customRenderScalePercent
        : settings_.renderScalePercent;
    const PixelExtent internalExtent = scaledRenderExtent({
        .width = static_cast<uint32_t>(std::max(viewport.x, 0.0f)),
        .height = static_cast<uint32_t>(std::max(viewport.y, 0.0f)),
    }, effectiveRenderScale);
    const std::string internalResolution =
        std::to_string(internalExtent.width) + " x " +
        std::to_string(internalExtent.height) + " internal";
    ui.text(contentRect(panel, 394.0f, 24.0f).position, internalResolution,
        { 0.58f, 0.63f, 0.62f, 1.0f }, 18.0f);

    if (uiControls::checkbox(
            ui, "graphics.ao", contentRect(panel, 426.0f, 48.0f),
            "Ambient occlusion", settings_.ambientOcclusion,
            selectedRow_ == 3, input.confirm && selectedRow_ == 3)) {
        result.settingsChanged = true;
    }

    int display = displayIndex(settings_);
    if (selectedRow_ == 4 && input.left) {
        display = (display + static_cast<int>(displayModes.size()) - 1) % static_cast<int>(displayModes.size());
    }
    if (selectedRow_ == 4 && input.right) {
        display = (display + 1) % static_cast<int>(displayModes.size());
    }
    ui.text(contentRect(panel, 486.0f, 30.0f).position, "Display",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    if (uiControls::choiceStepper(
            ui, "graphics.display", contentRect(panel, 518.0f, 54.0f),
            displayLabels, display, selectedRow_ == 4)) {
        result.settingsChanged = true;
    }
    const DisplayMode& mode = displayModes[static_cast<size_t>(display)];
    if (settings_.fullscreen != mode.fullscreen ||
        (!mode.fullscreen &&
            (settings_.windowWidth != mode.width || settings_.windowHeight != mode.height))) {
        settings_.fullscreen = mode.fullscreen;
        if (!mode.fullscreen) {
            settings_.windowWidth = mode.width;
            settings_.windowHeight = mode.height;
        }
        result.settingsChanged = true;
    }

    const UiRect backButton = contentRect(panel, panel.size.y - 92.0f, 52.0f);
    if (uiControls::button(ui, "graphics.back", backButton, "Back", {
        .focused = selectedRow_ == 5,
        .activate = input.confirm && selectedRow_ == 5,
        })) {
        setPage(Page::Main);
    }
    return result;
}

OptionsMenuResult OptionsMenu::drawAudio(
    UiContext& ui,
    UiRect panel,
    const OptionsMenuInput& input)
{
    navigateRows(input, 3);
    OptionsMenuResult result;
    drawTitle(ui, panel, "AUDIO");

    auto keyboardAdjust = [&](int row, float& value) {
        const float old = value;
        if (selectedRow_ == row && input.left) {
            value -= 0.05f;
        }
        if (selectedRow_ == row && input.right) {
            value += 0.05f;
        }
        value = std::clamp(value, 0.0f, 1.0f);
        return std::abs(value - old) > 0.0001f;
    };

    ui.text(contentRect(panel, 130.0f, 30.0f).position, "Master volume",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    result.settingsChanged |= keyboardAdjust(0, settings_.masterVolume);
    result.settingsChanged |= uiControls::slider(
        ui, "audio.master", contentRect(panel, 170.0f, 34.0f),
        settings_.masterVolume, 0.0f, 1.0f, selectedRow_ == 0);
    const int masterPercent = static_cast<int>(std::round(settings_.masterVolume * 100.0f));
    const std::string masterText = std::to_string(masterPercent) + "%";
    ui.text({ panel.position.x + panel.size.x - 82.0f, panel.position.y + 126.0f },
        masterText, { 0.68f, 0.88f, 0.82f, 1.0f }, 20.0f);

    ui.text(contentRect(panel, 250.0f, 30.0f).position, "Music volume",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    result.settingsChanged |= keyboardAdjust(1, settings_.musicVolume);
    result.settingsChanged |= uiControls::slider(
        ui, "audio.music", contentRect(panel, 290.0f, 34.0f),
        settings_.musicVolume, 0.0f, 1.0f, selectedRow_ == 1);
    const int musicPercent = static_cast<int>(std::round(settings_.musicVolume * 100.0f));
    const std::string musicText = std::to_string(musicPercent) + "%";
    ui.text({ panel.position.x + panel.size.x - 82.0f, panel.position.y + 246.0f },
        musicText, { 0.68f, 0.88f, 0.82f, 1.0f }, 20.0f);

    const UiRect backButton = contentRect(panel, panel.size.y - 92.0f, 52.0f);
    if (uiControls::button(ui, "audio.back", backButton, "Back", {
            .focused = selectedRow_ == 2,
            .activate = input.confirm && selectedRow_ == 2,
        })) {
        setPage(Page::Main);
    }
    return result;
}

OptionsMenuResult OptionsMenu::drawQuitConfirmation(
    UiContext& ui,
    UiRect panel,
    const OptionsMenuInput& input)
{
    navigateRows(input, 2);
    drawTitle(ui, panel, "QUIT GAME?");
    ui.centeredText(contentRect(panel, 150.0f, 44.0f),
        "Your progress is saved automatically.",
        { 0.72f, 0.76f, 0.74f, 1.0f }, 20.0f);

    const UiRect cancel = contentRect(panel, 260.0f, 58.0f);
    if (uiControls::button(ui, "quit.cancel", cancel, "Cancel", {
            .focused = selectedRow_ == 0,
            .activate = input.confirm && selectedRow_ == 0,
        })) {
        setPage(Page::Main);
    }
    const UiRect quit = contentRect(panel, 338.0f, 58.0f);
    OptionsMenuResult result;
    if (uiControls::button(ui, "quit.confirm", quit, "Quit Game", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == 1,
            .activate = input.confirm && selectedRow_ == 1,
        })) {
        result.quitRequested = true;
    }
    return result;
}

} // namespace sokoban
