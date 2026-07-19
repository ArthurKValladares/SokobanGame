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

// Rows on the main page in order; Title is skipped when the menu is opened
// from the title screen itself.
enum class MainRow {
    Graphics,
    Audio,
    Controls,
    Title,
    Quit,
    Count,
};

// The Controls page lists these remappable gameplay actions in order,
// followed by Reset To Defaults and Back rows. Menu navigation stays fixed
// so a bad remap cannot lock the player out of the menus.
constexpr std::array remappableActions {
    InputAction::MoveUp,
    InputAction::MoveDown,
    InputAction::MoveLeft,
    InputAction::MoveRight,
    InputAction::Undo,
    InputAction::Restart,
};

constexpr std::array<std::string_view, remappableActions.size()> remappableActionLabels {
    "Move up",
    "Move down",
    "Move left",
    "Move right",
    "Undo",
    "Restart",
};

constexpr int controlsResetRow = static_cast<int>(remappableActions.size());
constexpr int controlsBackRow = controlsResetRow + 1;
constexpr int controlsRowCount = controlsBackRow + 1;

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
    MainPageLayout(UiRect panel, bool withTitleExit)
    {
        graphics = tree.item(tree.root(), 62.0f);
        tree.spacer(tree.root(), 18.0f);
        audio = tree.item(tree.root(), 62.0f);
        tree.spacer(tree.root(), 18.0f);
        controls = tree.item(tree.root(), 62.0f);
        if (withTitleExit) {
            tree.spacer(tree.root(), 18.0f);
            title = tree.item(tree.root(), 62.0f);
        }
        tree.flexibleSpacer(tree.root());
        quitDivider = tree.item(tree.root(), 1.0f);
        tree.spacer(tree.root(), 24.0f);
        quit = tree.item(tree.root(), 62.0f);
        tree.arrange(panel);
    }

    UiLayoutNode graphics {};
    UiLayoutNode audio {};
    UiLayoutNode controls {};
    UiLayoutNode title {};
    UiLayoutNode quitDivider {};
    UiLayoutNode quit {};
};

struct ControlsPageLayout : MenuPageLayout {
    explicit ControlsPageLayout(UiRect panel)
        : MenuPageLayout(18.0f)
    {
        for (UiLayoutNode& action : actions) {
            action = tree.item(tree.root(), 46.0f);
            tree.spacer(tree.root(), 8.0f);
        }
        tree.spacer(tree.root(), 6.0f);
        prompt = tree.item(tree.root(), 26.0f);
        tree.flexibleSpacer(tree.root());
        reset = tree.item(tree.root(), 48.0f);
        tree.spacer(tree.root(), 12.0f);
        back = tree.item(tree.root(), 48.0f);
        tree.arrange(panel);
    }

