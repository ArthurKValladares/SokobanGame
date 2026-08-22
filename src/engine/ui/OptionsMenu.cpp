#include "engine/ui/OptionsMenu.hpp"

#include "engine/ui/InputPrompts.hpp"

#include "engine/Flow.hpp"
#include "engine/render/RenderResolution.hpp"
#include "engine/ui/MenuKit.hpp"
#include "engine/ui/Ui.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace sokoban {
namespace {

constexpr std::array sampleCountChoices {
    OptionsMenuChoice { 1, "Off" },
    OptionsMenuChoice { 2, "2x" },
    OptionsMenuChoice { 4, "4x" },
    OptionsMenuChoice { 8, "8x" },
};

constexpr std::array renderScaleChoices {
    OptionsMenuChoice { 100, "100%" },
    OptionsMenuChoice { 75, "75%" },
    OptionsMenuChoice { 67, "67%" },
    OptionsMenuChoice { 50, "50%" },
    OptionsMenuChoice { 25, "25%" },
};

struct DisplayMode {
    OptionsMenuChoice choice;
    bool fullscreen = false;
    int width = 1280;
    int height = 720;
};

constexpr std::array displayModes {
    DisplayMode { { 0, "Fullscreen" }, true, 0, 0 },
    DisplayMode { { 1, "1920 x 1080" }, false, 1920, 1080 },
    DisplayMode { { 2, "1600 x 900" }, false, 1600, 900 },
    DisplayMode { { 3, "1280 x 720" }, false, 1280, 720 },
    DisplayMode { { 4, "1024 x 768" }, false, 1024, 768 },
};

constexpr std::array displayChoices {
    displayModes[0].choice,
    displayModes[1].choice,
    displayModes[2].choice,
    displayModes[3].choice,
    displayModes[4].choice,
};

constexpr std::array bindingDeviceChoices {
    OptionsMenuChoice { 0, "Keyboard" },
    OptionsMenuChoice { 1, "Controller" },
};

int bindingDeviceChoice(BindingDeviceClass deviceClass)
{
    return deviceClass == BindingDeviceClass::Keyboard ? 0 : 1;
}

BindingDeviceClass bindingDeviceFromChoice(int value)
{
    return value == 1
        ? BindingDeviceClass::Gamepad
        : BindingDeviceClass::Keyboard;
}

struct BindingRow {
    OptionsMenuRowId row;
    InputAction action;
    std::string_view label;
};

constexpr std::array bindingRows {
    BindingRow { OptionsMenuRowId::MoveUp, InputAction::MoveUp, "Move up" },
    BindingRow { OptionsMenuRowId::MoveDown, InputAction::MoveDown, "Move down" },
    BindingRow { OptionsMenuRowId::MoveLeft, InputAction::MoveLeft, "Move left" },
    BindingRow { OptionsMenuRowId::MoveRight, InputAction::MoveRight, "Move right" },
    BindingRow { OptionsMenuRowId::Undo, InputAction::Undo, "Undo" },
    BindingRow { OptionsMenuRowId::Restart, InputAction::Restart, "Restart" },
    BindingRow {
        OptionsMenuRowId::ShowTopDownView,
        InputAction::ShowTopDownView,
        "Show Top-Down View",
    },
    BindingRow {
        OptionsMenuRowId::ConfirmInteract,
        InputAction::MenuConfirm,
        "Confirm / Interact",
    },
    BindingRow {
        OptionsMenuRowId::PreviewScreen,
        InputAction::PreviewScreen,
        "Preview screen",
    },
};

constexpr std::array editorBindingRows {
    BindingRow {
        OptionsMenuRowId::EditorReplaceTile,
        InputAction::EditorReplaceTile,
        "Replace tile",
    },
    BindingRow {
        OptionsMenuRowId::EditorDeleteTile,
        InputAction::EditorDeleteTile,
        "Delete tile",
    },
    BindingRow {
        OptionsMenuRowId::EditorMoveTile,
        InputAction::EditorMoveTile,
        "Move tile object",
    },
};

int displayIndex(const UserSettings& settings)
{
    if (settings.video.fullscreen) {
        return 0;
    }
    for (std::size_t index = 1; index < displayModes.size(); ++index) {
        if (displayModes[index].width == settings.video.windowWidth &&
            displayModes[index].height == settings.video.windowHeight) {
            return static_cast<int>(index);
        }
    }
    return 3;
}

std::optional<InputAction> actionForRow(OptionsMenuRowId row)
{
    const auto found = std::ranges::find(bindingRows, row, &BindingRow::row);
    if (found != bindingRows.end()) {
        return found->action;
    }
    const auto editorFound =
        std::ranges::find(editorBindingRows, row, &BindingRow::row);
    return editorFound == editorBindingRows.end()
        ? std::nullopt
        : std::optional<InputAction>(editorFound->action);
}

void setPage(OptionsMenuState& state, OptionsMenuPage page)
{
    state.page = page;
    state.selectedRow = 0;
    state.capturingAction.reset();
    state.customRenderScalePreview.reset();
}

void normalizeSelection(OptionsMenuState& state, const UserSettings& settings)
{
    const int count = static_cast<int>(
        optionsMenuRows(state, settings).size());
    if (count == 0) {
        state.selectedRow = 0;
        return;
    }
    state.selectedRow = std::clamp(state.selectedRow, 0, count - 1);
}

std::optional<OptionsMenuRowId> selectedRowId(
    const OptionsMenuState& state,
    const UserSettings& settings)
{
    const std::vector<OptionsMenuRow> rows =
        optionsMenuRows(state, settings);
    if (state.selectedRow < 0 ||
        state.selectedRow >= static_cast<int>(rows.size())) {
        return std::nullopt;
    }
    return rows[static_cast<std::size_t>(state.selectedRow)].id;
}

std::optional<OptionsAction> changedSettings(
    UserSettings settings,
    UserSettings current)
{
    settings.normalize();
    current.normalize();
    if (settings == current) {
        return std::nullopt;
    }
    return options::SettingsChanged { std::move(settings) };
}

void applyDisplayMode(UserSettings& settings, int index)
{
    const DisplayMode& mode = displayModes[static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(displayModes.size()) - 1))];
    settings.video.fullscreen = mode.fullscreen;
    if (!mode.fullscreen) {
        settings.video.windowWidth = mode.width;
        settings.video.windowHeight = mode.height;
    }
}

