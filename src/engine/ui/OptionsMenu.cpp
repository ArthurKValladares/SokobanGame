#include "engine/ui/OptionsMenu.hpp"

#include "engine/render/RenderResolution.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>

namespace sokoban {
namespace {

using uiControls::ButtonTone;
using uiControls::ChoiceOption;

enum class MainRow {
    Graphics,
    Audio,
    Quit,
    Count,
};

enum class GraphicsRow {
    AntiAliasing,
    RenderScalePreset,
    CustomRenderScale,
    AmbientOcclusion,
    Display,
    Back,
    Count,
};

enum class AudioRow {
    MasterVolume,
    MusicVolume,
    Back,
    Count,
};

enum class QuitRow {
    Cancel,
    Confirm,
    Count,
};

template <typename Row>
constexpr int rowIndex(Row row)
{
    return static_cast<int>(row);
}

constexpr std::array sampleCountChoices {
    ChoiceOption { 1, "Off" },
    ChoiceOption { 2, "2x" },
    ChoiceOption { 4, "4x" },
    ChoiceOption { 8, "8x" },
};
constexpr std::array renderScaleChoices {
    ChoiceOption { 100, "100%" },
    ChoiceOption { 75, "75%" },
    ChoiceOption { 67, "67%" },
    ChoiceOption { 50, "50%" },
    ChoiceOption { 25, "25%" },
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

struct LabeledControlLayout {
    UiLayoutNode label;
    UiLayoutNode control;
};

struct MenuPageLayout {
    explicit MenuPageLayout(float afterHeader = 28.0f)
        : tree(UiLayoutAxis::Vertical, { 42.0f, 30.0f, 42.0f, 40.0f })
    {
        title = tree.item(tree.root(), 48.0f);
        tree.spacer(tree.root(), 14.0f);
        divider = tree.item(tree.root(), 1.0f);
        tree.spacer(tree.root(), afterHeader);
    }

    UiLayoutTree tree;
    UiLayoutNode title {};
    UiLayoutNode divider {};
};

LabeledControlLayout addLabeledControl(
    UiLayoutTree& tree,
    UiLayoutNode parent,
    float controlHeight,
    float gap = 4.0f)
{
    const UiLayoutNode group = tree.column(
        parent, UiLayoutSize::content(), gap);
    return {
        .label = tree.item(group, 30.0f),
        .control = tree.item(group, controlHeight),
    };
}

struct MainPageLayout : MenuPageLayout {
    explicit MainPageLayout(UiRect panel)
    {
        graphics = tree.item(tree.root(), 62.0f);
        tree.spacer(tree.root(), 18.0f);
        audio = tree.item(tree.root(), 62.0f);
        tree.flexibleSpacer(tree.root());
        quitDivider = tree.item(tree.root(), 1.0f);
        tree.spacer(tree.root(), 24.0f);
        quit = tree.item(tree.root(), 62.0f);
        tree.arrange(panel);
    }

    UiLayoutNode graphics {};
    UiLayoutNode audio {};
    UiLayoutNode quitDivider {};
    UiLayoutNode quit {};
};

struct GraphicsPageLayout : MenuPageLayout {
    explicit GraphicsPageLayout(UiRect panel)
    {
        antiAliasing = addLabeledControl(tree, tree.root(), 52.0f);
        tree.spacer(tree.root(), 10.0f);
        renderScale = addLabeledControl(tree, tree.root(), 48.0f);
        tree.spacer(tree.root(), 8.0f);

        const UiLayoutNode customGroup = tree.column(
            tree.root(), UiLayoutSize::content(), 4.0f);
        customToggle = tree.item(customGroup, 44.0f);
        customSlider = tree.item(customGroup, 32.0f);
        internalResolution = tree.item(customGroup, 24.0f);

        tree.spacer(tree.root(), 8.0f);
        ambientOcclusion = tree.item(tree.root(), 48.0f);
        tree.spacer(tree.root(), 8.0f);
        display = addLabeledControl(tree, tree.root(), 54.0f);
        tree.flexibleSpacer(tree.root());
        back = tree.item(tree.root(), 52.0f);
        tree.arrange(panel);
    }

    LabeledControlLayout antiAliasing {};
    LabeledControlLayout renderScale {};
    UiLayoutNode customToggle {};
    UiLayoutNode customSlider {};
    UiLayoutNode internalResolution {};
    UiLayoutNode ambientOcclusion {};
    LabeledControlLayout display {};
    UiLayoutNode back {};
};

struct AudioPageLayout : MenuPageLayout {
    explicit AudioPageLayout(UiRect panel)
    {
        masterVolume = addLabeledControl(tree, tree.root(), 34.0f, 8.0f);
        tree.spacer(tree.root(), 38.0f);
        musicVolume = addLabeledControl(tree, tree.root(), 34.0f, 8.0f);
        tree.flexibleSpacer(tree.root());
        back = tree.item(tree.root(), 52.0f);
        tree.arrange(panel);
    }

    LabeledControlLayout masterVolume {};
    LabeledControlLayout musicVolume {};
    UiLayoutNode back {};
};

struct QuitPageLayout : MenuPageLayout {
    explicit QuitPageLayout(UiRect panel)
    {
        tree.spacer(tree.root(), 20.0f);
        message = tree.item(tree.root(), 44.0f);
        tree.spacer(tree.root(), 74.0f);
        cancel = tree.item(tree.root(), 58.0f);
        tree.spacer(tree.root(), 20.0f);
        quit = tree.item(tree.root(), 58.0f);
        tree.flexibleSpacer(tree.root());
        tree.arrange(panel);
    }

    UiLayoutNode message {};
    UiLayoutNode cancel {};
    UiLayoutNode quit {};
};

void drawTitle(
    UiContext& ui,
    const MenuPageLayout& layout,
    std::string_view title)
{
    ui.centeredText(layout.tree.rect(layout.title), title,
        { 0.94f, 0.96f, 0.93f, 1.0f }, 36.0f);
    ui.divider(layout.tree.rect(layout.divider));
}

void drawTrailingText(
    UiContext& ui,
    UiRect row,
    std::string_view text,
    Vec4 color,
    float size)
{
    const Vec2 measured = ui.measureText(text, size);
    ui.text({
        row.position.x + row.size.x - measured.x,
        row.position.y + (row.size.y - size) * 0.5f,
    }, text, color, size);
}

bool cycleIndex(
    int& value,
    int count,
    bool previous,
    bool next)
{
    const int oldValue = value;
    if (previous) {
        value = (value + count - 1) % count;
    }
    if (next) {
        value = (value + 1) % count;
    }
    return value != oldValue;
}

bool adjustInt(int& value, bool decrease, bool increase, int step = 1)
{
    const int oldValue = value;
    if (decrease) {
        value -= step;
    }
    if (increase) {
        value += step;
    }
    return value != oldValue;
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
    navigateRows(input, rowIndex(MainRow::Count));

    MainPageLayout layout(panel);

    drawTitle(ui, layout, "OPTIONS");
    if (uiControls::button(
            ui, "options.graphics", layout.tree.rect(layout.graphics), "Graphics", {
            .tone = ButtonTone::Accent,
            .focused = selectedRow_ == rowIndex(MainRow::Graphics),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Graphics),
        })) {
        setPage(Page::Graphics);
    }
    if (uiControls::button(
            ui, "options.audio", layout.tree.rect(layout.audio), "Audio", {
            .focused = selectedRow_ == rowIndex(MainRow::Audio),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Audio),
        })) {
        setPage(Page::Audio);
    }