    std::array<UiLayoutNode, remappableActions.size()> actions {};
    UiLayoutNode prompt {};
    UiLayoutNode reset {};
    UiLayoutNode back {};
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

void OptionsMenu::open(OptionsMenuSettings settings, bool allowTitleExit)
{
    settings_ = settings;
    allowTitleExit_ = allowTitleExit;
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
    if (capturingAction_) {
        capturingAction_.reset();
        return;
    }
    if (page_ == Page::Main) {
        close();
    } else {
        setPage(Page::Main);
    }
}

void OptionsMenu::provideBindingCandidate(const InputBinding& candidate)
{
    if (!capturingAction_) {
        return;
    }
    // Escape stays capturing here and cancels through the caller's MenuBack
    // routing; Start cancels directly because its raw event is suppressed.
    // Neither can be bound or stolen from the menus.
    if (const KeyboardBinding* key = std::get_if<KeyboardBinding>(&candidate);
        key != nullptr && key->scancode == "Escape") {
        return;
    }
    if (const GamepadButtonBinding* button = std::get_if<GamepadButtonBinding>(&candidate);
        button != nullptr && button->button == "start") {
        capturingAction_.reset();
        return;
    }
    assignBinding(settings_.input, *capturingAction_, candidate);
    capturingAction_.reset();
    bindingAssigned_ = true;
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
    const float height = page_ == Page::Graphics
        ? 740.0f
        : (page_ == Page::Controls ? 700.0f : 540.0f);
    const UiRect panel = centeredPanel(viewport, height);
    ui.panel(panel);

    switch (page_) {
    case Page::Main: return drawMain(ui, panel, input);
    case Page::Graphics: return drawGraphics(ui, panel, viewport, input);
    case Page::Audio: return drawAudio(ui, panel, input);
    case Page::Controls: return drawControls(ui, panel, input);
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
    capturingAction_.reset();
}

OptionsMenuResult OptionsMenu::drawMain(
    UiContext& ui,
    UiRect panel,
    const OptionsMenuInput& input)
{
    // Without the title-exit entry the Quit row directly follows Audio.
    const int titleRowIndex = rowIndex(MainRow::Title);
    const int quitRowIndex = allowTitleExit_
        ? rowIndex(MainRow::Quit)
        : rowIndex(MainRow::Title);
    // (Controls precedes Title/Quit and is always present.)
    navigateRows(input, quitRowIndex + 1);

    MainPageLayout layout(panel, allowTitleExit_);

    drawTitle(ui, layout, "OPTIONS");
    OptionsMenuResult result;
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
    if (uiControls::button(
            ui, "options.controls", layout.tree.rect(layout.controls), "Controls", {
            .focused = selectedRow_ == rowIndex(MainRow::Controls),
            .activate = input.confirm && selectedRow_ == rowIndex(MainRow::Controls),
        })) {
        setPage(Page::Controls);
    }
    if (allowTitleExit_ &&
        uiControls::button(
            ui, "options.title", layout.tree.rect(layout.title), "Exit To Title", {
            .focused = selectedRow_ == titleRowIndex,
            .activate = input.confirm && selectedRow_ == titleRowIndex,
        })) {
        result.titleRequested = true;
    }

    ui.divider(layout.tree.rect(layout.quitDivider));
    if (uiControls::button(
            ui, "options.quit", layout.tree.rect(layout.quit), "Quit Game", {
            .tone = ButtonTone::Danger,
            .focused = selectedRow_ == quitRowIndex,
            .activate = input.confirm && selectedRow_ == quitRowIndex,
        })) {
        setPage(Page::QuitConfirmation);
    }
    return result;
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

OptionsMenuResult OptionsMenu::drawControls(
    UiContext& ui,
    UiRect panel,
    const OptionsMenuInput& input)
{
    OptionsMenuResult result;
    if (bindingAssigned_) {
        bindingAssigned_ = false;
        result.settingsChanged = true;
    }

    // Navigation freezes while a capture is pending; Escape/Start cancel via
    // back() in the caller.
    if (!capturingAction_) {
        navigateRows(input, controlsRowCount);
    }

    ControlsPageLayout layout(panel);
    drawTitle(ui, layout, "CONTROLS");

    for (std::size_t i = 0; i < remappableActions.size(); ++i) {
        const InputAction action = remappableActions[i];
        const bool focused = selectedRow_ == static_cast<int>(i);
        const bool capturingThis = capturingAction_ == action;
        const UiRect row = layout.tree.rect(layout.actions[i]);
        if (uiControls::button(
                ui, std::string("controls.action-") + std::to_string(i), row,
                remappableActionLabels[i], {
                .tone = capturingThis
                    ? uiControls::ButtonTone::Accent
                    : uiControls::ButtonTone::Normal,
                .focused = focused,
                .activate = input.confirm && focused && !capturingAction_,
            })) {
            capturingAction_ = action;
        }
        drawTrailingText(
            ui, row,
            capturingThis
                ? "Press a key or button..."
                : actionBindingsDisplay(settings_.input, action),
            capturingThis
                ? Vec4 { 0.98f, 0.84f, 0.42f, 1.0f }
                : (focused
                        ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                        : Vec4 { 0.62f, 0.67f, 0.65f, 1.0f }),
            18.0f);
    }

    ui.centeredText(layout.tree.rect(layout.prompt),
        capturingAction_
            ? "Esc or Start cancels. Rebinding steals duplicates."
            : "Confirm a row to rebind it. Keyboard and pad bindings coexist.",
        { 0.58f, 0.63f, 0.62f, 1.0f }, 16.0f);

    const bool resetFocused = selectedRow_ == controlsResetRow;
    if (uiControls::button(
            ui, "controls.reset", layout.tree.rect(layout.reset),
            "Reset To Defaults", {
            .focused = resetFocused,
            .activate = input.confirm && resetFocused && !capturingAction_,
        })) {
        if (!(settings_.input == defaultInputBindings())) {
            settings_.input = defaultInputBindings();
            result.settingsChanged = true;
        }
    }

    const bool backFocused = selectedRow_ == controlsBackRow;
    if (uiControls::button(
            ui, "controls.back", layout.tree.rect(layout.back), "Back", {
            .focused = backFocused,
            .activate = input.confirm && backFocused && !capturingAction_,
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