int cycleChoice(
    std::span<const OptionsMenuChoice> choices,
    int current,
    int direction)
{
    auto found = std::ranges::find(choices, current, &OptionsMenuChoice::value);
    int index = found == choices.end()
        ? 0
        : static_cast<int>(std::distance(choices.begin(), found));
    index = (index + (direction < 0 ? -1 : 1) +
        static_cast<int>(choices.size())) %
        static_cast<int>(choices.size());
    return choices[static_cast<std::size_t>(index)].value;
}

std::optional<OptionsAction> activateRow(
    OptionsMenuState& state,
    const UserSettings& current,
    OptionsMenuRowId row)
{
    UserSettings settings = current;
    switch (row) {
    case OptionsMenuRowId::Graphics:
        setPage(state, OptionsMenuPage::Graphics);
        break;
    case OptionsMenuRowId::Audio:
        setPage(state, OptionsMenuPage::Audio);
        break;
    case OptionsMenuRowId::Controls:
        setPage(state, OptionsMenuPage::Controls);
        break;
    case OptionsMenuRowId::EditorControls:
        setPage(state, OptionsMenuPage::EditorControls);
        break;
    case OptionsMenuRowId::LevelSelect:
        return options::OpenLevelSelect {};
    case OptionsMenuRowId::ExitToTitle:
        return options::ExitToTitle {};
    case OptionsMenuRowId::Quit:
        setPage(state, OptionsMenuPage::QuitConfirmation);
        break;
    case OptionsMenuRowId::CustomRenderScale:
        settings.video.customRenderScale =
            !settings.video.customRenderScale;
        state.customRenderScalePreview.reset();
        return changedSettings(std::move(settings), current);
    case OptionsMenuRowId::AmbientOcclusion:
        settings.video.ambientOcclusion =
            !settings.video.ambientOcclusion;
        return changedSettings(std::move(settings), current);
    case OptionsMenuRowId::MoveUp:
    case OptionsMenuRowId::MoveDown:
    case OptionsMenuRowId::MoveLeft:
    case OptionsMenuRowId::MoveRight:
    case OptionsMenuRowId::Undo:
    case OptionsMenuRowId::Restart:
    case OptionsMenuRowId::ShowTopDownView:
    case OptionsMenuRowId::ConfirmInteract:
    case OptionsMenuRowId::PreviewScreen:
    case OptionsMenuRowId::EditorReplaceTile:
    case OptionsMenuRowId::EditorDeleteTile:
    case OptionsMenuRowId::EditorMoveTile:
        state.capturingAction = actionForRow(row);
        break;
    case OptionsMenuRowId::ResetBindings:
        if (!(settings.input == defaultInputBindings())) {
            settings.input = defaultInputBindings();
            return changedSettings(std::move(settings), current);
        }
        break;
    case OptionsMenuRowId::Back:
        setPage(
            state,
            state.page == OptionsMenuPage::EditorControls
                ? OptionsMenuPage::Controls
                : OptionsMenuPage::Main);
        break;
    case OptionsMenuRowId::CancelQuit:
        setPage(state, OptionsMenuPage::Main);
        break;
    case OptionsMenuRowId::ConfirmQuit:
        return options::Quit {};
    case OptionsMenuRowId::AntiAliasing:
    case OptionsMenuRowId::RenderScalePreset:
    case OptionsMenuRowId::AmbientOcclusionStrength:
    case OptionsMenuRowId::Display:
    case OptionsMenuRowId::MasterVolume:
    case OptionsMenuRowId::MusicVolume:
    case OptionsMenuRowId::BindingDevice:
        break;
    }
    return std::nullopt;
}

std::optional<OptionsAction> adjustRow(
    OptionsMenuState& state,
    const UserSettings& current,
    OptionsMenuRowId row,
    int direction)
{
    if (direction == 0) {
        return std::nullopt;
    }
    UserSettings settings = current;
    switch (row) {
    case OptionsMenuRowId::AntiAliasing:
        settings.video.antiAliasingSamples = cycleChoice(
            sampleCountChoices,
            settings.video.antiAliasingSamples,
            direction);
        break;
    case OptionsMenuRowId::RenderScalePreset:
        settings.video.renderScalePercent = cycleChoice(
            renderScaleChoices,
            settings.video.renderScalePercent,
            direction);
        settings.video.customRenderScale = false;
        state.customRenderScalePreview.reset();
        break;
    case OptionsMenuRowId::CustomRenderScale:
        if (!settings.video.customRenderScale) {
            return std::nullopt;
        }
        settings.video.customRenderScalePercent += direction < 0 ? -1 : 1;
        break;
    case OptionsMenuRowId::Display:
        applyDisplayMode(
            settings,
            cycleChoice(displayChoices, displayIndex(settings), direction));
        break;
    case OptionsMenuRowId::MasterVolume:
        settings.audio.masterVolume += direction < 0 ? -0.05f : 0.05f;
        break;
    case OptionsMenuRowId::MusicVolume:
        settings.audio.musicVolume += direction < 0 ? -0.05f : 0.05f;
        break;
    case OptionsMenuRowId::AmbientOcclusionStrength:
        if (!settings.video.ambientOcclusion) {
            return std::nullopt;
        }
        settings.video.ambientOcclusionStrength +=
            direction < 0 ? -0.05f : 0.05f;
        break;
    case OptionsMenuRowId::BindingDevice:
        state.controlsBindingDevice = bindingDeviceFromChoice(
            cycleChoice(
                bindingDeviceChoices,
                bindingDeviceChoice(state.controlsBindingDevice),
                direction));
        return std::nullopt;
    default:
        return std::nullopt;
    }
    return changedSettings(std::move(settings), current);
}