    ui.divider(layout.tree.rect(layout.quitDivider));
    if (uiControls::button(
            ui, "options.quit", layout.tree.rect(layout.quit), "Quit Game", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == rowIndex(MainRow::Quit),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Quit),
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
    navigateRows(input, rowIndex(GraphicsRow::Count));
    OptionsMenuResult result;
    if (customRenderScaleDragPending_ && !ui.mouseDown()) {
        customRenderScaleDragPending_ = false;
        result.settingsChanged = true;
    }

    GraphicsPageLayout layout(panel);

    drawTitle(ui, layout, "GRAPHICS");

    const bool antiAliasingFocused =
        selectedRow_ == rowIndex(GraphicsRow::AntiAliasing);
    ui.text(layout.tree.rect(layout.antiAliasing.label).position, "Anti-aliasing",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    if (uiControls::segmentedControl(
            ui, "graphics.msaa", layout.tree.rect(layout.antiAliasing.control),
            sampleCountChoices, settings_.antiAliasingSamples, {
                .focused = antiAliasingFocused,
                .selectPrevious = antiAliasingFocused && input.left,
                .selectNext = antiAliasingFocused && input.right,
            })) {
        result.settingsChanged = true;
    }

    const bool renderScalePresetFocused =
        selectedRow_ == rowIndex(GraphicsRow::RenderScalePreset);
    ui.text(layout.tree.rect(layout.renderScale.label).position, "Render scale",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    const bool presetChosen = uiControls::segmentedControl(
            ui, "graphics.render-scale", layout.tree.rect(layout.renderScale.control),
            renderScaleChoices, settings_.renderScalePercent, {
                .focused = renderScalePresetFocused,
                .selectPrevious = renderScalePresetFocused && input.left,
                .selectNext = renderScalePresetFocused && input.right,
            });
    if (presetChosen) {
        result.settingsChanged = true;
        if (settings_.customRenderScale) {
            settings_.customRenderScale = false;
        }
    }

    const bool customRenderScaleFocused =
        selectedRow_ == rowIndex(GraphicsRow::CustomRenderScale);
    if (uiControls::checkbox(
            ui, "graphics.custom-render-scale", layout.tree.rect(layout.customToggle),
            "Custom", settings_.customRenderScale,
            customRenderScaleFocused,
            input.confirm && customRenderScaleFocused)) {
        result.settingsChanged = true;
    }
    if (settings_.customRenderScale && customRenderScaleFocused) {
        result.settingsChanged |= adjustInt(
            settings_.customRenderScalePercent, input.left, input.right);
    }
    settings_.customRenderScalePercent = normalizedRenderScalePercent(
        settings_.customRenderScalePercent);
    float customRenderScale =
        static_cast<float>(settings_.customRenderScalePercent) / 100.0f;
    if (uiControls::slider(
            ui, "graphics.custom-render-scale-value",
            layout.tree.rect(layout.customSlider), customRenderScale,
            0.25f, 1.0f,
            customRenderScaleFocused && settings_.customRenderScale,
            settings_.customRenderScale)) {
        settings_.customRenderScalePercent = static_cast<int>(
            std::round(customRenderScale * 100.0f));
        customRenderScaleDragPending_ = true;
    }
    const std::string customScaleText =
        std::to_string(settings_.customRenderScalePercent) + "%";
    drawTrailingText(
        ui, layout.tree.rect(layout.customToggle),
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
    ui.text(layout.tree.rect(layout.internalResolution).position, internalResolution,
        { 0.58f, 0.63f, 0.62f, 1.0f }, 18.0f);

    const bool ambientOcclusionFocused =
        selectedRow_ == rowIndex(GraphicsRow::AmbientOcclusion);
    if (uiControls::checkbox(
            ui, "graphics.ao", layout.tree.rect(layout.ambientOcclusion),
            "Ambient occlusion", settings_.ambientOcclusion,
            ambientOcclusionFocused,
            input.confirm && ambientOcclusionFocused)) {
        result.settingsChanged = true;
    }

    int selectedDisplay = displayIndex(settings_);
    const bool displayFocused = selectedRow_ == rowIndex(GraphicsRow::Display);
    (void)cycleIndex(
        selectedDisplay,
        static_cast<int>(displayModes.size()),
        displayFocused && input.left,
        displayFocused && input.right);
    ui.text(layout.tree.rect(layout.display.label).position, "Display",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    if (uiControls::choiceStepper(
            ui, "graphics.display", layout.tree.rect(layout.display.control),
            displayLabels, selectedDisplay, displayFocused)) {
        result.settingsChanged = true;
    }
    const DisplayMode& mode = displayModes[static_cast<size_t>(selectedDisplay)];
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

    const bool backFocused = selectedRow_ == rowIndex(GraphicsRow::Back);
    if (uiControls::button(
            ui, "graphics.back", layout.tree.rect(layout.back), "Back", {
        .focused = backFocused,
        .activate = input.confirm && backFocused,
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
    navigateRows(input, rowIndex(AudioRow::Count));
    OptionsMenuResult result;

    AudioPageLayout layout(panel);

    drawTitle(ui, layout, "AUDIO");

    auto keyboardAdjust = [&](AudioRow row, float& value) {
        const float old = value;
        if (selectedRow_ == rowIndex(row) && input.left) {
            value -= 0.05f;
        }
        if (selectedRow_ == rowIndex(row) && input.right) {
            value += 0.05f;
        }
        value = std::clamp(value, 0.0f, 1.0f);
        return std::abs(value - old) > 0.0001f;
    };

    const bool masterVolumeFocused =
        selectedRow_ == rowIndex(AudioRow::MasterVolume);
    ui.text(layout.tree.rect(layout.masterVolume.label).position, "Master volume",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    result.settingsChanged |= keyboardAdjust(
        AudioRow::MasterVolume, settings_.masterVolume);
    result.settingsChanged |= uiControls::slider(
        ui, "audio.master", layout.tree.rect(layout.masterVolume.control),
        settings_.masterVolume, 0.0f, 1.0f, masterVolumeFocused);
    const int masterPercent = static_cast<int>(std::round(settings_.masterVolume * 100.0f));
    const std::string masterText = std::to_string(masterPercent) + "%";
    drawTrailingText(ui, layout.tree.rect(layout.masterVolume.label),
        masterText, { 0.68f, 0.88f, 0.82f, 1.0f }, 20.0f);

    const bool musicVolumeFocused =
        selectedRow_ == rowIndex(AudioRow::MusicVolume);
    ui.text(layout.tree.rect(layout.musicVolume.label).position, "Music volume",
        { 0.83f, 0.86f, 0.83f, 1.0f }, 22.0f);
    result.settingsChanged |= keyboardAdjust(
        AudioRow::MusicVolume, settings_.musicVolume);
    result.settingsChanged |= uiControls::slider(
        ui, "audio.music", layout.tree.rect(layout.musicVolume.control),
        settings_.musicVolume, 0.0f, 1.0f, musicVolumeFocused);
    const int musicPercent = static_cast<int>(std::round(settings_.musicVolume * 100.0f));
    const std::string musicText = std::to_string(musicPercent) + "%";
    drawTrailingText(ui, layout.tree.rect(layout.musicVolume.label),
        musicText, { 0.68f, 0.88f, 0.82f, 1.0f }, 20.0f);

    const bool backFocused = selectedRow_ == rowIndex(AudioRow::Back);
    if (uiControls::button(
            ui, "audio.back", layout.tree.rect(layout.back), "Back", {
            .focused = backFocused,
            .activate = input.confirm && backFocused,
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
    navigateRows(input, rowIndex(QuitRow::Count));

    QuitPageLayout layout(panel);

    drawTitle(ui, layout, "QUIT GAME?");
    ui.centeredText(layout.tree.rect(layout.message),
        "Your progress is saved automatically.",
        { 0.72f, 0.76f, 0.74f, 1.0f }, 20.0f);

    const bool cancelFocused = selectedRow_ == rowIndex(QuitRow::Cancel);
    if (uiControls::button(
            ui, "quit.cancel", layout.tree.rect(layout.cancel), "Cancel", {
            .focused = cancelFocused,
            .activate = input.confirm && cancelFocused,
        })) {
        setPage(Page::Main);
    }
    OptionsMenuResult result;
    const bool quitFocused = selectedRow_ == rowIndex(QuitRow::Confirm);
    if (uiControls::button(
            ui, "quit.confirm", layout.tree.rect(layout.quit), "Quit Game", {
            .tone = ButtonTone::Danger,
            .focused = quitFocused,
            .activate = input.confirm && quitFocused,
        })) {
        result.quitRequested = true;
    }
    return result;
}

} // namespace sokoban