std::string_view pageTitle(OptionsMenuPage page)
{
    switch (page) {
    case OptionsMenuPage::Main: return "OPTIONS";
    case OptionsMenuPage::Graphics: return "GRAPHICS";
    case OptionsMenuPage::Audio: return "AUDIO";
    case OptionsMenuPage::Controls: return "CONTROLS";
    case OptionsMenuPage::EditorControls: return "EDITOR CONTROLS";
    case OptionsMenuPage::QuitConfirmation: return "QUIT GAME";
    }
    return "OPTIONS";
}

float pageHeight(OptionsMenuPage page)
{
    switch (page) {
    case OptionsMenuPage::Graphics: return 740.0f;
    case OptionsMenuPage::Controls: return 720.0f;
    case OptionsMenuPage::EditorControls: return 540.0f;
    default: return 540.0f;
    }
}

uiControls::ButtonTone buttonTone(OptionsMenuRowTone tone)
{
    switch (tone) {
    case OptionsMenuRowTone::Accent:
        return uiControls::ButtonTone::Accent;
    case OptionsMenuRowTone::Danger:
        return uiControls::ButtonTone::Danger;
    case OptionsMenuRowTone::Normal:
        return uiControls::ButtonTone::Normal;
    }
    return uiControls::ButtonTone::Normal;
}

std::string rowControlId(OptionsMenuRowId row)
{
    return "options.row." + std::to_string(static_cast<int>(row));
}

struct RowLayout {
    UiLayoutNode divider {};
    UiLayoutNode primary {};
    UiLayoutNode control {};
    UiLayoutNode detail {};
};

bool hasNode(UiLayoutNode node)
{
    return node != 0;
}

float fittedTextSize(
    const UiContext& ui,
    std::string_view text,
    float preferredSize,
    float availableWidth)
{
    const float measuredWidth = ui.measureText(text, preferredSize).x;
    if (measuredWidth <= availableWidth || measuredWidth <= 0.0f) {
        return preferredSize;
    }
    return preferredSize * availableWidth / measuredWidth;
}

void drawBindingRowText(
    UiContext& ui,
    UiRect row,
    std::string_view label,
    std::string_view binding,
    Vec4 bindingColor)
{
    constexpr float horizontalPadding = 16.0f;
    constexpr float columnGap = 12.0f;
    const float contentWidth = std::max(
        row.size.x - horizontalPadding * 2.0f - columnGap,
        0.0f);
    const float labelWidth = contentWidth * 0.43f;
    const float bindingWidth = contentWidth - labelWidth;
    const UiRect labelRect {
        { row.position.x + horizontalPadding, row.position.y },
        { labelWidth, row.size.y },
    };
    const UiRect bindingRect {
        { labelRect.position.x + labelWidth + columnGap, row.position.y },
        { bindingWidth, row.size.y },
    };

    const float labelSize = fittedTextSize(ui, label, 20.0f, labelRect.size.x);
    const Vec2 labelMeasure = ui.measureText(label, labelSize);
    ui.text({
        labelRect.position.x,
        labelRect.position.y + (labelRect.size.y - labelMeasure.y) * 0.5f,
    }, label, { 0.92f, 0.94f, 0.92f, 1.0f }, labelSize);

    const float bindingSize = fittedTextSize(
        ui, binding, 16.0f, bindingRect.size.x);
    const Vec2 bindingMeasure = ui.measureText(binding, bindingSize);
    ui.text({
        bindingRect.position.x + bindingRect.size.x - bindingMeasure.x,
        bindingRect.position.y + (bindingRect.size.y - bindingMeasure.y) * 0.5f,
    }, binding, bindingColor, bindingSize);
}

bool drawBindingRowPrompts(
    UiContext& ui,
    UiRect row,
    std::string_view label,
    const InputBindings& bindings,
    InputAction action,
    BindingDeviceClass device,
    const InputPromptCatalog& prompts,
    const GamepadPresentation& gamepad,
    bool focused)
{
    std::vector<InputPromptGlyph> glyphs;
    for (const InputBinding& binding : bindings.forAction(action)) {
        if (bindingDeviceClass(binding) != device) continue;
        const std::optional<InputPromptGlyph> glyph =
            prompts.glyphForBinding(binding, gamepad);
        if (!glyph) return false;
        glyphs.push_back(*glyph);
    }
    if (glyphs.empty()) return false;

    constexpr float horizontalPadding = 16.0f;
    constexpr float columnGap = 12.0f;
    const float contentWidth = std::max(
        row.size.x - horizontalPadding * 2.0f - columnGap,
        0.0f);
    const float labelWidth = contentWidth * 0.43f;
    const UiRect labelRect {
        { row.position.x + horizontalPadding, row.position.y },
        { labelWidth, row.size.y },
    };
    const float labelSize = fittedTextSize(ui, label, 20.0f, labelRect.size.x);
    const Vec2 labelMeasure = ui.measureText(label, labelSize);
    ui.text({
        labelRect.position.x,
        labelRect.position.y + (labelRect.size.y - labelMeasure.y) * 0.5f,
    }, label, { 0.92f, 0.94f, 0.92f, 1.0f }, labelSize);

    constexpr float glyphSize = 36.0f;
    constexpr float glyphGap = 5.0f;
    const float totalWidth = glyphs.size() * glyphSize +
        (glyphs.size() - 1) * glyphGap;
    float x = row.position.x + row.size.x - horizontalPadding - totalWidth;
    const float y = row.position.y + (row.size.y - glyphSize) * 0.5f;
    const Vec4 color = focused
        ? Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }
        : Vec4 { 0.86f, 0.89f, 0.88f, 0.9f };
    for (const InputPromptGlyph& glyph : glyphs) {
        drawInputPromptGlyph(ui, { { x, y }, { glyphSize, glyphSize } }, glyph, color);
        x += glyphSize + glyphGap;
    }
    return true;
}

} // namespace

std::vector<OptionsMenuRow> optionsMenuRows(
    const OptionsMenuState& state,
    const UserSettings& settings)
{
    std::vector<OptionsMenuRow> rows;
    switch (state.page) {
    case OptionsMenuPage::Main:
        rows.push_back({
            .id = OptionsMenuRowId::Graphics,
            .kind = OptionsMenuRowKind::Button,
            .label = "Graphics",
            .tone = OptionsMenuRowTone::Accent,
        });
        rows.push_back({
            .id = OptionsMenuRowId::Audio,
            .kind = OptionsMenuRowKind::Button,
            .label = "Audio",
        });
        rows.push_back({
            .id = OptionsMenuRowId::Controls,
            .kind = OptionsMenuRowKind::Button,
            .label = "Controls",
        });
        if (state.allowTitleExit) {
            rows.push_back({
                .id = OptionsMenuRowId::ExitToTitle,
                .kind = OptionsMenuRowKind::Button,
                .label = "Exit To Title",
            });
        }
        rows.push_back({
            .id = OptionsMenuRowId::Quit,
            .kind = OptionsMenuRowKind::Button,
            .label = "Quit Game",
            .tone = OptionsMenuRowTone::Danger,
            .flexibleSpaceBefore = true,
            .dividerBefore = true,
        });
        break;
    case OptionsMenuPage::Graphics:
        rows = {
            {
                .id = OptionsMenuRowId::AntiAliasing,
                .kind = OptionsMenuRowKind::SegmentedChoice,
                .label = "Anti-aliasing",
                .choices = sampleCountChoices,
                .choiceValue = settings.video.antiAliasingSamples,
            },
            {
                .id = OptionsMenuRowId::RenderScalePreset,
                .kind = OptionsMenuRowKind::SegmentedChoice,
                .label = "Render scale",
                .choices = renderScaleChoices,
                .choiceValue = settings.video.renderScalePercent,
            },
            {
                .id = OptionsMenuRowId::CustomRenderScale,
                .kind = OptionsMenuRowKind::CustomRenderScale,
                .label = "Custom",
                .sliderValue = static_cast<float>(
                    state.customRenderScalePreview.value_or(
                        settings.video.customRenderScalePercent)) / 100.0f,
                .toggleValue = settings.video.customRenderScale,
                .enabled = settings.video.customRenderScale,
            },
            {
                .id = OptionsMenuRowId::AmbientOcclusion,
                .kind = OptionsMenuRowKind::Toggle,
                .label = "Ambient occlusion",
                .toggleValue = settings.video.ambientOcclusion,
            },
            {
                .id = OptionsMenuRowId::AmbientOcclusionStrength,
                .kind = OptionsMenuRowKind::Slider,
                .label = "AO strength",
                .sliderValue =
                    settings.video.ambientOcclusionStrength,
                .enabled = settings.video.ambientOcclusion,
            },
            {
                .id = OptionsMenuRowId::Display,
                .kind = OptionsMenuRowKind::StepperChoice,
                .label = "Display",
                .choices = displayChoices,
                .choiceValue = displayIndex(settings),
            },
            {
                .id = OptionsMenuRowId::Back,
                .kind = OptionsMenuRowKind::Button,
                .label = "Back",
                .flexibleSpaceBefore = true,
            },
        };
        break;
    case OptionsMenuPage::Audio:
        rows = {
            {
                .id = OptionsMenuRowId::MasterVolume,
                .kind = OptionsMenuRowKind::Slider,
                .label = "Master volume",
                .sliderValue = settings.audio.masterVolume,
            },
            {
                .id = OptionsMenuRowId::MusicVolume,
                .kind = OptionsMenuRowKind::Slider,
                .label = "Music volume",
                .sliderValue = settings.audio.musicVolume,
            },
            {
                .id = OptionsMenuRowId::Back,
                .kind = OptionsMenuRowKind::Button,
                .label = "Back",
                .flexibleSpaceBefore = true,
            },
        };
        break;
    case OptionsMenuPage::Controls:
        rows.push_back({
            .id = OptionsMenuRowId::BindingDevice,
            .kind = OptionsMenuRowKind::Tabs,
            .choices = bindingDeviceChoices,
            .choiceValue = bindingDeviceChoice(
                state.controlsBindingDevice),
        });
        for (const BindingRow& binding : bindingRows) {
            rows.push_back({
                .id = binding.row,
                .kind = OptionsMenuRowKind::Binding,
                .label = binding.label,
                .tone = state.capturingAction == binding.action
                    ? OptionsMenuRowTone::Accent
                    : OptionsMenuRowTone::Normal,
            });
        }
        rows.push_back({
            .id = OptionsMenuRowId::ResetBindings,
            .kind = OptionsMenuRowKind::Button,
            .label = "Reset To Defaults",
            .flexibleSpaceBefore = true,
        });
#if SOKOBAN_ENABLE_DEBUG_UI
        if (state.controlsBindingDevice ==
            BindingDeviceClass::Keyboard) {
            rows.push_back({
                .id = OptionsMenuRowId::EditorControls,
                .kind = OptionsMenuRowKind::Button,
                .label = "Editor Controls",
            });
        }
#endif
        rows.push_back({
            .id = OptionsMenuRowId::Back,
            .kind = OptionsMenuRowKind::Button,
            .label = "Back",
        });
        break;
    case OptionsMenuPage::EditorControls:
        for (const BindingRow& binding : editorBindingRows) {
            rows.push_back({
                .id = binding.row,
                .kind = OptionsMenuRowKind::Binding,
                .label = binding.label,
                .tone = state.capturingAction == binding.action
                    ? OptionsMenuRowTone::Accent
                    : OptionsMenuRowTone::Normal,
            });
        }
        rows.push_back({
            .id = OptionsMenuRowId::Back,
            .kind = OptionsMenuRowKind::Button,
            .label = "Back",
            .flexibleSpaceBefore = true,
        });
        break;
    case OptionsMenuPage::QuitConfirmation:
        rows = {
            {
                .id = OptionsMenuRowId::CancelQuit,
                .kind = OptionsMenuRowKind::Button,
                .label = "Cancel",
            },
            {
                .id = OptionsMenuRowId::ConfirmQuit,
                .kind = OptionsMenuRowKind::Button,
                .label = "Quit",
                .tone = OptionsMenuRowTone::Danger,
            },
        };
        break;
    }
    return rows;
}

OptionsMenuReduction reduceOptionsMenu(
    const OptionsMenuState& currentState,
    const UserSettings& currentSettings,
    const OptionsMenuIntent& intent)
{
    OptionsMenuReduction result { .state = currentState };
    std::visit(flow::Overloaded {
        [&](const options::intent::Open& open) {
            result.state.open = true;
            result.state.allowTitleExit = open.allowTitleExit;
            result.state.allowLevelSelect =
                open.allowLevelSelect && open.allowTitleExit;
            setPage(result.state, OptionsMenuPage::Main);
        },
        [&](const options::intent::Close&) {
            result.state.open = false;
            setPage(result.state, OptionsMenuPage::Main);
        },
        [&](const options::intent::Back&) {
            if (!result.state.open) {
                return;
            }
            if (result.state.capturingAction) {
                result.state.capturingAction.reset();
            } else if (result.state.page == OptionsMenuPage::Main) {
                result.state.open = false;
            } else if (result.state.page == OptionsMenuPage::EditorControls) {
                setPage(result.state, OptionsMenuPage::Controls);
            } else {
                setPage(result.state, OptionsMenuPage::Main);
            }
        },
        [&](const options::intent::RequestQuitConfirmation&) {
            result.state.open = true;
            setPage(result.state, OptionsMenuPage::QuitConfirmation);
        },
        [&](const options::intent::Navigate& navigation) {
            if (result.state.capturingAction || navigation.direction == 0) {
                return;
            }
            const int count = static_cast<int>(
                optionsMenuRows(result.state, currentSettings).size());
            if (count != 0) {
                result.state.selectedRow =
                    (result.state.selectedRow +
                        (navigation.direction < 0 ? -1 : 1) + count) %
                    count;
            }
        },
        [&](const options::intent::AdjustSelected& adjustment) {
            if (result.state.capturingAction) {
                return;
            }
            const std::optional<OptionsMenuRowId> row =
                selectedRowId(result.state, currentSettings);
            if (row) {
                result.action = adjustRow(
                    result.state,
                    currentSettings,
                    *row,
                    adjustment.direction);
            }
        },
        [&](const options::intent::ActivateSelected&) {
            if (result.state.capturingAction) {
                return;
            }
            const std::optional<OptionsMenuRowId> row =
                selectedRowId(result.state, currentSettings);
            if (row) {
                result.action = activateRow(
                    result.state, currentSettings, *row);
            }
        },
        [&](const options::intent::ActivateRow& activation) {
            result.action = activateRow(
                result.state, currentSettings, activation.row);
        },
        [&](const options::intent::SelectChoice& selection) {
            UserSettings settings = currentSettings;
            switch (selection.row) {
            case OptionsMenuRowId::AntiAliasing:
                settings.video.antiAliasingSamples = selection.value;
                break;
            case OptionsMenuRowId::RenderScalePreset:
                settings.video.renderScalePercent = selection.value;
                settings.video.customRenderScale = false;
                result.state.customRenderScalePreview.reset();
                break;
            case OptionsMenuRowId::Display:
                applyDisplayMode(settings, selection.value);
                break;
            case OptionsMenuRowId::BindingDevice:
                result.state.controlsBindingDevice =
                    bindingDeviceFromChoice(selection.value);
                return;
            default:
                return;
            }
            result.action = changedSettings(
                std::move(settings), currentSettings);
        },
        [&](const options::intent::SetToggle& toggle) {
            UserSettings settings = currentSettings;
            switch (toggle.row) {
            case OptionsMenuRowId::CustomRenderScale:
                settings.video.customRenderScale = toggle.value;
                result.state.customRenderScalePreview.reset();
                break;
            case OptionsMenuRowId::AmbientOcclusion:
                settings.video.ambientOcclusion = toggle.value;
                break;
            default:
                return;
            }
            result.action = changedSettings(
                std::move(settings), currentSettings);
        },
        [&](const options::intent::SetSlider& slider) {
            UserSettings settings = currentSettings;
            switch (slider.row) {
            case OptionsMenuRowId::CustomRenderScale: {
                const int percent = normalizedRenderScalePercent(
                    static_cast<int>(std::round(slider.value * 100.0f)));
                if (!slider.commit) {
                    result.state.customRenderScalePreview = percent;
                    return;
                }
                settings.video.customRenderScalePercent = percent;
                result.state.customRenderScalePreview.reset();
                break;
            }
            case OptionsMenuRowId::MasterVolume:
                settings.audio.masterVolume = slider.value;
                break;
            case OptionsMenuRowId::MusicVolume:
                settings.audio.musicVolume = slider.value;
                break;
            case OptionsMenuRowId::AmbientOcclusionStrength:
                if (!settings.video.ambientOcclusion) {
                    return;
                }
                settings.video.ambientOcclusionStrength = slider.value;
                break;
            default:
                return;
            }
            result.action = changedSettings(
                std::move(settings), currentSettings);
        },
        [&](const options::intent::ProvideBinding& provided) {
            if (!result.state.capturingAction) {
                return;
            }
            if (const auto* key =
                    std::get_if<KeyboardBinding>(&provided.binding);
                key != nullptr && key->scancode == "Escape") {
                return;
            }
            if (const auto* button =
                    std::get_if<GamepadButtonBinding>(&provided.binding);
                button != nullptr && button->button == "start") {
                result.state.capturingAction.reset();
                return;
            }
            if (bindingDeviceClass(provided.binding) !=
                result.state.controlsBindingDevice) {
                return;
            }
            UserSettings settings = currentSettings;
            assignBinding(
                settings.input,
                *result.state.capturingAction,
                provided.binding);
            result.state.capturingAction.reset();
            result.action = changedSettings(
                std::move(settings), currentSettings);
        },
    }, intent);
    normalizeSelection(result.state, currentSettings);
    return result;
}

void OptionsMenu::open(bool allowTitleExit, bool allowLevelSelect)
{
    (void)dispatch({}, options::intent::Open {
        .allowTitleExit = allowTitleExit,
        .allowLevelSelect = allowLevelSelect,
    });
}

void OptionsMenu::close()
{
    (void)dispatch({}, options::intent::Close {});
}

void OptionsMenu::back()
{
    (void)dispatch({}, options::intent::Back {});
}

void OptionsMenu::requestQuitConfirmation()
{
    (void)dispatch(
        {}, options::intent::RequestQuitConfirmation {});
}

std::optional<OptionsAction> OptionsMenu::handleInput(
    const UserSettings& settings,
    const OptionsMenuInput& input)
{
    UserSettings current = settings;
    std::optional<OptionsAction> action;
    auto apply = [&](OptionsMenuIntent intent) {
        if (const std::optional<OptionsAction> next =
                dispatch(current, intent)) {
            action = next;
            if (const auto* changed =
                    std::get_if<options::SettingsChanged>(&*next)) {
                current = changed->settings;
            }
        }
    };
    if (input.up) {
        apply(options::intent::Navigate { -1 });
    }
    if (input.down) {
        apply(options::intent::Navigate { 1 });
    }
    if (input.left) {
        apply(options::intent::AdjustSelected { -1 });
    }
    if (input.right) {
        apply(options::intent::AdjustSelected { 1 });
    }
    if (input.confirm) {
        apply(options::intent::ActivateSelected {});
    }
    return action;
}

std::optional<OptionsAction> OptionsMenu::dispatch(
    const UserSettings& settings,
    const OptionsMenuIntent& intent)
{
    OptionsMenuReduction reduction =
        reduceOptionsMenu(state_, settings, intent);
    state_ = std::move(reduction.state);
    return std::move(reduction.action);
}

std::optional<OptionsAction> OptionsMenu::provideBindingCandidate(
    const UserSettings& settings,
    const InputBinding& candidate)
{
    return dispatch(
        settings,
        options::intent::ProvideBinding { candidate });
}

std::optional<OptionsMenuIntent> OptionsMenuView::draw(
    UiContext& ui,
    Vec2 viewport,
    const OptionsMenuState& state,
    const UserSettings& settings,
    const InputPromptCatalog* inputPrompts,
    const GamepadPresentation* gamepad) const
{
    if (!state.open) {
        return std::nullopt;
    }

    ui.rect(
        { { 0.0f, 0.0f }, viewport },
        { 0.015f, 0.020f, 0.021f, 0.78f });
    const UiRect panel = menuKit::centeredPanel(
        viewport, 560.0f, pageHeight(state.page), 400.0f);
    ui.panel(panel);

    const std::vector<OptionsMenuRow> rows =
        optionsMenuRows(state, settings);
    const bool compactGraphics =
        state.page == OptionsMenuPage::Graphics;
    menuKit::MenuPage layout(
        state.page == OptionsMenuPage::Controls
            ? 16.0f
            : (compactGraphics ? 16.0f : 28.0f));
    std::vector<RowLayout> rowLayouts(rows.size());
    UiLayoutNode message {};
    UiLayoutNode controlsPrompt {};
    if (state.page == OptionsMenuPage::QuitConfirmation) {
        layout.tree.spacer(layout.tree.root(), 20.0f);
        message = layout.tree.item(layout.tree.root(), 44.0f);
        layout.tree.spacer(layout.tree.root(), 74.0f);
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const OptionsMenuRow& row = rows[index];
        RowLayout& rowLayout = rowLayouts[index];
        if (row.flexibleSpaceBefore) {
            if (state.page == OptionsMenuPage::Controls &&
                row.id == OptionsMenuRowId::ResetBindings) {
                controlsPrompt = layout.tree.item(
                    layout.tree.root(), 22.0f);
                layout.tree.spacer(layout.tree.root(), 4.0f);
            }
            layout.tree.flexibleSpacer(layout.tree.root());
        }
        if (row.dividerBefore) {
            rowLayout.divider = layout.tree.item(
                layout.tree.root(), 1.0f);
            layout.tree.spacer(layout.tree.root(), 20.0f);
        }
        switch (row.kind) {
        case OptionsMenuRowKind::Tabs:
            rowLayout.primary = layout.tree.item(
                layout.tree.root(), 44.0f);
            break;
        case OptionsMenuRowKind::SegmentedChoice:
        case OptionsMenuRowKind::StepperChoice:
        case OptionsMenuRowKind::Slider: {
            const UiLayoutNode group = layout.tree.column(
                layout.tree.root(), UiLayoutSize::content(),
                row.kind == OptionsMenuRowKind::Slider
                    ? (compactGraphics ? 4.0f : 8.0f)
                    : 4.0f);
            rowLayout.primary = layout.tree.item(
                group, compactGraphics ? 24.0f : 30.0f);
            rowLayout.control = layout.tree.item(
                group,
                row.kind == OptionsMenuRowKind::Slider
                    ? (compactGraphics ? 28.0f : 34.0f)
                    : (compactGraphics ? 44.0f : 52.0f));
            break;
        }
        case OptionsMenuRowKind::CustomRenderScale: {
            const UiLayoutNode group = layout.tree.column(
                layout.tree.root(), UiLayoutSize::content(), 4.0f);
            rowLayout.primary = layout.tree.item(
                group, compactGraphics ? 40.0f : 44.0f);
            rowLayout.control = layout.tree.item(
                group, compactGraphics ? 28.0f : 32.0f);
            rowLayout.detail = layout.tree.item(
                group, compactGraphics ? 20.0f : 24.0f);
            break;
        }
        case OptionsMenuRowKind::Button:
        case OptionsMenuRowKind::Toggle:
        case OptionsMenuRowKind::Binding:
            rowLayout.primary = layout.tree.item(
                layout.tree.root(),
                state.page == OptionsMenuPage::Controls
                    ? 38.0f
                    : (compactGraphics ? 44.0f : 52.0f));
            break;
        }
        if (index + 1 < rows.size() &&
            !rows[index + 1].flexibleSpaceBefore) {
            layout.tree.spacer(
                layout.tree.root(),
                state.page == OptionsMenuPage::Main
                    ? 16.0f
                    : (state.page == OptionsMenuPage::Controls
                            ? 3.0f
                            : (compactGraphics ? 6.0f : 10.0f)));
        }
    }
    layout.tree.arrange(panel);
    layout.drawHeader(ui, pageTitle(state.page), 36.0f);

    if (state.page == OptionsMenuPage::QuitConfirmation) {
        ui.centeredText(
            layout.tree.rect(message),
            "Are you sure you want to quit?",
            { 0.83f, 0.86f, 0.83f, 1.0f },
            22.0f);
    }
    if (state.page == OptionsMenuPage::Controls) {
        ui.centeredText(
            layout.tree.rect(controlsPrompt),
            state.capturingAction
                ? "Esc or Start cancels. Rebinding steals duplicates."
                : "Choose a tab, then confirm a row to remap it.",
            { 0.58f, 0.63f, 0.62f, 1.0f },
            17.0f);
    }

    std::optional<OptionsMenuIntent> intent;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const OptionsMenuRow& row = rows[index];
        const RowLayout& rowLayout = rowLayouts[index];
        const bool focused = state.selectedRow == static_cast<int>(index);
        const std::string controlId = rowControlId(row.id);
        if (hasNode(rowLayout.divider)) {
            ui.divider(layout.tree.rect(rowLayout.divider));
        }
        switch (row.kind) {
        case OptionsMenuRowKind::Tabs: {
            std::vector<uiControls::ChoiceOption> choices;
            choices.reserve(row.choices.size());
            for (const OptionsMenuChoice& choice : row.choices) {
                choices.push_back({ choice.value, choice.label });
            }
            int value = row.choiceValue;
            if (uiControls::segmentedControl(
                    ui,
                    layout.tree.rect(rowLayout.primary),
                    choices,
                    value,
                    { .focused = focused })) {
                intent = options::intent::SelectChoice {
                    row.id, value };
            }
            break;
        }
        case OptionsMenuRowKind::Button:
            if (uiControls::button(
                    ui,
                    layout.tree.rect(rowLayout.primary),
                    row.label,
                    {
                        .tone = buttonTone(row.tone),
                        .focused = focused,
                    })) {
                intent = options::intent::ActivateRow { row.id };
            }
            break;
        case OptionsMenuRowKind::Toggle: {
            bool value = row.toggleValue;
            if (uiControls::checkbox(
                    ui,
                    layout.tree.rect(rowLayout.primary),
                    row.label,
                    value,
                    focused)) {
                intent = options::intent::SetToggle { row.id, value };
            }
            break;
        }
        case OptionsMenuRowKind::SegmentedChoice: {
            ui.text(
                layout.tree.rect(rowLayout.primary).position,
                row.label,
                { 0.83f, 0.86f, 0.83f, 1.0f },
                22.0f);
            std::vector<uiControls::ChoiceOption> choices;
            choices.reserve(row.choices.size());
            for (const OptionsMenuChoice& choice : row.choices) {
                choices.push_back({ choice.value, choice.label });
            }
            int value = row.choiceValue;
            if (uiControls::segmentedControl(
                    ui,
                    layout.tree.rect(rowLayout.control),
                    choices,
                    value,
                    { .focused = focused })) {
                intent = options::intent::SelectChoice {
                    row.id, value };
            }
            break;
        }
        case OptionsMenuRowKind::StepperChoice: {
            ui.text(
                layout.tree.rect(rowLayout.primary).position,
                row.label,
                { 0.83f, 0.86f, 0.83f, 1.0f },
                22.0f);
            std::vector<std::string_view> labels;
            labels.reserve(row.choices.size());
            for (const OptionsMenuChoice& choice : row.choices) {
                labels.push_back(choice.label);
            }
            int value = row.choiceValue;
            if (uiControls::choiceStepper(
                    ui,
                    layout.tree.rect(rowLayout.control),
                    labels,
                    value,
                    focused)) {
                intent = options::intent::SelectChoice {
                    row.id, value };
            }
            break;
        }
        case OptionsMenuRowKind::Slider: {
            const Vec4 labelColor = row.enabled
                ? Vec4 { 0.83f, 0.86f, 0.83f, 1.0f }
                : Vec4 { 0.50f, 0.52f, 0.51f, 0.55f };
            ui.text(
                layout.tree.rect(rowLayout.primary).position,
                row.label,
                labelColor,
                22.0f);
            float value = row.sliderValue;
            if (uiControls::slider(
                    ui,
                    controlId,
                    layout.tree.rect(rowLayout.control),
                    value,
                    0.0f,
                    1.0f,
                    focused && row.enabled,
                    row.enabled)) {
                intent = options::intent::SetSlider {
                    row.id, value, true };
            }
            const std::string percent = std::to_string(
                static_cast<int>(std::round(row.sliderValue * 100.0f))) + "%";
            menuKit::trailingText(
                ui,
                layout.tree.rect(rowLayout.primary),
                percent,
                row.enabled
                    ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                    : Vec4 { 0.50f, 0.52f, 0.51f, 0.45f },
                20.0f);
            break;
        }
        case OptionsMenuRowKind::CustomRenderScale: {
            bool enabled = row.toggleValue;
            if (uiControls::checkbox(
                    ui,
                    layout.tree.rect(rowLayout.primary),
                    row.label,
                    enabled,
                    focused)) {
                intent = options::intent::SetToggle {
                    row.id, enabled };
            }
            float value = row.sliderValue;
            const bool sliderChanged = uiControls::slider(
                ui,
                controlId + ".slider",
                layout.tree.rect(rowLayout.control),
                value,
                0.25f,
                1.0f,
                focused && row.enabled,
                row.enabled);
            if (sliderChanged) {
                intent = options::intent::SetSlider {
                    row.id, value, !ui.mouseDown() };
            } else if (state.customRenderScalePreview &&
                !ui.mouseDown()) {
                intent = options::intent::SetSlider {
                    row.id,
                    static_cast<float>(
                        *state.customRenderScalePreview) / 100.0f,
                    true,
                };
            }
            const int percentValue = static_cast<int>(
                std::round(row.sliderValue * 100.0f));
            const std::string percent =
                std::to_string(percentValue) + "%";
            menuKit::trailingText(
                ui,
                layout.tree.rect(rowLayout.primary),
                percent,
                row.enabled
                    ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                    : Vec4 { 0.58f, 0.61f, 0.60f, 0.45f },
                20.0f);
            const int effectiveScale = row.enabled
                ? percentValue
                : settings.video.renderScalePercent;
            const PixelExtent internal = scaledRenderExtent({
                .width = static_cast<uint32_t>(std::max(viewport.x, 0.0f)),
                .height = static_cast<uint32_t>(std::max(viewport.y, 0.0f)),
            }, effectiveScale);
            const std::string resolution =
                std::to_string(internal.width) + " x " +
                std::to_string(internal.height) + " internal";
            ui.text(
                layout.tree.rect(rowLayout.detail).position,
                resolution,
                { 0.58f, 0.63f, 0.62f, 1.0f },
                18.0f);
            break;
        }
        case OptionsMenuRowKind::Binding: {
            const std::optional<InputAction> action =
                actionForRow(row.id);
            const UiRect bindingRow = layout.tree.rect(rowLayout.primary);
            if (uiControls::button(
                    ui,
                    bindingRow,
                    "",
                    {
                        .tone = buttonTone(row.tone),
                        .focused = focused,
                    })) {
                intent = options::intent::ActivateRow { row.id };
            }
            const bool capturing = action &&
                state.capturingAction == action;
            const GamepadPresentation noGamepad;
            const bool drewPrompts = !capturing && action && inputPrompts &&
                drawBindingRowPrompts(
                    ui,
                    bindingRow,
                    row.label,
                    settings.input,
                    *action,
                    state.controlsBindingDevice,
                    *inputPrompts,
                    gamepad ? *gamepad : noGamepad,
                    focused);
            if (!drewPrompts) {
                drawBindingRowText(
                    ui,
                    bindingRow,
                    row.label,
                    capturing
                        ? (state.controlsBindingDevice ==
                                  BindingDeviceClass::Keyboard
                                ? "Press a key..."
                                : "Press a controller input...")
                        : actionBindingsDisplay(
                              settings.input,
                              *action,
                              state.controlsBindingDevice),
                    capturing
                        ? Vec4 { 0.98f, 0.84f, 0.42f, 1.0f }
                        : (focused
                                ? Vec4 { 0.68f, 0.88f, 0.82f, 1.0f }
                                : Vec4 { 0.62f, 0.67f, 0.65f, 1.0f }));
            }
            break;
        }
        }
    }
    return intent;
}

} // namespace sokoban
