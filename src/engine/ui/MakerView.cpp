#include "engine/ui/MakerView.hpp"

#include "engine/EditorTilePalette.hpp"
#include "engine/ui/LecternDialog.hpp"
#include "engine/ui/MakerHelp.hpp"
#include "engine/ui/MakerTileGuide.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/UiColorValues.hpp"
#include "engine/ui/UiPropertyInspector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

constexpr float margin = 12.0f;
constexpr float compactToolbarHeight = 96.0f;
constexpr float wideToolbarHeight = 62.0f;
constexpr float wideToolbarWidth = 1370.0f;
constexpr float toolbarOverflowWidth = 640.0f;
constexpr float footerHeight = 34.0f;
constexpr float tileHeight = 32.0f;
constexpr float tileGap = 4.0f;
constexpr float groupHeight = 28.0f;
constexpr float groupGap = 9.0f;
constexpr float searchTop = 48.0f;
constexpr float searchHeight = 32.0f;
constexpr float listTop = searchTop + searchHeight + 8.0f;
constexpr float guideHeight = 157.0f;
constexpr float compactGuideHeight = 100.0f;
constexpr float areaRowHeight = 28.0f;
constexpr float areaRowGap = 6.0f;
constexpr float sidebarTabHeight = 27.0f;
constexpr float sidebarTabInset = 6.0f;

constexpr Vec4 background { 0.061f, 0.078f, 0.081f, 1.0f };
constexpr Vec4 chrome { 0.089f, 0.112f, 0.115f, 1.0f };
constexpr Vec4 divider { 0.22f, 0.30f, 0.29f, 1.0f };
constexpr Vec4 ink { 0.94f, 0.95f, 0.90f, 1.0f };
constexpr Vec4 mutedInk { 0.68f, 0.75f, 0.72f, 1.0f };
constexpr Vec4 accentInk { 0.54f, 0.86f, 0.77f, 1.0f };
constexpr Vec4 buttonInk { 0.92f, 0.94f, 0.92f, 1.0f };
constexpr Vec4 disabledButtonInk { 0.50f, 0.52f, 0.51f, 0.72f };
constexpr Vec4 errorInk { 1.0f, 0.66f, 0.57f, 1.0f };
constexpr Vec4 removedTileInk { 0.94f, 0.07f, 0.09f, 1.0f };
constexpr Vec4 addedTileInk { 0.05f, 0.33f, 0.98f, 1.0f };

constexpr UiControlId draftCancelId = 1;
constexpr UiControlId draftSubmitId = 2;
constexpr UiControlId widthDecreaseId = 3;
constexpr UiControlId widthIncreaseId = 4;
constexpr UiControlId heightDecreaseId = 5;
constexpr UiControlId heightIncreaseId = 6;

constexpr std::array basics {
    TileType::Ground, TileType::Wall, TileType::Rock, TileType::End,
};
constexpr std::array terrain {
    TileType::CliffWall, TileType::Ladder, TileType::Water, TileType::StreamUp,
    TileType::LilyPad, TileType::Decorative,
};
constexpr std::array characters {
    TileType::Rogue, TileType::Knight, TileType::Druid, TileType::Witch, TileType::Bard,
    TileType::Cleric, TileType::Ranger,
};
constexpr std::array overworldCharacters { TileType::Player };
constexpr std::array overworldTiles {
    TileType::LecternNorth, TileType::WardrobeRogue,
    TileType::WardrobeKnight, TileType::WardrobeDruid, TileType::WardrobeWitch,
    TileType::WardrobeBard, TileType::WardrobeCleric, TileType::WardrobeRanger,
    TileType::WardrobeCrystalDruid,
};
constexpr std::array objects {
    TileType::RoundRock, TileType::Crystal, TileType::SkippingCrystal, TileType::Ice,
    TileType::MirrorNorthWest, TileType::RobotNorth, TileType::Enemy,
};
constexpr std::array mechanisms {
    TileType::PressurePlate, TileType::Gate, TileType::EnemyGate, TileType::LockPlate,
    TileType::ButtonNorth, TileType::LeverNorth, TileType::ConveyorUp,
    TileType::RotatorClockwise, TileType::RotatorCounterClockwise, TileType::Elevator,
    TileType::PortalNorth, TileType::TurretNorth,
};
constexpr std::array rail {
    TileType::RailStraightNorthSouth, TileType::RailCornerNorthEast, TileType::RailStopNorthSouth,
    TileType::Minecart, TileType::MinecartGate,
};

struct TileGroup {
    std::string_view label;
    std::span<const TileType> tiles;
};

constexpr std::array groups {
    TileGroup { "Basics", basics },
    TileGroup { "Terrain", terrain },
    TileGroup { "Characters", characters },
    TileGroup { "Overworld", overworldTiles },
    TileGroup { "Objects", objects },
    TileGroup { "Mechanisms", mechanisms },
    TileGroup { "Rails", rail },
};
constexpr std::size_t charactersSection = 2;
constexpr std::size_t overworldSection = 3;
constexpr std::size_t paletteTileCount = basics.size() + terrain.size() + characters.size() +
    objects.size() + mechanisms.size() + rail.size() + overworldTiles.size() + 1;
constexpr UiControlId firstTileId = 100;
constexpr UiControlId firstPaletteSectionId = firstTileId - groups.size();
constexpr std::size_t paletteRowCapacity = paletteTileCount + groups.size();
constexpr UiControlId selectorTileId = firstTileId + tileTypeCount;
constexpr UiControlId firstLinkGroupId = 100;

UiControlId commandId(MakerCommand command)
{
    return static_cast<UiControlId>(command);
}

UiControlId tileId(TileType tile)
{
    return firstTileId + static_cast<UiControlId>(tile);
}

bool selectedTile(TileType tile, TileType brush)
{
    const auto* group = editorTilePalette::groupFor(tile);
    return tile == brush || (group && group->rotationOnly && group->contains(brush));
}

struct ToolbarControl {
    MakerCommand command;
    UiRect bounds;
    std::string_view label;
    bool enabled = true;
    bool accented = false;
    std::optional<Vec3> swatch;
    bool inOverflow = false;
};

constexpr std::size_t toolbarControlCount = 36;
constexpr std::size_t linkActionCapacity = 7;

void drawColorSwatch(UiContext& ui, UiRect bounds, Vec3 color, float scale)
{
    const Vec2 inset { scale, scale };
    const Vec3 linear = ui::color::displayLinear(color);
    ui.rect(bounds, ink);
    ui.rect({ bounds.position + inset, bounds.size - inset * 2.0f },
        { linear.x, linear.y, linear.z, 1.0f });
}

float fitLabel(UiContext& ui, std::string_view label, float textSize, float availableWidth)
{
    const float width = ui.measureText(label, textSize).x;
    return width > availableWidth ? textSize * availableWidth / width : textSize;
}

bool drawToolbarControl(UiContext& ui, const ToolbarControl& control,
    const UiInteraction& interaction, bool showFocus, float scale)
{
    if (control.bounds.size.x <= 0.0f || control.bounds.size.y <= 0.0f) return false;
    ui.pushClip(control.bounds);
    (void)uiControls::button(ui, control.bounds, "", {
        .tone = control.accented ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
        .focused = showFocus && interaction.focused == commandId(control.command),
        .enabled = control.enabled,
        .contentScale = 0.72f * scale,
    });
    const float normalTextSize = 24.0f * 0.72f * scale;
    UiRect labelBounds = control.bounds;
    if (control.swatch && control.bounds.size.x >= 45.0f * scale) {
        const Vec2 swatchSize { 12.0f * scale, 12.0f * scale };
        const Vec2 swatchPosition = control.bounds.position +
            Vec2 { 7.0f * scale, (control.bounds.size.y - swatchSize.y) * 0.5f };
        drawColorSwatch(ui, { swatchPosition, swatchSize }, *control.swatch, scale);
        labelBounds.position.x += 21.0f * scale;
        labelBounds.size.x -= 21.0f * scale;
    }
    const float innerWidth = labelBounds.size.x - 8.0f * scale;
    if (innerWidth > 0.0f) {
        ui.centeredText(labelBounds, control.label, control.enabled ? buttonInk : disabledButtonInk,
            fitLabel(ui, control.label, normalTextSize, innerWidth));
    }
    ui.popClip();
    return control.enabled && interaction.activated == commandId(control.command);
}

std::array<ToolbarControl, toolbarControlCount> toolbarControls(const MakerLayout& layout, const MakerViewState& state)
{
    const float scale = layout.scale;
    const float height = 34.0f * scale;
    const float gap = 6.0f * scale;
    const float inset = margin * scale;
    const float width = layout.toolbar.size.x;
    const bool wide = width >= wideToolbarWidth * scale;
    const bool narrow = width < toolbarOverflowWidth * scale;
    const UiRect help { { width - inset - height, 14.0f * scale }, { height, height } };
    const float backWidth = std::min(88.0f * scale, width * 0.18f);
    const UiRect back { { help.position.x - gap - backWidth, help.position.y }, { backWidth, height } };
    const float testWidth = std::min(120.0f * scale, width * 0.23f);
    const UiRect test { { back.position.x - gap - testWidth, back.position.y }, { testWidth, height } };
    const float fileWidth = std::min(68.0f * scale, width * 0.13f);
    const UiRect load { { test.position.x - gap - fileWidth, back.position.y }, { fileWidth, height } };
    const UiRect save { { load.position.x - gap - fileWidth, back.position.y }, { fileWidth, height } };
    const float newWidth = std::min(104.0f * scale, width * 0.23f);
    const UiRect newDraft { { save.position.x - gap - newWidth, back.position.y }, { newWidth, height } };
    const float resizeWidth = std::min(126.0f * scale, width * 0.26f);
    const float resizeEnd = wide ? newDraft.position.x - gap : width - inset;
    const UiRect resize { { resizeEnd - resizeWidth, (wide ? 14.0f : 53.0f) * scale },
        { resizeWidth, height } };
    const float cameraWidth = std::min(74.0f * scale, width * 0.14f);
    const UiRect camera { { resize.position.x - gap - cameraWidth, resize.position.y },
        { cameraWidth, height } };
    const float toolsStart = wide ? 222.0f * scale : inset;
    const float toolsEnd = camera.position.x - (wide ? 16.0f : 6.0f) * scale;
    const float toolsWidth = std::max(0.0f, toolsEnd - toolsStart);
    const float y = (wide ? 14.0f : 53.0f) * scale;
    const float layerWidth = std::min(148.0f * scale, toolsWidth * 0.28f);
    const float layerButton = std::min(30.0f * scale, layerWidth * 0.24f);
    const float toolCount = state.overworld ? 7.0f : 6.0f;
    const float buttonWidth = std::min(92.0f * scale,
        std::max(0.0f, (toolsWidth - layerWidth - gap * toolCount) / toolCount));
    const auto tool = [&](int index) -> UiRect {
        return { { toolsStart + static_cast<float>(index) * (buttonWidth + gap), y },
            { buttonWidth, height } };
    };
    const float layerStart = toolsStart + toolCount * (buttonWidth + gap);
    const bool connecting = state.linkInspector.has_value();
    const bool ordering = connecting && state.linkInspector->kind == MakerLinkInspector::Kind::Selector;
    const float modeWidth = std::min(88.0f * scale, layout.palette.size.x * 0.46f);
    const UiRect modeSwitch {
        layout.palette.position + Vec2 { layout.palette.size.x - inset - modeWidth, 9.0f * scale },
        { modeWidth, 28.0f * scale },
    };
    using AreaMode = MakerAreaViewState::Mode;
    const bool showingPalette = !connecting && state.panel == MakerPanel::Palette;
    const bool showAreaTools = showingPalette && (state.area.mode || state.area.hasSelection);
    const bool showSelectionActions = state.area.hasSelection &&
        (!state.area.mode || state.area.mode == AreaMode::Rectangle);
    const float areaSwitchWidth = std::clamp(modeSwitch.position.x - layout.palette.position.x - 62.0f * scale,
        0.0f, 55.0f * scale);
    const UiRect areaSwitch = showingPalette ? UiRect {
        modeSwitch.position - Vec2 { areaSwitchWidth + 6.0f * scale, 0.0f }, { areaSwitchWidth, modeSwitch.size.y },
    } : UiRect {};
    const auto areaButton = [&](int row, int column, int count) -> UiRect {
        if (!showAreaTools || (row == 1 && !showSelectionActions)) return {};
        const float buttonY = listTop + static_cast<float>(row) * (areaRowHeight + areaRowGap);
        if (layout.palette.size.y < (buttonY + areaRowHeight) * scale) return {};
        const float buttonGap = 4.0f * scale;
        const float areaWidth = layout.palette.size.x - 20.0f * scale;
        const float actionWidth = (areaWidth - buttonGap * static_cast<float>(count - 1)) / static_cast<float>(count);
        return { layout.palette.position + Vec2 { 10.0f * scale + (actionWidth + buttonGap) * static_cast<float>(column),
                    buttonY * scale }, { actionWidth, areaRowHeight * scale } };
    };
    const Vec2 screenNavStart = layout.footer.position + Vec2 { margin, 4.0f } * scale;
    const Vec2 screenNavSize { 27.0f * scale, 26.0f * scale };
    const UiRect previousScreen = state.overworld ? UiRect { screenNavStart, screenNavSize } : UiRect {};
    const UiRect nextScreen = state.overworld
        ? UiRect { screenNavStart + Vec2 { 31.0f * scale, 0.0f }, screenNavSize } : UiRect {};
    const UiRect overworldMap = state.overworld
        ? UiRect { screenNavStart + Vec2 { 62.0f * scale, 0.0f }, { 52.0f * scale, screenNavSize.y } } : UiRect {};
    const float viewInset = 10.0f * scale;
    const float viewWidth = std::max(0.0f, layout.board.size.x - 2.0f * viewInset);
    const float viewHeight = std::clamp(layout.board.size.y - 2.0f * viewInset, 0.0f, 28.0f * scale);
    const Vec2 viewStart = layout.board.position + Vec2 { viewInset, viewInset };
    const float topDownWidth = std::min(92.0f * scale, viewWidth * 0.42f);
    const UiRect topDown { viewStart, { topDownWidth, viewHeight } };
    const UiRect frameSelection {
        viewStart + Vec2 { topDownWidth + gap, 0.0f },
        { std::clamp(viewWidth - topDownWidth - gap, 0.0f, 142.0f * scale), viewHeight },
    };
    const float cursorWidth = 104.0f * scale;
    const bool cursorFits = topDownWidth + frameSelection.size.x + 2.0f * gap + cursorWidth <= viewWidth;
    const UiRect cursor {
        frameSelection.position + Vec2 { frameSelection.size.x + gap, 0.0f },
        { cursorWidth, viewHeight },
    };
    const auto panelTab = [&](std::size_t column) -> UiRect {
        constexpr std::array columnEdges { 0.0f, 0.18f, 0.38f, 0.70f, 1.0f };
        const float tabWidths = std::max(0.0f,
            layout.palette.size.x - static_cast<float>(columnEdges.size()) * sidebarTabInset * scale);
        const float tabRowHeight = std::min(layout.palette.size.y, sidebarTabHeight * scale);
        const float tabY = layout.palette.position.y + std::max(0.0f, layout.palette.size.y - tabRowHeight - sidebarTabInset * scale);
        return {
            { layout.palette.position.x + tabWidths * columnEdges[column] +
                static_cast<float>(column + 1) * sidebarTabInset * scale, tabY },
            { tabWidths * (columnEdges[column + 1] - columnEdges[column]), tabRowHeight },
        };
    };
    std::array<ToolbarControl, toolbarControlCount> controls {{
        { MakerCommand::Paint, tool(0), "Paint", true,
            !state.erasing && !connecting && !state.area.mode && state.panel != MakerPanel::Properties },
        { MakerCommand::Erase, tool(1), "Erase", true, state.erasing && !connecting },
        { MakerCommand::Connect, tool(2), "Connect", true, connecting && !ordering },
        connecting && !ordering ? ToolbarControl { MakerCommand::ToggleLinkLayer, tool(3),
            state.linkLayerIsolated ? "This Layer" : "All Layers", state.layerCount > 1,
            state.linkLayerIsolated } : ToolbarControl { MakerCommand::Rotate, tool(3), "Rotate", !state.erasing },
        { MakerCommand::Undo, tool(4), "Undo", state.canUndo },
        { MakerCommand::Redo, tool(5), "Redo", state.canRedo },
        { MakerCommand::ConnectSelectors, state.overworld ? tool(6) : UiRect {},
            "Order", state.overworld, ordering },
        { MakerCommand::LayerDown, { { layerStart, y }, { layerButton, height } }, "-", state.layer > 0 },
        { MakerCommand::LayerUp, { { layerStart + layerWidth - layerButton, y },
            { layerButton, height } }, "+", state.layer + 1 < state.layerCount },
        { MakerCommand::Camera, camera, "Camera" },
        state.overworld ? ToolbarControl { MakerCommand::ToggleOverworldNeighbors, resize,
            "Show Neighbors", true, state.showNeighbors }
            : ToolbarControl { MakerCommand::ResizeBoard, resize,
                resizeWidth >= 104.0f * scale ? "Resize Board" : "Resize" },
        { MakerCommand::NewDraft, newDraft, "New Draft" },
        { MakerCommand::Save, save, "Save" },
        { MakerCommand::Load, load, "Load" },
        { MakerCommand::TestPlay, test, "Test Play", true, true },
        { MakerCommand::Back, back, "Back" },
        { MakerCommand::Help, help, "?" },
        { MakerCommand::ToggleOverworldMode, modeSwitch, state.overworld ? "Puzzle" : "Overworld" },
        { MakerCommand::PreviousOverworldScreen, previousScreen, "<", state.overworld && state.hasMultipleOverworldScreens },
        { MakerCommand::NextOverworldScreen, nextScreen, ">", state.overworld && state.hasMultipleOverworldScreens },
        { MakerCommand::SelectArea, areaSwitch, "Area", !connecting, state.area.mode == AreaMode::Rectangle },
        { MakerCommand::FloodFill, areaButton(0, 0, 2), "Flood Fill", true, state.area.mode == AreaMode::FloodFill },
        { MakerCommand::PasteArea, areaButton(0, 1, 2), "Paste", state.area.hasClipboard, state.area.mode == AreaMode::Paste },
        { MakerCommand::FillSelection, areaButton(1, 0, 4), "Fill", state.area.hasSelection },
        { MakerCommand::CopySelection, areaButton(1, 1, 4), "Copy", state.area.hasSelection },
        { MakerCommand::CutSelection, areaButton(1, 2, 4), "Cut", state.area.hasSelection },
        { MakerCommand::EraseSelection, areaButton(1, 3, 4), "Erase", state.area.hasSelection },
        { MakerCommand::ShowPalette, panelTab(0), "Tiles", true, showingPalette },
        { MakerCommand::ShowLayers, panelTab(1), "Layers", true, state.panel == MakerPanel::Layers },
        { MakerCommand::ShowProperties, panelTab(2), "Properties", true, state.panel == MakerPanel::Properties },
        { MakerCommand::ShowProblems, panelTab(3), "Problems", true, state.panel == MakerPanel::Problems },
        { MakerCommand::ToggleTopDown, state.showViewportControls ? topDown : UiRect {}, "Top Down",
            state.showViewportControls, state.topDown },
        { MakerCommand::FrameSelection, state.showViewportControls ? frameSelection : UiRect {},
            state.canFrameSelection ? "Frame Selection" : "Frame Board", state.showViewportControls },
        { MakerCommand::ToggleBoardCursor, state.showViewportControls && cursorFits ? cursor : UiRect {},
            "Board Cursor", state.showViewportControls, state.boardCursorFocused, std::nullopt,
            narrow && state.showViewportControls && !cursorFits },
        { MakerCommand::OverworldMap, overworldMap, "Map", state.overworld },
        { MakerCommand::ToggleToolbarOverflow, {}, "More", narrow },
    }};
    if (!narrow) return controls;

    const auto at = [&](MakerCommand command) -> ToolbarControl& {
        return *std::ranges::find(controls, command, &ToolbarControl::command);
    };
    const auto topRowEnd = std::ranges::find(controls, MakerCommand::Help, &ToolbarControl::command);
    for (auto& control : std::span(controls.begin(), topRowEnd + 1)) {
        control.bounds = {};
        control.inOverflow = control.command != MakerCommand::ConnectSelectors || state.overworld;
    }
    const float available = std::max(0.0f, width - 2.0f * inset);
    float x = width - inset;
    const auto placeRight = [&](MakerCommand command, float logicalWidth, bool show) {
        if (!show || x <= inset) return;
        auto& control = at(command);
        const float button = std::min(logicalWidth * scale, x - inset);
        x -= button;
        control.bounds = { { x, help.position.y }, { button, height } };
        control.inOverflow = false;
        x -= gap;
    };
    placeRight(MakerCommand::Help, 34.0f, available >= 410.0f * scale);
    placeRight(MakerCommand::Back, 64.0f, available >= 250.0f * scale);
    placeRight(MakerCommand::ToggleToolbarOverflow, 72.0f, true);
    placeRight(MakerCommand::TestPlay, 96.0f, available >= 210.0f * scale);
    placeRight(MakerCommand::Save, 58.0f, available >= 390.0f * scale);

    x = inset;
    const auto placeTool = [&](MakerCommand command, float logicalWidth) {
        const float button = logicalWidth * scale;
        if (x + button > width - inset) return;
        auto& control = at(command);
        control.bounds = { { x, y }, { button, height } };
        control.inOverflow = false;
        x += button + gap;
    };
    placeTool(MakerCommand::Paint, 55.0f);
    placeTool(MakerCommand::Erase, 55.0f);
    placeTool(MakerCommand::Connect, 72.0f);
    placeTool(MakerCommand::Undo, 54.0f);
    placeTool(MakerCommand::Redo, 54.0f);
    placeTool(controls[3].command, connecting && !ordering ? 83.0f : 62.0f);
    if (state.overworld) placeTool(MakerCommand::ConnectSelectors, 63.0f);
    if (x + 116.0f * scale <= width - inset) {
        auto& lower = at(MakerCommand::LayerDown);
        auto& upper = at(MakerCommand::LayerUp);
        const Vec2 button { 27.0f * scale, height };
        lower.bounds = { { x, y }, button };
        upper.bounds = { { x + 89.0f * scale, y }, button };
        lower.inOverflow = upper.inOverflow = false;
    }
    return controls;
}

MakerCommand drawToolbar(UiContext& ui, const MakerLayout& layout, const MakerViewState& state,
    std::span<const ToolbarControl> controls, const UiInteraction& interaction, bool showFocus)
{
    MakerCommand command = MakerCommand::None;
    for (const auto& control : controls) {
        if (drawToolbarControl(ui, control, interaction, showFocus, layout.scale)) command = control.command;
    }
    const auto lower = std::ranges::find(controls, MakerCommand::LayerDown, &ToolbarControl::command);
    const auto upper = std::ranges::find(controls, MakerCommand::LayerUp, &ToolbarControl::command);
    if (lower->bounds.size.x > 0.0f && upper->bounds.size.x > 0.0f) {
        const float left = lower->bounds.position.x + lower->bounds.size.x;
        ui.centeredText({ { left, lower->bounds.position.y },
                            { upper->bounds.position.x - left, lower->bounds.size.y } },
            "Layer " + std::to_string(state.layer + 1), state.layerLocked ? accentInk : ink, 15.0f * layout.scale);
    }
    return command;
}

void drawChrome(UiContext& ui, const MakerLayout& layout)
{
    const UiRect board = layout.board;
    const float scale = layout.scale;
    const float right = board.position.x + board.size.x;
    const float bottom = board.position.y + board.size.y;
    const float width = layout.toolbar.size.x;
    const float height = layout.footer.position.y + layout.footer.size.y;
    ui.rect({ {}, { width, board.position.y } }, background);
    ui.rect({ { 0.0f, board.position.y }, { board.position.x, board.size.y } }, background);
    ui.rect({ { right, board.position.y }, { width - right, board.size.y } }, background);
    ui.rect({ { 0.0f, bottom }, { width, height - bottom } }, background);
    ui.rect(layout.toolbar, chrome);
    ui.rect(layout.footer, chrome);
    ui.rect(layout.palette, chrome);
    ui.divider({ { 0.0f, layout.toolbar.size.y - scale }, { width, scale } });
    ui.divider({ layout.footer.position, { width, scale } });

    // The board's border is outside the shared viewport and picking bounds.
    ui.rect({ board.position - Vec2 { scale, scale }, { board.size.x + 2.0f * scale, scale } }, divider);
    ui.rect({ { board.position.x - scale, bottom }, { board.size.x + 2.0f * scale, scale } }, divider);
    ui.rect({ { board.position.x - scale, board.position.y }, { scale, board.size.y } }, divider);
    ui.rect({ { right, board.position.y }, { scale, board.size.y } }, divider);
}

void drawTitle(UiContext& ui, const MakerLayout& layout, float right, std::string_view title, Vec4 color = ink)
{
    const float scale = layout.scale;
    ui.pushClip({ {}, { std::max(0.0f, right - margin * scale), layout.toolbar.size.y } });
    const Vec2 position = Vec2 { margin + 4.0f, 17.0f } * scale;
    ui.text(position, title, color,
        fitLabel(ui, title, 24.0f * scale, std::max(0.0f, right - margin * scale - position.x)));
    ui.popClip();
}

struct PaletteRow {
    UiControlId id;
    UiRect bounds;
    std::size_t section;
    std::optional<TileType> tile;
    bool selector = false;
};

struct DraftDialogCopy {
    std::string_view title;
    std::string_view submitLabel;
    std::string_view scope;
};

DraftDialogCopy draftDialogCopy(MakerDraftDialogMode mode, bool overworld)
{
    switch (mode) {
    case MakerDraftDialogMode::Save: return { "Save Draft", "Save", "Maker Draft Save" };
    case MakerDraftDialogMode::Load: return { "Load Draft", "Load", "Maker Draft Load" };
    case MakerDraftDialogMode::Overwrite: return { "Replace Draft?", "Replace", "Maker Draft Overwrite" };
    case MakerDraftDialogMode::Discard: return { "Discard Changes?", "Discard and Load", "Maker Draft Discard" };
    case MakerDraftDialogMode::NewDraft: return { "New Draft", "Create", "Maker New Draft" };
    case MakerDraftDialogMode::DiscardNew: return { "Discard Changes?", "Discard and Create", "Maker Discard for New Draft" };
    case MakerDraftDialogMode::ResizeBoard: return { "Resize Board", "Resize", "Maker Resize Board" };
    case MakerDraftDialogMode::ConfirmCrop: return { "Crop Board?", "Crop and Resize", "Maker Confirm Crop" };
    case MakerDraftDialogMode::NameLinkGroup: return { "New Group", "Create", "Maker New Link Group" };
    case MakerDraftDialogMode::RenameLinkGroup: return { "Rename Group", "Rename", "Maker Rename Link Group" };
    case MakerDraftDialogMode::LecternText: return { "Lectern Text", "Apply", "Maker Lectern Text" };
    case MakerDraftDialogMode::SelectorTarget: return { "Attach Level", "Apply", "Maker Selector Target" };
    case MakerDraftDialogMode::ConfirmDeleteLayer: return { "Delete Layer?", "Delete Layer", "Maker Delete Layer" };
    case MakerDraftDialogMode::RenameDraft:
        return { overworld ? "Rename Overworld" : "Rename Level", "Rename", "Maker Rename Draft" };
    case MakerDraftDialogMode::DeleteDraft:
        return { overworld ? "Delete Overworld?" : "Delete Level?", "Delete", "Maker Delete Draft" };
    case MakerDraftDialogMode::RecoverAutosave: return { "Recover Autosave?", "Recover", "Maker Recover Autosave" };
    case MakerDraftDialogMode::DiscardAutosave: return { "Discard Autosave?", "Discard Autosave", "Maker Discard Autosave" };
    }
    return {};
}

bool nameDialog(MakerDraftDialogMode mode)
{
    return mode == MakerDraftDialogMode::Save || mode == MakerDraftDialogMode::RenameDraft || mode == MakerDraftDialogMode::NameLinkGroup ||
        mode == MakerDraftDialogMode::RenameLinkGroup || mode == MakerDraftDialogMode::LecternText;
}

float drawWrappedText(UiContext& ui, UiRect bounds, std::string_view text, Vec4 color, float size)
{
    if (text.empty() || bounds.size.x <= 0.0f || bounds.size.y <= 0.0f) return 0.0f;
    const auto& lines = ui.wrapText(text, bounds.size.x, size);
    ui.pushClip(bounds);
    float y = bounds.position.y;
    for (const auto& line : lines) {
        if (y >= bounds.position.y + bounds.size.y) break;
        ui.text({ bounds.position.x, y }, line, color, size);
        y += size * 1.25f;
    }
    ui.popClip();
    return y - bounds.position.y;
}

void drawTileThumbnail(UiContext& ui, UiRect bounds, TileType tile, float fallbackSize,
    const MakerViewInput& input)
{
    constexpr float cornerRadiusRatio = 0.16f;
    const RenderTexture texture = input.tileThumbnail ? input.tileThumbnail(tile) : noTexture;
    if (!texture.isNone()) {
        ui.textureImage(bounds, texture, { {}, { 1.0f, 1.0f } },
            { 1.0f, 1.0f, 1.0f, 1.0f }, {}, bounds.size.x * cornerRadiusRatio);
    } else {
        const Vec2 size { fallbackSize, fallbackSize };
        ui.rect({ bounds.position + (bounds.size - size) * 0.5f, size },
            tileColor(tile), fallbackSize * cornerRadiusRatio);
    }
}

void drawSelectorThumbnail(UiContext& ui, UiRect bounds)
{
    const float scale = std::min(bounds.size.x, bounds.size.y) / 28.0f;
    ui.rect(bounds, { 0.72f, 0.74f, 0.73f, 1.0f }, 4.0f * scale);
    ui.rect({ bounds.position + Vec2 { 8.0f, 6.0f } * scale, Vec2 { 2.0f, 17.0f } * scale },
        { 0.12f, 0.20f, 0.20f, 1.0f });
    ui.rect({ bounds.position + Vec2 { 10.0f, 6.0f } * scale, Vec2 { 11.0f, 8.0f } * scale },
        { 0.10f, 0.46f, 0.39f, 1.0f });
}

void drawTileGuide(UiContext& ui, UiRect panel, float scale, TileType tile, const MakerViewInput& input,
    bool overworld, bool selector)
{
    const auto entry = selector ? makerTileGuide::levelSelector : makerTileGuide::forTile(tile, overworld);
    ui.pushClip(panel);
    ui.divider({ panel.position, { panel.size.x, scale } });
    const float iconSize = 30.0f * scale;
    const Vec2 header = panel.position + Vec2 { 2.0f, 7.0f } * scale;
    const UiRect icon { header, { iconSize, iconSize } };
    if (selector) drawSelectorThumbnail(ui, icon);
    else drawTileThumbnail(ui, icon, tile, 20.0f * scale, input);
    const auto label = selector ? makerTileGuide::levelSelectorName : makerTileGuide::name(tile, overworld);
    ui.text(header + Vec2 { 38.0f, 3.0f } * scale, label, ink,
        fitLabel(ui, label, 18.0f * scale, std::max(0.0f, panel.size.x - 42.0f * scale)));

    const Vec2 copy = panel.position + Vec2 { 2.0f, 41.0f } * scale;
    const float copyWidth = panel.size.x - 4.0f * scale;
    const float copyHeight = panel.size.y - 41.0f * scale;
    const float bodySize = std::clamp(panel.size.x / (15.0f * scale), 12.0f, 14.0f) * scale;
    const float descriptionHeight = drawWrappedText(ui,
        { copy, { copyWidth, copyHeight } }, entry.description, ink, bodySize);
    const float hintOffset = descriptionHeight + 4.0f * scale;
    drawWrappedText(ui, { copy + Vec2 { 0.0f, hintOffset }, { copyWidth, copyHeight - hintOffset } },
        entry.placementHint, accentInk, bodySize);
    ui.popClip();
}

void drawPlacementHint(UiContext& ui, UiRect board, float scale,
    std::string_view hint, bool blocked)
{
    const float inset = 12.0f * scale;
    const float textSize = 15.0f * scale;
    const float lineHeight = textSize * 1.25f;
    const float width = std::min(board.size.x - 2.0f * inset,
        ui.measureText(hint, textSize).x + 2.0f * inset);
    if (width <= 2.0f * inset) return;
    const auto& lines = ui.wrapText(hint, width - 2.0f * inset, textSize);
    const float height = static_cast<float>(lines.size()) * lineHeight + inset;
    if (height > board.size.y - 2.0f * inset) return;
    UiRect hintBounds {
        board.position + Vec2 { (board.size.x - width) * 0.5f, board.size.y - height - inset },
        { width, height },
    };
    if (ui.contains(hintBounds, ui.mousePosition())) {
        hintBounds.position.y = board.position.y + inset;
    }
    ui.pushClip(board);
    ui.rect(hintBounds, background);
    const Vec4 color = blocked ? errorInk : accentInk;
    ui.rect({ hintBounds.position, { width, scale } }, color);
    float y = hintBounds.position.y + inset * 0.5f;
    for (const auto& line : lines) {
        ui.text({ hintBounds.position.x + inset, y }, line, color, textSize);
        y += lineHeight;
    }
    ui.popClip();
}

void drawLinkOverlay(UiContext& ui, UiRect viewport, float scale,
    std::span<const MakerLinkLine> lines, std::span<const MakerLinkMarker> markers)
{
    constexpr std::size_t overlayCommandBudget = 3072;
    const auto markerCount = std::min(markers.size(), overlayCommandBudget / 6);
    const bool directional = std::ranges::any_of(lines, &MakerLinkLine::directional);
    const std::size_t commandsPerLine = directional ? 12 : 4;
    const auto lineCount = std::min(lines.size(),
        (overlayCommandBudget - markerCount * 2 - 512) / commandsPerLine);
    const std::size_t maxDots = lineCount == 0 ? 0 :
        std::min<std::size_t>((overlayCommandBudget - markerCount * 2 - 512 -
            (directional ? lineCount * 8 : 0)) / (lineCount * 2), 109);
    std::size_t labels = 0;
    ui.pushClip(viewport);
    for (const auto& line : lines.first(lineCount)) {
        const Vec2 delta = line.to - line.from;
        const float distance = length(delta);
        if (!std::isfinite(distance) || distance <= 0.0f) continue;
        const Vec2 direction = delta * (1.0f / distance);
        const Vec2 normal { -direction.y, direction.x };
        const int steps = static_cast<int>(std::clamp(std::ceil(distance / (7.0f * scale)), 1.0f,
            static_cast<float>(maxDots + 1)));
        for (int step = 1; step < steps; ++step) {
            const Vec2 point = line.from + delta * (static_cast<float>(step) / static_cast<float>(steps));
            const Vec2 radius = Vec2 { 2.0f, 2.0f } * scale;
            ui.rect({ point - radius, radius * 2.0f }, ink);
            ui.rect({ point - radius * 0.65f, radius * 1.3f }, line.color);
        }
        if (line.directional && distance > 12.0f * scale) {
            const Vec2 tip = line.to - direction * (9.0f * scale);
            const Vec2 radius { 1.8f * scale, 1.8f * scale };
            for (int step = 0; step < 4; ++step) {
                const float offset = static_cast<float>(step) * scale;
                const Vec2 base = tip - direction * (2.0f * offset);
                for (const float side : { -1.0f, 1.0f }) {
                    const Vec2 point = base + normal * (1.3f * offset * side);
                    ui.rect({ point - radius, radius * 2.0f }, line.color);
                }
            }
        }
        if (!line.label.empty() && labels < 8) {
            ++labels;
            const float textSize = 13.0f * scale;
            const float width = std::min(ui.measureText(line.label, textSize).x + 14.0f * scale,
                viewport.size.x);
            const Vec2 size { width, 22.0f * scale };
            Vec2 center = (line.from + line.to) * 0.5f;
            if (line.directional) {
                const float clearance = (std::abs(normal.x) * size.x + std::abs(normal.y) * size.y) * 0.5f;
                center = center + normal * (clearance + 5.0f * scale);
            }
            const UiRect label { center - size * 0.5f, size };
            ui.rect(label, background);
            ui.pushClip(label);
            ui.centeredText(label, line.label, ink, textSize);
            ui.popClip();
        }
    }
    for (const auto& marker : markers.first(markerCount)) {
        if (!std::isfinite(marker.position.x) || !std::isfinite(marker.position.y)) continue;
        const float size = (marker.selected ? 12.0f : 8.0f) * scale;
        const Vec2 radius { size * 0.5f, size * 0.5f };
        ui.rect({ marker.position - radius, radius * 2.0f }, ink);
        ui.rect({ marker.position - radius * 0.58f, radius * 1.16f }, marker.color);
    }
    ui.popClip();
}

void drawAreaOutline(UiContext& ui, UiRect viewport, float scale, const MakerAreaViewState::Outline& outline)
{
    ui.pushClip(viewport);
    for (std::size_t i = 0; i < outline.corners.size(); ++i) {
        const Vec2 from = outline.corners[i];
        const Vec2 delta = outline.corners[(i + 1) % outline.corners.size()] - from;
        const float distance = length(delta);
        if (!std::isfinite(distance)) continue;
        const int steps = static_cast<int>(std::clamp(std::ceil(distance / (6.0f * scale)), 1.0f, 160.0f));
        for (int step = 1; step < steps; ++step) {
            const Vec2 point = from + delta * (static_cast<float>(step) / static_cast<float>(steps));
            const Vec2 border = Vec2 { 2.6f, 2.6f } * scale;
            const Vec2 center = Vec2 { 1.7f, 1.7f } * scale;
            ui.rect({ point - border, border * 2.0f }, background);
            ui.rect({ point - center, center * 2.0f }, outline.color);
        }
        const Vec2 border = Vec2 { 5.0f, 5.0f } * scale;
        const Vec2 center = Vec2 { 3.0f, 3.0f } * scale;
        ui.rect({ from - border, border * 2.0f }, background);
        ui.rect({ from - center, center * 2.0f }, outline.color);
    }
    ui.popClip();
}

std::string_view linkKindTitle(const MakerLinkInspector& inspector)
{
    if (!inspector.kind) return "Connect tiles";
    switch (*inspector.kind) {
    case MakerLinkInspector::Kind::Device: return "Signal wiring";
    case MakerLinkInspector::Kind::Portal: return "Portal pair";
    case MakerLinkInspector::Kind::Object: return "Synchronized objects";
    case MakerLinkInspector::Kind::Selector: return "Connect Levels";
    }
    return {};
}

MakerViewAction drawLinkInspector(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input,
    std::span<const ToolbarControl> toolbar, UiControlId& focus, UiScroll& scroll)
{
    const auto& inspector = *state.linkInspector;
    const float scale = layout.scale;
    const float padding = 10.0f * scale;
    const float gap = 6.0f * scale;
    const Vec2 content = layout.palette.position + Vec2 { padding, padding };
    const float width = std::max(0.0f, layout.palette.size.x - padding * 2.0f);
    const float endY = layout.palette.position.y + layout.palette.size.y - padding;
    ui.pushClip(layout.palette);
    const auto mode = std::ranges::find(toolbar, MakerCommand::ToggleOverworldMode, &ToolbarControl::command);
    const auto title = linkKindTitle(inspector);
    ui.text(content, title, ink, fitLabel(ui, title, 22.0f * scale,
        mode == toolbar.end() ? width : mode->bounds.position.x - content.x - gap));
    ui.divider({ content + Vec2 { 0.0f, 31.0f } * scale, { width, scale } });
    float y = content.y + 39.0f * scale;
    if (!inspector.title.empty()) {
        ui.text({ content.x, y }, inspector.title, accentInk, 16.0f * scale);
        y += 23.0f * scale;
    }
    const auto describe = [&](std::string_view text) {
        y += drawWrappedText(ui, { { content.x, y }, { width, std::max(0.0f, endY - y) } },
            text, mutedInk, 14.0f * scale);
    };
    describe(inspector.detail.empty()
        ? "Click a signal source, device, portal, or movable object on the board."
        : inspector.detail);
    y += gap;

    std::array<ToolbarControl, linkActionCapacity> actions;
    std::size_t actionCount = 0;
    const auto addAction = [&](MakerCommand command, std::string_view label, bool show) {
        if (show) actions[actionCount++] = { command, {}, label };
    };
    addAction(MakerCommand::NewLinkGroup, "New Group", inspector.canCreate);
    addAction(MakerCommand::RenameLinkGroup, "Rename", inspector.canRename);
    if (inspector.groupColor) {
        actions[actionCount++] = { .command = MakerCommand::EditLinkGroupColor, .label = "Color",
            .swatch = inspector.groupColor };
    }
    addAction(MakerCommand::Disconnect,
        inspector.kind == MakerLinkInspector::Kind::Portal ? "Unpair" : "Disconnect", inspector.canDisconnect);
    addAction(MakerCommand::ClearLinkSelection,
        inspector.kind == MakerLinkInspector::Kind::Selector ? "Clear Source" : "Clear", inspector.canClear);
    addAction(MakerCommand::SelectOtherLinkEndpoint,
        inspector.kind == MakerLinkInspector::Kind::Object ? "Covered Tile" : "Top Tile", inspector.canSelectOther);
    if (inspector.elevatorStopOnLayer) {
        addAction(MakerCommand::ToggleElevatorStop,
            *inspector.elevatorStopOnLayer ? "Remove Stop" : "Add Stop", true);
    }
    constexpr int buttonColumns = 2;
    const float buttonWidth = (width - gap) / buttonColumns;
    const float buttonHeight = 29.0f * scale;
    for (std::size_t i = 0; i < actionCount; ++i) {
        actions[i].bounds = { { content.x + (buttonWidth + gap) * static_cast<float>(i % buttonColumns),
            y + (buttonHeight + gap) * static_cast<float>(i / buttonColumns) }, { buttonWidth, buttonHeight } };
    }
    y += static_cast<float>((actionCount + buttonColumns - 1) / buttonColumns) * (buttonHeight + gap);

    const std::size_t linkGroupCount = inspector.groups.size();
    if (linkGroupCount > 0 || !inspector.members.empty()) {
        ui.divider({ { content.x, y + gap }, { width, scale } });
        y += 2.0f * gap;
    }
    const UiRect rows { { content.x, y },
        { std::max(0.0f, width - 9.0f * scale), std::max(0.0f, endY - y) } };
    const float rowHeight = 29.0f;
    const float headerHeight = 25.0f;
    const float groupRowsHeight = linkGroupCount > 0 ?
        headerHeight + static_cast<float>(linkGroupCount) * rowHeight : 0.0f;
    const float memberHeight = inspector.members.empty() ? 0.0f : headerHeight +
        static_cast<float>(inspector.members.size()) * rowHeight;
    scroll.setExtents(rows.size.y / scale, groupRowsHeight + memberHeight);
    const auto visibleRows = static_cast<std::size_t>(std::ceil(rows.size.y / (rowHeight * scale))) + 2;
    FrameArray<UiControlRegion> regions(ui.frameArena(), toolbar.size() + actionCount + visibleRows);
    for (const auto& button : toolbar) regions.push_back({ commandId(button.command), button.bounds, button.enabled });
    for (const auto& action : std::span(actions).first(actionCount)) {
        regions.push_back({ commandId(action.command), ui.visibleRect(action.bounds), action.enabled });
    }
    UiNavigation navigation = input.navigation;
    if (input.interactive) {
        scroll.scrollWheel(ui, rows, input.wheelY, input.wheelPosition);
        if (linkGroupCount > 0 && rows.size.y > 0.0f && navigation.previous != navigation.next &&
            !ui.mousePressPosition() && !ui.mouseDown()) {
            std::array<UiControlId, toolbarControlCount + linkActionCapacity> fixedOrder;
            std::size_t fixedCount = 0;
            for (const auto& region : regions) {
                if (region.enabled && region.bounds.size.x > 0.0f && region.bounds.size.y > 0.0f) {
                    fixedOrder[fixedCount++] = region.id;
                }
            }
            const auto order = std::span(fixedOrder).first(fixedCount);
            const auto current = std::ranges::find(order, focus);
            const bool groupFocused = focus >= firstLinkGroupId && focus - firstLinkGroupId < linkGroupCount;
            const std::size_t index = groupFocused ? fixedCount + focus - firstLinkGroupId :
                current == order.end() ? 0 : static_cast<std::size_t>(current - order.begin());
            const std::size_t count = fixedCount + linkGroupCount;
            const auto step = [&](std::size_t item) {
                return navigation.next ? (item + 1) % count : (item + count - 1) % count;
            };
            std::size_t target = step(index);
            if (target >= fixedCount && inspector.activeGroup == target - fixedCount) target = step(target);
            focus = target < fixedCount ? order[target] : firstLinkGroupId + target - fixedCount;
            if (focus >= firstLinkGroupId) {
                const float top = headerHeight + static_cast<float>(focus - firstLinkGroupId) * rowHeight;
                scroll.reveal(top, top + rowHeight);
            }
            navigation.previous = navigation.next = false;
        }
        scroll.drawScrollbar(ui, "maker.wiring.scroll", {
            { rows.position.x + rows.size.x + 4.0f * scale, rows.position.y }, { 5.0f * scale, rows.size.y },
        });
    }

    const auto rowBounds = [&](std::size_t index) -> UiRect {
        return { { rows.position.x, y + (headerHeight + static_cast<float>(index) * rowHeight - scroll.offset()) * scale },
            { rows.size.x, (rowHeight - 3.0f) * scale } };
    };
    const auto firstGroup = std::min(linkGroupCount, static_cast<std::size_t>(
        std::max(0.0f, scroll.offset() - headerHeight) / rowHeight));
    const auto lastGroup = std::min(linkGroupCount, firstGroup + visibleRows);
    ui.pushClip(rows);
    for (std::size_t i = firstGroup; i < lastGroup; ++i) {
        regions.push_back({ firstLinkGroupId + i, ui.visibleRect(rowBounds(i)), true,
            inspector.activeGroup != i });
    }
    ui.popClip();
    ui.popClip();

    UiInteraction interaction;
    if (input.interactive) {
        interaction = ui.resolveControls("Maker wiring", regions, focus, navigation);
        focus = interaction.focused;
    }
    MakerViewAction result { .command = drawToolbar(ui, layout, state, toolbar, interaction, true) };
    ui.pushClip(layout.palette);
    for (const auto& action : std::span(actions).first(actionCount)) {
        if (drawToolbarControl(ui, action, interaction, true, scale)) result.command = action.command;
    }
    ui.pushClip(rows);
    const float listY = y - scroll.offset() * scale;
    if (linkGroupCount > 0) ui.text({ rows.position.x, listY }, "Assign to group", mutedInk, 14.0f * scale);
    for (std::size_t i = firstGroup; i < lastGroup; ++i) {
        const UiRect row = rowBounds(i);
        if (ui.visibleRect(row).size.y <= 0.0f) continue;
        const bool active = inspector.activeGroup == i;
        const bool clicked = uiControls::button(ui, interaction, firstLinkGroupId + i, row, "",
            active ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal);
        if (clicked && !active) {
            result.command = MakerCommand::AssignLinkGroup;
            result.linkGroup = i;
        }
        float textInset = 7.0f * scale;
        if (i < inspector.groupColors.size()) {
            const Vec2 size { 11.0f * scale, 11.0f * scale };
            drawColorSwatch(ui, { row.position + Vec2 { textInset, (row.size.y - size.y) * 0.5f }, size },
                inspector.groupColors[i], scale);
            textInset += 17.0f * scale;
        }
        ui.text(row.position + Vec2 { textInset, 3.0f * scale }, inspector.groups[i], ink, 14.0f * scale);
    }
    if (!inspector.members.empty()) {
        float memberY = listY + groupRowsHeight * scale;
        ui.text({ rows.position.x, memberY }, inspector.kind == MakerLinkInspector::Kind::Selector
            ? "Unlock order" : "Connected tiles", mutedInk, 14.0f * scale);
        memberY += headerHeight * scale;
        for (const auto& member : inspector.members) {
            if (memberY + rowHeight * scale > rows.position.y && memberY < rows.position.y + rows.size.y) {
                ui.text({ rows.position.x, memberY }, member, ink, 14.0f * scale);
            }
            memberY += rowHeight * scale;
        }
    }
    ui.popClip();
    ui.popClip();
    if (input.interactive) {
        drawLinkOverlay(ui, state.linkViewport.value_or(layout.board), scale, state.linkLines, state.linkMarkers);
    }
    return result;
}

MakerViewAction drawMakerDetailsPanel(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input,
    std::span<const ToolbarControl> toolbar, UiControlId& focus, UiScroll& scroll)
{
    constexpr UiControlId firstValueRow = 100;
    const bool layers = state.panel == MakerPanel::Layers;
    const float scale = layout.scale;
    const Vec2 start = layout.palette.position + Vec2 { 10.0f, 40.0f } * scale;
    const float width = std::max(0.0f, layout.palette.size.x / scale - 29.0f);
    const UiRect viewport { start, { width * scale, std::max(0.0f, layout.palette.size.y - 43.0f * scale) } };
    const auto mode = std::ranges::find(toolbar, MakerCommand::ToggleOverworldMode, &ToolbarControl::command);
    const float headerWidth = mode->bounds.position.x - start.x - 6.0f * scale;
    const std::string_view title = layers ? "Layers" : "Properties";
    ui.pushClip(layout.palette);
    ui.text(layout.palette.position + Vec2 { 10.0f, 10.0f } * scale,
        title, ink, fitLabel(ui, title, 22.0f * scale, std::max(0.0f, headerWidth)));
    ui.divider({ layout.palette.position + Vec2 { 10.0f, 32.0f } * scale,
        { std::max(0.0f, layout.palette.size.x - 20.0f * scale), scale } });
    ui.popClip();

    UiPropertyInspector inspector(ui, viewport, scale, scroll);
    std::vector<std::pair<UiControlId, MakerViewAction>> actions;
    using Tone = uiControls::ButtonTone;
    const auto makeAction = [&](std::string_view label, MakerViewAction action, bool enabled = true,
                                Tone tone = Tone::Normal) -> UiPropertyInspector::Action {
        const auto id = action.layer ? firstValueRow + *action.layer :
            action.minecartDirection ? firstValueRow + *action.minecartDirection : commandId(action.command);
        actions.emplace_back(id, action);
        return { id, label, tone, enabled };
    };
    const auto button = [&](std::string_view label, MakerViewAction action, bool enabled = true,
                            Tone tone = Tone::Normal) {
        inspector.action(makeAction(label, action, enabled, tone));
    };
    if (layers) {
        if (state.overworld) inspector.note("Layer changes apply to the whole overworld.");
        inspector.actionRow(std::array {
            makeAction("Add Above", { .command = MakerCommand::AddLayerAbove }, state.layerPanel.canAdd),
            makeAction("Add Below", { .command = MakerCommand::AddLayerBelow }, state.layerPanel.canAdd),
        });
        const bool waterHere = state.layerPanel.waterLayer == state.layer;
        button(waterHere ? "Clear Water Layer" : state.layerPanel.waterLayer ? "Move Water Here" : "Set Water Layer",
            { .command = MakerCommand::ToggleWaterLayer }, state.layerPanel.canSetWater,
            waterHere ? Tone::Accent : Tone::Normal);
        button(state.layerLocked ? "Show All Layers" : "Show Only This Layer",
            { .command = MakerCommand::ToggleLayerLock }, true, state.layerLocked ? Tone::Accent : Tone::Normal);
        button("Delete Layer", { .command = MakerCommand::DeleteLayer }, state.layerPanel.canDelete, Tone::Danger);
        for (uint32_t layer = state.layerCount; layer > 0; --layer) {
            auto label = "Layer " + std::to_string(layer);
            if (state.layerPanel.waterLayer == layer - 1) label += "  \xc2\xb7  Water";
            button(label, { .command = MakerCommand::SelectLayer, .layer = layer - 1 },
                true, state.layer == layer - 1 ? Tone::Accent : Tone::Normal);
        }
    } else {
        const auto& properties = state.propertyPanel;
        if (!properties.cell || !properties.tile) {
            inspector.note("Click a placed tile to inspect its properties.");
        } else {
            inspector.heading(makerTileGuide::name(*properties.tile, state.overworld));
            inspector.note("Layer " + std::to_string(properties.cell->z + 1) + "  \xc2\xb7  X " +
                std::to_string(properties.cell->x) + "  Y " + std::to_string(properties.cell->y));
            if (properties.gateStartOpen) {
                inspector.heading("Initial state");
                button(*properties.gateStartOpen ? "Starts Open" : "Starts Closed",
                    { .command = MakerCommand::ToggleGateStartOpen }, true,
                    *properties.gateStartOpen ? Tone::Accent : Tone::Normal);
            } else if (properties.lockPlateStartEnabled) {
                inspector.heading("Initial state");
                button(*properties.lockPlateStartEnabled ? "Starts Enabled" : "Starts Disabled",
                    { .command = MakerCommand::ToggleLockPlateStartEnabled }, true,
                    *properties.lockPlateStartEnabled ? Tone::Accent : Tone::Normal);
            } else if (properties.minecartDirection) {
                inspector.heading("Launch direction");
                constexpr std::array names { "North", "East", "South", "West" };
                const auto directionControl = [&](uint8_t direction) {
                    const bool allowed = std::ranges::find(properties.minecartDirections, direction) !=
                        properties.minecartDirections.end();
                    return makeAction(names[direction],
                        { .command = MakerCommand::SetMinecartDirection, .minecartDirection = direction },
                        allowed, properties.minecartDirection == direction ? Tone::Accent : Tone::Normal);
                };
                inspector.actionRow(std::array { directionControl(0), directionControl(1) });
                inspector.actionRow(std::array { directionControl(2), directionControl(3) });
                inspector.note(properties.minecartRoute.empty()
                    ? "Connect rails to this stop to see the route." : properties.minecartRoute);
            } else if (*properties.tile == TileType::Elevator) {
                if (!properties.elevatorStops.empty()) inspector.note(properties.elevatorStops);
                if (properties.elevatorStopOnLayer) {
                    inspector.heading("Active layer " + std::to_string(state.layer + 1));
                    button(*properties.elevatorStopOnLayer ? "Remove Stop" : "Add Stop",
                        { .command = MakerCommand::ToggleElevatorStop });
                } else {
                    inspector.note("The starting layer is always a stop. Select another layer to edit its stop.");
                }
            } else {
                inspector.note("This tile has no adjustable properties.");
            }
        }
    }

    std::vector<UiControlRegion> regions;
    regions.reserve(toolbar.size());
    for (const auto& control : toolbar) regions.push_back({ commandId(control.command), control.bounds, control.enabled });
    const auto interaction = inspector.resolve(layers ? "Maker Layers" : "Maker Properties",
        layers ? "maker.layers.scroll" : "maker.properties.scroll", regions, focus, {
            .navigation = input.navigation, .wheelY = input.wheelY, .wheelPosition = input.wheelPosition,
            .interactive = input.interactive,
        });
    MakerViewAction result { .command = drawToolbar(ui, layout, state, toolbar, interaction, true) };
    if (const auto activated = inspector.draw(interaction)) {
        const auto action = std::ranges::find(actions, activated, &std::pair<UiControlId, MakerViewAction>::first);
        if (action != actions.end()) result = action->second;
    }
    if (input.interactive && !layers && !state.linkMarkers.empty()) {
        drawLinkOverlay(ui, state.linkViewport.value_or(layout.board), scale, {}, state.linkMarkers);
    }
    return result;
}

MakerViewAction drawMakerProblemsPanel(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input,
    std::span<const ToolbarControl> toolbar, UiControlId& focus, UiScroll& scroll)
{
    constexpr UiControlId firstProblemId = 100;
    constexpr float rowHeight = 64.0f;
    constexpr float rowGap = 5.0f;
    constexpr float rowStride = rowHeight + rowGap;
    const float scale = layout.scale;
    const Vec2 start = layout.palette.position + Vec2 { 10.0f, 40.0f } * scale;
    const UiRect list { start,
        { std::max(0.0f, layout.palette.size.x - 29.0f * scale),
            std::max(0.0f, layout.palette.size.y - 43.0f * scale) } };
    const auto mode = std::ranges::find(toolbar, MakerCommand::ToggleOverworldMode, &ToolbarControl::command);
    const float headerWidth = std::max(0.0f, mode->bounds.position.x - start.x - 6.0f * scale);
    const auto count = state.problems.size();
    const std::string title = count == 0 ? "Problems" : "Problems (" + std::to_string(count) + ")";
    ui.pushClip(layout.palette);
    ui.text(layout.palette.position + Vec2 { 10.0f, 10.0f } * scale,
        title, ink, fitLabel(ui, title, 22.0f * scale, headerWidth));
    ui.divider({ layout.palette.position + Vec2 { 10.0f, 32.0f } * scale,
        { std::max(0.0f, layout.palette.size.x - 20.0f * scale), scale } });
    ui.popClip();

    const float listHeight = list.size.y / scale;
    scroll.setExtents(listHeight, std::max(0.0f, static_cast<float>(count) * rowStride - rowGap));
    UiNavigation navigation = input.navigation;
    if (input.interactive) {
        scroll.scrollWheel(ui, list, input.wheelY, input.wheelPosition);
        if (count != 0 && list.size.x > 0.0f && list.size.y > 0.0f && navigation.previous != navigation.next &&
            !ui.mousePressPosition() && !ui.mouseDown()) {
            const auto enabled = [](const ToolbarControl& control) {
                return control.enabled && control.bounds.size.x > 0.0f && control.bounds.size.y > 0.0f;
            };
            std::optional<std::size_t> target;
            if (focus >= firstProblemId && focus - firstProblemId < count) {
                const auto row = static_cast<std::size_t>(focus - firstProblemId);
                if (navigation.next && row + 1 < count) target = row + 1;
                if (navigation.previous && row > 0) target = row - 1;
            } else if (navigation.next) {
                const auto last = std::find_if(toolbar.rbegin(), toolbar.rend(), enabled);
                if (last != toolbar.rend() && focus == commandId(last->command)) target = 0;
            } else {
                const auto first = std::ranges::find_if(toolbar, enabled);
                if (first != toolbar.end() && focus == commandId(first->command)) target = count - 1;
            }
            if (target) {
                focus = firstProblemId + *target;
                const float rowTop = static_cast<float>(*target) * rowStride;
                scroll.reveal(rowTop, rowTop + rowHeight);
                navigation.previous = navigation.next = false;
            }
        }
        scroll.drawScrollbar(ui, "maker.problems.scroll", {
            { list.position.x + list.size.x + 5.0f * scale, list.position.y }, { 6.0f * scale, list.size.y },
        });
    }
    const auto firstVisible = std::min(count, static_cast<std::size_t>(scroll.offset() / rowStride));
    const auto endVisible = std::min(count, static_cast<std::size_t>(std::ceil((scroll.offset() + listHeight) / rowStride)));
    const auto rowBounds = [&](std::size_t index) -> UiRect {
        return { start + Vec2 { 0.0f, (static_cast<float>(index) * rowStride - scroll.offset()) * scale },
            { list.size.x, rowHeight * scale } };
    };
    UiInteraction interaction;
    if (input.interactive) {
        std::vector<UiControlRegion> regions;
        regions.reserve(toolbar.size() + endVisible - firstVisible);
        for (const auto& control : toolbar) {
            regions.push_back({ commandId(control.command), control.bounds, control.enabled });
        }
        ui.pushClip(list);
        for (std::size_t index = firstVisible; index < endVisible; ++index) {
            regions.push_back({ firstProblemId + index, ui.visibleRect(rowBounds(index)) });
        }
        ui.popClip();
        interaction = ui.resolveControls("Maker Problems", regions, focus, navigation);
        focus = interaction.focused;
    }

    MakerViewAction result { .command = drawToolbar(ui, layout, state, toolbar, interaction, true) };
    ui.pushClip(list);
    if (count == 0) ui.text(start, "No problems found.", mutedInk, 14.0f * scale);
    for (std::size_t index = firstVisible; index < endVisible; ++index) {
        const UiRect row = rowBounds(index);
        const float textWidth = row.size.x - 16.0f * scale;
        if (textWidth <= 0.0f || ui.visibleRect(row).size.y <= 0.0f) continue;
        const UiControlId id = firstProblemId + index;
        if (uiControls::button(ui, row, "", {
                .focused = interaction.focused == id, .activate = interaction.activated == id,
                .contentScale = 0.72f * scale,
            })) {
            result = { .command = MakerCommand::JumpToProblem, .problem = index };
        }
        const auto& problem = state.problems[index];
        const float textSize = 14.0f * scale;
        const auto& lines = ui.wrapText(problem.message, textWidth, textSize);
        ui.pushClip(row);
        for (std::size_t line = 0; line < std::min<std::size_t>(2, lines.size()); ++line) {
            const std::string label = line == 1 && lines.size() > 2 ? lines[line] + "\xe2\x80\xa6" : lines[line];
            ui.text(row.position + Vec2 { 8.0f, 7.0f + static_cast<float>(line) * 17.0f } * scale,
                label, errorInk, fitLabel(ui, label, textSize, textWidth));
        }
        std::string location = problem.screen ? "Screen " + std::to_string(*problem.screen) : "Board";
        if (problem.cell) {
            location += "  \xc2\xb7  X " + std::to_string(problem.cell->x) + "  Y " + std::to_string(problem.cell->y) +
                "  \xc2\xb7  Layer " + std::to_string(problem.cell->z + 1);
        }
        ui.text(row.position + Vec2 { 8.0f, 44.0f } * scale,
            location, mutedInk, fitLabel(ui, location, 13.0f * scale, textWidth));
        ui.popClip();
    }
    ui.popClip();
    return result;
}

} // namespace

MakerLayout MakerView::layout(Vec2 viewport, float scale)
{
    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = 1.0f;
    }
    const float width = std::max(0.0f, viewport.x / scale);
    const float height = std::max(0.0f, viewport.y / scale);
    const float toolbar = std::min(height,
        width >= wideToolbarWidth ? wideToolbarHeight : compactToolbarHeight);
    const float footer = std::min(height - toolbar, footerHeight);
    const float innerHeight = std::max(0.0f, height - toolbar - footer - margin * 2.0f);
    const float paletteWidth = std::min(width * 0.45f,
        std::clamp(width * 0.22f, 202.0f, 268.0f));
    const float boardLeft = std::min(width, paletteWidth + margin * 2.0f);
    const auto pixels = [scale](UiRect rect) -> UiRect {
        return { rect.position * scale, rect.size * scale };
    };
    return {
        .toolbar = pixels({ {}, { width, toolbar } }),
        .palette = pixels({ { margin, toolbar + margin }, { paletteWidth, innerHeight } }),
        .board = pixels({ { boardLeft, toolbar + margin },
            { std::max(0.0f, width - boardLeft - margin), innerHeight } }),
        .footer = pixels({ { 0.0f, height - footer }, { width, footer } }),
        .scale = scale,
    };
}

MakerLayout MakerView::playtestLayout(Vec2 viewport, float scale)
{
    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = 1.0f;
    }
    const float width = std::max(0.0f, viewport.x);
    const float height = std::max(0.0f, viewport.y);
    const float toolbar = std::min(height, wideToolbarHeight * scale);
    return {
        .toolbar = { {}, { width, toolbar } },
        .board = { { 0.0f, toolbar }, { width, height - toolbar } },
        .scale = scale,
    };
}

bool MakerView::focusTileSearch()
{
    if (searchHitBounds_.size.x <= 0.0f || searchHitBounds_.size.y <= 0.0f) {
        return false;
    }
    searchFocused_ = true;
    searchEdit_.selectAll();
    return true;
}

bool MakerView::handleTileSearchPress(UiContext& ui, Vec2 position, bool extendSelection)
{
    if (searchHitBounds_.size.x <= 0.0f || searchHitBounds_.size.y <= 0.0f) {
        return false;
    }
    // Use the last drawn field so the rest of this event batch reaches the
    // correct text input target.
    searchPressHandled_ = true;
    if (ui.contains(searchHitBounds_, position)) {
        searchFocused_ = true;
        searchField_.placeCaret(ui, searchBounds_, searchEdit_, searchTextSize_,
            position, extendSelection);
        return true;
    }
    const bool wasFocused = searchFocused_;
    blurTileSearch();
    return wasFocused;
}

void MakerView::blurTileSearch()
{
    searchFocused_ = false;
    searchEdit_.clearComposition();
}

void MakerView::drawTileSearch(UiContext& ui, const MakerLayout& layout, const MakerViewInput& input)
{
    const float scale = layout.scale;
    ui.pushClip(layout.palette);
    ui.text(layout.palette.position + Vec2 { 12.0f, 10.0f } * scale, "Tiles", ink, 23.0f * scale);
    ui.divider({ layout.palette.position + Vec2 { 10.0f, 42.0f } * scale,
        { std::max(0.0f, layout.palette.size.x - 20.0f * scale), scale } });
    searchBounds_ = {
        layout.palette.position + Vec2 { 10.0f, searchTop } * scale,
        { std::max(0.0f, layout.palette.size.x - 20.0f * scale), searchHeight * scale },
    };
    searchHitBounds_ = ui.visibleRect(searchBounds_);
    searchTextSize_ = 16.0f * scale;
    const bool pressHandled = std::exchange(searchPressHandled_, false);
    if (searchHitBounds_.size.x <= 0.0f || searchHitBounds_.size.y <= 0.0f) {
        blurTileSearch();
        textCaret_ = {};
    } else {
        if (const auto press = ui.mousePressPosition(); press && !pressHandled && input.interactive) {
            if (ui.contains(searchHitBounds_, *press)) {
                searchFocused_ = true;
            } else {
                blurTileSearch();
            }
        }
        textCaret_ = searchField_.draw(ui, "maker.palette.search", searchBounds_, searchEdit_, {
            .focused = searchFocused_ && input.interactive,
            .enabled = input.interactive,
            .extendSelection = input.extendTextSelection,
            .pressAlreadyHandled = pressHandled,
            .placeholder = "Search tiles (Ctrl+F)",
            .textSize = searchTextSize_,
        }).caret;
    }
    ui.popClip();
}

bool MakerView::closeToolbarOverflow()
{
    return std::exchange(toolbarOverflowOpen_, false);
}

MakerViewAction MakerView::drawToolbarOverflow(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input)
{
    std::array<ToolbarControl, toolbarControlCount> entries;
    std::size_t count = 0;
    const auto controls = toolbarControls(layout, state);
    for (const auto& control : controls) {
        if (control.inOverflow) entries[count++] = control;
    }
    const auto rows = std::span(entries).first(count);
    const float scale = layout.scale;
    const float inset = 8.0f * scale;
    const float pitch = 36.0f * scale;
    const auto anchor = std::ranges::find(controls, MakerCommand::ToggleToolbarOverflow,
        &ToolbarControl::command)->bounds;
    const float viewportWidth = layout.toolbar.size.x;
    const float viewportHeight = layout.footer.position.y + layout.footer.size.y;
    const float panelTop = anchor.position.y + anchor.size.y + 4.0f * scale;
    const float panelWidth = std::min(220.0f * scale, std::max(0.0f, viewportWidth - 2.0f * inset));
    const float panelHeight = std::min(pitch * static_cast<float>(count) + 2.0f * inset,
        std::max(0.0f, viewportHeight - panelTop - inset));
    if (panelWidth <= 3.0f * inset || panelHeight <= 2.0f * inset || rows.empty()) {
        closeToolbarOverflow();
        return {};
    }
    const UiRect panel {
        { std::clamp(anchor.position.x + anchor.size.x - panelWidth, inset, viewportWidth - panelWidth - inset),
            panelTop },
        { panelWidth, panelHeight },
    };
    if (const auto press = ui.mousePressPosition(); press && !ui.contains(panel, *press)) {
        const std::array backdrop { UiControlRegion { 1, { {}, { viewportWidth, viewportHeight } }, false, false } };
        (void)ui.resolveControls("Maker toolbar overflow", backdrop, 0);
        closeToolbarOverflow();
        ui.cancelPointerInteraction();
        return {};
    }

    ui.panel(panel);
    const UiRect list { panel.position + Vec2 { inset, inset },
        { panelWidth - 3.0f * inset, panelHeight - 2.0f * inset } };
    toolbarOverflowScroll_.setExtents(list.size.y / scale, pitch / scale * static_cast<float>(count));
    toolbarOverflowScroll_.scrollWheel(ui, panel, input.wheelY, input.wheelPosition);
    toolbarOverflowScroll_.drawScrollbar(ui, "maker.toolbar.scroll", {
        list.position + Vec2 { list.size.x + 3.0f * scale, 0.0f }, { 5.0f * scale, list.size.y },
    });

    UiNavigation navigation = input.navigation;
    if (!ui.mouseDown() && !ui.mousePressPosition() && navigation.previous != navigation.next) {
        const auto current = std::ranges::find_if(rows, [&](const ToolbarControl& row) {
            return row.enabled && commandId(row.command) == toolbarOverflowFocus_;
        });
        const auto enabled = std::ranges::find_if(rows, &ToolbarControl::enabled);
        if (enabled != rows.end()) {
            auto index = current == rows.end() ? enabled - rows.begin() : current - rows.begin();
            if (current != rows.end()) {
                const auto size = static_cast<std::ptrdiff_t>(rows.size());
                const auto direction = navigation.next ? 1 : -1;
                do {
                    index = (index + size + direction) % size;
                } while (!rows[static_cast<std::size_t>(index)].enabled);
            }
            toolbarOverflowFocus_ = commandId(rows[static_cast<std::size_t>(index)].command);
            const float top = static_cast<float>(index) * pitch / scale;
            toolbarOverflowScroll_.reveal(top, top + pitch / scale);
        }
        navigation.previous = navigation.next = false;
    }

    std::array<UiControlRegion, toolbarControlCount> regions;
    const float offset = toolbarOverflowScroll_.offset() * scale;
    ui.pushClip(list);
    for (std::size_t i = 0; i < count; ++i) {
        auto& row = rows[i];
        row.bounds = { list.position + Vec2 { 0.0f, static_cast<float>(i) * pitch - offset },
            { list.size.x, pitch - 4.0f * scale } };
        if (row.command == MakerCommand::LayerDown) row.label = "Previous Layer";
        if (row.command == MakerCommand::LayerUp) row.label = "Next Layer";
        if (row.command == MakerCommand::Help) row.label = "Help";
        regions[i] = { commandId(row.command), ui.visibleRect(row.bounds), row.enabled };
    }
    const auto interaction = ui.resolveControls("Maker toolbar overflow", std::span(regions).first(count),
        toolbarOverflowFocus_, navigation);
    toolbarOverflowFocus_ = interaction.focused;
    MakerCommand command = MakerCommand::None;
    for (const auto& row : rows) {
        if (ui.visibleRect(row.bounds).size.y > 0.0f && drawToolbarControl(ui, row, interaction, true, scale)) {
            command = row.command;
        }
    }
    ui.popClip();
    if (command != MakerCommand::None) {
        closeToolbarOverflow();
        ui.cancelPointerInteraction();
    }
    return { .command = command };
}

MakerViewAction MakerView::draw(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input)
{
    drawChrome(ui, layout);
    if (state.area.outline && !state.linkInspector) {
        drawAreaOutline(ui, state.linkViewport.value_or(layout.board), layout.scale, *state.area.outline);
    }
    if (!input.interactive || layout.toolbar.size.x >= toolbarOverflowWidth * layout.scale) closeToolbarOverflow();
    MakerViewInput editorInput = input;
    editorInput.interactive = input.interactive && !toolbarOverflowOpen_;
    if (editorInput.interactive && state.boardCursorOutline) {
        drawAreaOutline(ui, state.linkViewport.value_or(layout.board), layout.scale, *state.boardCursorOutline);
    }
    const MakerViewAction result = drawEditorControls(ui, layout, state, editorInput);
    if (editorInput.interactive && state.problemMarker) {
        const std::array markers { MakerLinkMarker { *state.problemMarker, errorInk, true } };
        drawLinkOverlay(ui, state.linkViewport.value_or(layout.board), layout.scale, {}, markers);
    }
    const float scale = layout.scale;
    const float footerY = layout.footer.position.y + 7.0f * scale;
    const float footerInset = margin * scale;
    const float footerText = 15.0f * scale;
    const bool inspecting = state.panel == MakerPanel::Properties;
    float leftWidth = 0.0f;
    if (state.overworld) {
        leftWidth = std::min(258.0f * scale, layout.footer.size.x * 0.43f);
        const float screenLeft = (margin + 121.0f) * scale;
        ui.pushClip({ { screenLeft, layout.footer.position.y },
            { std::max(0.0f, leftWidth - screenLeft), layout.footer.size.y } });
        ui.text({ screenLeft, footerY }, "Screen " + std::to_string(state.overworldScreen.x) + ", " +
            std::to_string(state.overworldScreen.y), accentInk, footerText);
        ui.popClip();
    } else if (state.status.empty() && !inspecting) {
        std::string toolLabel = state.linkInspector ? "Connect" : state.erasing ? "Erase" :
            std::string(makerTileGuide::name(state.brush, state.overworld));
        if (!state.linkInspector && !state.erasing && state.quarterTurns != 0) {
            toolLabel += "  " + std::to_string(state.quarterTurns * 90) + "\xc2\xb0";
        }
        leftWidth = std::min(210.0f * scale, layout.footer.size.x * 0.32f);
        ui.pushClip({ layout.footer.position, { leftWidth, layout.footer.size.y } });
        ui.text({ footerInset, footerY }, toolLabel, accentInk, footerText);
        ui.popClip();
    }
    std::string sessionLabel = state.draftName.empty() ? "Untitled" : std::string(state.draftName);
    if (state.unsavedChanges) {
        sessionLabel += "  \xc2\xb7  Unsaved";
    }
    if (state.hovered) {
        sessionLabel = "X " + std::to_string(state.hovered->x) +
            "  Y " + std::to_string(state.hovered->y) + "  \xc2\xb7  " + sessionLabel;
    }
    const float sessionWidth = ui.measureText(sessionLabel, footerText).x;
    const float rightWidth = std::min(sessionWidth + footerInset * 2.0f, layout.footer.size.x * 0.52f);
    const float statusWidth = std::max(0.0f, layout.footer.size.x - leftWidth - rightWidth);
    ui.pushClip({ { leftWidth, layout.footer.position.y }, { statusWidth, layout.footer.size.y } });
    const std::string_view defaultStatus = !state.boardCursorHint.empty() ? state.boardCursorHint :
        inspecting ? "Click a tile to inspect its properties" :
        state.linkInspector ? "Click a tile to inspect its connections" :
        state.area.mode == MakerAreaViewState::Mode::Rectangle ? "Drag a rectangle on the active layer" :
        state.area.mode == MakerAreaViewState::Mode::FloodFill ? "Click a connected region to fill it" :
        state.area.mode == MakerAreaViewState::Mode::Paste ? "Click to paste. Esc cancels." :
        state.erasing ? "Drag to erase" : state.placingSelector ? "Click to place a Level Selector" :
        state.overworld ? "Paint beyond the edge to add a screen" : "Drag to paint";
    ui.text({ std::max(leftWidth, footerInset), footerY }, state.status.empty() ? defaultStatus : state.status,
        mutedInk, footerText);
    ui.popClip();
    ui.pushClip({ { layout.footer.size.x - rightWidth, layout.footer.position.y },
        { rightWidth, layout.footer.size.y } });
    ui.text({ layout.footer.size.x - footerInset - sessionWidth, footerY },
        sessionLabel, mutedInk, footerText);
    ui.popClip();
    if (toolbarOverflowOpen_) return drawToolbarOverflow(ui, layout, state, input);
    if (result.command == MakerCommand::ToggleToolbarOverflow) {
        toolbarOverflowFocus_ = 0;
        toolbarOverflowScroll_ = {};
        toolbarOverflowOpen_ = true;
        blurTileSearch();
        searchHitBounds_ = {};
        ui.cancelPointerInteraction();
        return {};
    }
    return input.interactive ? result : MakerViewAction {};
}

MakerViewAction MakerView::drawEditorControls(UiContext& ui, const MakerLayout& workspace,
    const MakerViewState& state, const MakerViewInput& input)
{
    const float scale = workspace.scale;
    auto controls = toolbarControls(workspace, state);
    if (auto more = std::ranges::find(controls, MakerCommand::ToggleToolbarOverflow, &ToolbarControl::command);
        more != controls.end()) more->accented = toolbarOverflowOpen_;
    auto layout = workspace;
    layout.palette.size.y = std::max(0.0f,
        layout.palette.size.y - (sidebarTabHeight + sidebarTabInset * 2.0f) * scale);
    ui.divider({ { workspace.palette.position.x, workspace.palette.position.y + layout.palette.size.y },
        { workspace.palette.size.x, scale } });
    float titleEnd = layout.toolbar.size.x;
    for (const auto& control : controls) {
        if (control.bounds.size.x > 0.0f && control.bounds.position.y == 14.0f * scale) {
            titleEnd = std::min(titleEnd, control.bounds.position.x);
        }
    }
    drawTitle(ui, layout, titleEnd, state.overworld
        ? (titleEnd >= 210.0f * scale ? "Overworld Maker" : "Overworld")
        : (titleEnd >= 210.0f * scale ? "Sokoban Maker" : "Maker"));
    if (state.panel != MakerPanel::Palette || state.linkInspector) {
        blurTileSearch();
        searchHitBounds_ = {};
        textCaret_ = {};
        searchPressHandled_ = false;
        MakerViewAction action;
        if (state.panel == MakerPanel::Problems) {
            action = drawMakerProblemsPanel(ui, layout, state, input, controls, focus_, problemsScroll_);
            if (input.interactive && state.linkInspector) {
                drawLinkOverlay(ui, state.linkViewport.value_or(layout.board), scale, state.linkLines, state.linkMarkers);
            }
        } else if (state.panel == MakerPanel::Palette) {
            action = drawLinkInspector(ui, layout, state, input, controls, focus_, linkScroll_);
        } else {
            action = drawMakerDetailsPanel(ui, layout, state, input, controls, focus_,
                state.panel == MakerPanel::Layers ? layerScroll_ : propertyScroll_);
        }
        if (input.interactive && !state.placementHint.empty()) {
            drawPlacementHint(ui, layout.board, scale, state.placementHint, false);
        }
        return action;
    }
    drawTileSearch(ui, layout, input);
    const bool showSelectionActions = state.area.hasSelection &&
        (!state.area.mode || state.area.mode == MakerAreaViewState::Mode::Rectangle);
    const float areaHeight = showSelectionActions ? 2.0f * (areaRowHeight + areaRowGap) :
        state.area.mode ? areaRowHeight + areaRowGap : 0.0f;
    const float paletteListTop = listTop + areaHeight;
    const float listAndGuideHeight = std::max(0.0f, layout.palette.size.y - (paletteListTop + 11.0f) * scale);
    const float basicsHeight = (groupHeight + static_cast<float>(basics.size()) * (tileHeight + tileGap)) * scale;
    const float minimumListHeight = std::clamp(listAndGuideHeight - compactGuideHeight * scale, 84.0f * scale,
        basicsHeight);
    const float guideSize = std::min(guideHeight * scale, std::max(0.0f, listAndGuideHeight - minimumListHeight));
    const UiRect list {
        layout.palette.position + Vec2 { 10.0f, paletteListTop } * scale,
        { std::max(0.0f, layout.palette.size.x - 28.0f * scale),
            listAndGuideHeight - guideSize },
    };
    const UiRect scrollbar {
        { list.position.x + list.size.x + 5.0f * scale, list.position.y },
        { 6.0f * scale, list.size.y },
    };
    const auto& query = searchEdit_.text();
    if (renderedQuery_ != query || renderedOverworld_ != state.overworld) {
        paletteScroll_ = {};
        renderedQuery_ = query;
        renderedOverworld_ = state.overworld;
        collapsedSearchSections_ = {};
    }
    const bool searching = query.find_first_not_of(" \t") != std::string::npos;
    static_assert(std::tuple_size_v<PaletteSections> == groups.size());
    auto& collapsedSections = searching ? collapsedSearchSections_ : collapsedPaletteSections_;
    std::array<PaletteRow, paletteRowCapacity> rows;
    std::size_t rowCount = 0;
    float y = list.position.y;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (!state.overworld && index == overworldSection) continue;
        bool hasMatches = false;
        const auto appendTile = [&](UiControlId id, std::optional<TileType> tile, bool selector) {
            if (!hasMatches) {
                rows[rowCount++] = { firstPaletteSectionId + index,
                    { { list.position.x, y }, { list.size.x, groupHeight * scale } }, index, std::nullopt };
                y += groupHeight * scale;
                hasMatches = true;
            }
            if (collapsedSections[index]) return;
            rows[rowCount++] = { id,
                { { list.position.x, y }, { list.size.x, tileHeight * scale } }, index, tile, selector };
            y += (tileHeight + tileGap) * scale;
        };
        const auto tiles = state.overworld && index == charactersSection
            ? std::span<const TileType> { overworldCharacters } : groups[index].tiles;
        for (TileType tile : tiles) {
            if (state.overworld && tile == TileType::End) continue;
            if (searching && !makerTileGuide::matches(tile, groups[index].label, query, state.overworld)) continue;
            appendTile(tileId(tile), tile, false);
        }
        if (index == overworldSection) {
            const auto& entry = makerTileGuide::levelSelector;
            const std::array fields { makerTileGuide::levelSelectorName, groups[index].label,
                entry.description, entry.placementHint, entry.aliases };
            if (!searching || matchesSearchTerms(fields, query)) appendTile(selectorTileId, std::nullopt, true);
        }
        if (hasMatches) y += groupGap * scale;
    }
    // Keep the scroll offset in logical units across monitor scale changes.
    paletteScroll_.setExtents(list.size.y / scale, (y - list.position.y) / scale);
    if (input.interactive) {
        paletteScroll_.scrollWheel(ui, layout.palette, input.wheelY, input.wheelPosition);
    }

    std::array<UiControlRegion, controls.size() + paletteRowCapacity> regions;
    std::size_t regionCount = 0;
    for (const auto& control : controls) {
        regions[regionCount++] = { commandId(control.command), control.bounds, control.enabled };
    }
    UiNavigation navigation = searchFocused_ ? UiNavigation {} : input.navigation;
    const auto paletteRows = std::span(rows).first(rowCount);
    const bool navigationMoved = navigation.previous != navigation.next;
    if (input.interactive && navigationMoved && !ui.mousePressPosition() && !ui.mouseDown()) {
        // Resolve the complete palette's navigation before its clipped
        // pointer regions are submitted, so offscreen rows can receive focus.
        std::array<UiControlId, controls.size() + paletteRowCapacity> focusOrder;
        std::size_t focusCount = 0;
        for (const auto& control : controls) {
            if (control.enabled && control.bounds.size.x > 0.0f) focusOrder[focusCount++] = commandId(control.command);
        }
        for (const auto& row : paletteRows) focusOrder[focusCount++] = row.id;
        const auto order = std::span(focusOrder).first(focusCount);
        const auto current = std::ranges::find(order, focus_);
        if (!order.empty()) {
            const auto index = current == order.end() ? std::size_t { 0 } : static_cast<std::size_t>(current - order.begin());
            const auto next = navigation.next ? (index + 1) % order.size() : (index + order.size() - 1) % order.size();
            const auto target = std::ranges::find(paletteRows, order[next], &PaletteRow::id);
            if (target != paletteRows.end()) {
                const float top = (target->bounds.position.y - list.position.y) / scale;
                paletteScroll_.reveal(top, top + target->bounds.size.y / scale);
            }
            focus_ = order[next];
        }
        navigation.previous = navigation.next = false;
    }
    if (input.interactive) paletteScroll_.drawScrollbar(ui, "maker.palette.scroll", scrollbar);
    const float scroll = paletteScroll_.offset() * scale;
    ui.pushClip(list);
    for (auto& row : paletteRows) {
        row.bounds.position.y -= scroll;
        regions[regionCount++] = { row.id, ui.visibleRect(row.bounds) };
    }
    ui.popClip();

    UiInteraction interaction;
    if (input.interactive) {
        interaction = ui.resolveControls("Maker", std::span(regions).first(regionCount), focus_, navigation);
        focus_ = interaction.focused;
    }
    if (palettePointer_ != ui.mousePosition() || ui.mousePressPosition() || searchFocused_) {
        paletteKeyboardFocus_ = false;
    }
    if (input.interactive && navigationMoved) paletteKeyboardFocus_ = true;
    palettePointer_ = ui.mousePosition();
    MakerViewAction result { .command = drawToolbar(ui, layout, state, controls, interaction, !searchFocused_) };

    ui.pushClip(list);
    if (paletteRows.empty()) {
        ui.text(list.position + Vec2 { 2.0f, 6.0f } * scale, "No matching tiles", mutedInk, 15.0f * scale);
    }
    for (const auto& [id, bounds, section, tile, selector] : paletteRows) {
        const auto visible = ui.visibleRect(bounds);
        if (visible.size.y <= 0.0f) continue;
        if (!tile && !selector) {
            const bool collapsed = collapsedSections[section];
            const bool toggled = uiControls::button(ui, interaction, id, bounds, "",
                uiControls::ButtonTone::Normal);
            ui.text(bounds.position + Vec2 { 9.0f, 4.0f } * scale,
                groups[section].label, ink, 15.0f * scale);
            ui.text(bounds.position + Vec2 { bounds.size.x - 20.0f * scale, 4.0f * scale },
                collapsed ? "\xe2\x96\xb8" : "\xe2\x96\xbe", accentInk, 13.0f * scale);
            if (toggled) collapsedSections[section] = !collapsed;
            continue;
        }
        const bool selected = !state.erasing &&
            (selector ? state.placingSelector : !state.placingSelector && selectedTile(*tile, state.brush));
        if (uiControls::button(ui, bounds, "", {
                .tone = selected ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
                .focused = input.interactive && paletteKeyboardFocus_ && focus_ == id,
                .activate = interaction.activated == id,
                .contentScale = 0.72f * scale,
            })) {
            if (selector) result.command = MakerCommand::PaintSelector;
            else result.tile = tile;
        }
        const UiRect icon { bounds.position + Vec2 { 4.0f, 2.0f } * scale, Vec2 { 28.0f, 28.0f } * scale };
        if (selector) drawSelectorThumbnail(ui, icon);
        else drawTileThumbnail(ui, icon, *tile, 16.0f * scale, input);
        const auto label = selector ? makerTileGuide::levelSelectorName : makerTileGuide::name(*tile, state.overworld);
        ui.text(bounds.position + Vec2 { 37.0f, 6.0f } * scale, label, ink,
            fitLabel(ui, label, 16.0f * scale, std::max(0.0f, bounds.size.x - 40.0f * scale)));
    }
    ui.popClip();

    TileType inspected = state.erasing ? TileType::Air : state.brush;
    bool inspectingSelector = state.placingSelector && !state.erasing;
    if (state.hovered) {
        inspected = state.boardTile.value_or(inspected);
        if (state.boardTile) inspectingSelector = false;
    } else if (input.interactive && !searchFocused_) {
        const UiControlId inspectId = paletteKeyboardFocus_ ? interaction.focused : interaction.hovered;
        const auto inspectedRow = std::ranges::find(paletteRows, inspectId, &PaletteRow::id);
        if (inspectedRow != paletteRows.end() && (inspectedRow->tile || inspectedRow->selector)) {
            inspected = inspectedRow->tile.value_or(inspected);
            inspectingSelector = inspectedRow->selector;
        }
    }
    if (guideSize > 0.0f) {
        drawTileGuide(ui, { { list.position.x, list.position.y + list.size.y },
            { layout.palette.size.x - 20.0f * scale, guideSize } }, scale, inspected, input,
            state.overworld, inspectingSelector);
    }
    if (!state.placementHint.empty()) {
        drawPlacementHint(ui, layout.board, scale, state.placementHint, state.placementBlocked);
    }
    return result;
}

MakerCommand MakerView::drawPlaytest(UiContext& ui, const MakerLayout& layout, bool solved,
    std::string_view returnLabel, std::string_view hint)
{
    blurTileSearch();
    searchHitBounds_ = {};
    textCaret_ = {};
    searchPressHandled_ = false;
    const float scale = layout.scale;
    const float width = layout.toolbar.size.x;
    const float buttonWidth = std::min(176.0f * scale, width * 0.55f);
    const ToolbarControl returnControl {
        MakerCommand::ReturnToMaker,
        { { width - margin * scale - buttonWidth, 14.0f * scale },
            { buttonWidth, 34.0f * scale } },
        returnLabel, true, true,
    };
    ui.pushClip(layout.toolbar);
    ui.rect(layout.toolbar, chrome);
    ui.divider({ { 0.0f, std::max(0.0f, layout.toolbar.size.y - scale) }, { width, scale } });
    const std::string_view title = solved ? "Draft solved" : "Test Play";
    drawTitle(ui, layout, returnControl.bounds.position.x, title, solved ? accentInk : ink);
    const float hintLeft = ui.measureText(title, 24.0f * scale).x + 36.0f * scale;
    const float hintWidth = returnControl.bounds.position.x - 12.0f * scale - hintLeft;
    if (!hint.empty() && hintWidth > 0.0f) {
        const UiRect hintBounds { { hintLeft, 0.0f }, { hintWidth, layout.toolbar.size.y } };
        ui.pushClip(hintBounds);
        ui.centeredText(hintBounds, hint, mutedInk, 16.0f * scale);
        ui.popClip();
    }
    const std::array regions {
        UiControlRegion { commandId(returnControl.command), returnControl.bounds, true, false },
    };
    const UiInteraction interaction = ui.resolveControls("Maker Test Play", regions, 0);
    const bool returnToMaker = drawToolbarControl(ui, returnControl, interaction, false, scale);
    ui.popClip();
    return returnToMaker ? returnControl.command : MakerCommand::None;
}

void MakerView::beginHelp()
{
    helpScroll_ = {};
}

void MakerView::beginOverworldMap()
{
    overworldMapFocus_ = 0;
    overworldMapMove_.reset();
    overworldMapDelete_.reset();
}

bool MakerView::cancelOverworldMapAction()
{
    if (!overworldMapMove_ && !overworldMapDelete_) return false;
    beginOverworldMap();
    return true;
}

MakerOverworldMapAction MakerView::drawOverworldMap(UiContext& ui, Vec2 viewport, float scale,
    const MakerOverworldMapState& state, const MakerViewInput& input)
{
    using Command = MakerOverworldMapAction::Command;
    using Tone = uiControls::ButtonTone;
    constexpr UiControlId closeId = 1;
    constexpr UiControlId moveId = 2;
    constexpr UiControlId deleteId = 3;
    constexpr UiControlId undoId = 4;
    constexpr UiControlId redoId = 5;
    constexpr UiControlId firstSlotId = 6;

    scale = std::min({ scale, viewport.x / 440.0f, viewport.y / 344.0f });
    if (!std::isfinite(scale) || scale <= 0.0f) return {};
    const float inset = 18.0f * scale;
    const float gap = 6.0f * scale;
    const float buttonHeight = 32.0f * scale;
    const Vec2 size {
        std::min(780.0f * scale, viewport.x - 2.0f * margin * scale),
        std::min(620.0f * scale, viewport.y - 2.0f * margin * scale),
    };
    const UiRect panel { (viewport - size) * 0.5f, size };
    const float right = panel.position.x + size.x - inset;
    const float bottom = panel.position.y + size.y - inset;
    const float contentWidth = size.x - 2.0f * inset;
    const UiRect closeButton { { right - 115.0f * scale, panel.position.y + 12.0f * scale },
        { 115.0f * scale, buttonHeight } };
    const UiRect map {
        panel.position + Vec2 { inset, 84.0f * scale },
        { contentWidth, size.y - 158.0f * scale },
    };
    const float actionWidth = std::min(136.0f * scale, (contentWidth - 3.0f * gap) / 4.0f);
    const auto actionButton = [&](int index) -> UiRect {
        return { { map.position.x + static_cast<float>(index) * (actionWidth + gap), bottom - buttonHeight },
            { actionWidth, buttonHeight } };
    };

    const auto active = std::ranges::find(state.screens, state.activeScreen, &OverworldScreenSpec::id);
    if (overworldMapMove_ && (active == state.screens.end() || active->id != overworldMapMove_->screen ||
            active->slot != overworldMapMove_->origin)) {
        overworldMapMove_.reset();
        overworldMapFocus_ = 0;
    }
    if (overworldMapDelete_ && (state.screens.size() <= 1 || active == state.screens.end() ||
            active->id != *overworldMapDelete_)) {
        overworldMapDelete_.reset();
        overworldMapFocus_ = 0;
    }
    const bool moving = overworldMapMove_.has_value();
    const bool deleting = overworldMapDelete_.has_value();
    const bool canMove = active != state.screens.end();
    const bool canDelete = canMove && state.screens.size() > 1 && !moving;

    // Leave room beyond the occupied extent so edge screens can be moved outward.
    const auto overviewRange = [](int minimum, int maximum) {
        const auto span = static_cast<int64_t>(maximum) - minimum + 1;
        const auto padding = span >= 3 ? 1 : 2;
        const auto low = std::max<int64_t>(std::numeric_limits<int>::min(), static_cast<int64_t>(minimum) - padding);
        const auto high = std::min<int64_t>(std::numeric_limits<int>::max(), static_cast<int64_t>(maximum) + padding);
        return std::pair { static_cast<int>(low), static_cast<int>(high) };
    };
    OverworldSlot gridStart;
    int columns = 0;
    int rows = 0;
    if (!state.screens.empty()) {
        const auto [left, rightmost] = std::ranges::minmax_element(state.screens, {},
            [](const OverworldScreenSpec& screen) { return screen.slot.x; });
        const auto [top, bottommost] = std::ranges::minmax_element(state.screens, {},
            [](const OverworldScreenSpec& screen) { return screen.slot.y; });
        const auto [minX, maxX] = overviewRange(left->slot.x, rightmost->slot.x);
        const auto [minY, maxY] = overviewRange(top->slot.y, bottommost->slot.y);
        gridStart = { minX, minY };
        columns = maxX - minX + 1;
        rows = maxY - minY + 1;
    }
    const float mapInset = 8.0f * scale;
    const float pitch = columns > 0 && rows > 0 ? std::min({ 68.0f * scale,
        (map.size.x - 2.0f * mapInset) / static_cast<float>(columns),
        (map.size.y - 2.0f * mapInset) / static_cast<float>(rows) }) : 0.0f;
    const Vec2 gridSize { static_cast<float>(columns) * pitch, static_cast<float>(rows) * pitch };
    const Vec2 gridOrigin = map.position + (map.size - gridSize) * 0.5f;
    const auto slotIndex = [&](OverworldSlot slot) {
        return static_cast<std::size_t>((slot.y - gridStart.y) * columns + (slot.x - gridStart.x));
    };
    const auto slotAt = [&](std::size_t index) -> OverworldSlot {
        const auto offset = static_cast<int>(index);
        return { gridStart.x + offset % columns, gridStart.y + offset / columns };
    };
    const auto slotBounds = [&](std::size_t index) -> UiRect {
        const auto offset = static_cast<int>(index);
        return {
            gridOrigin + Vec2 { static_cast<float>(offset % columns) * pitch,
                static_cast<float>(offset / columns) * pitch },
            { pitch, pitch },
        };
    };
    std::vector<OverworldScreenId> occupants(static_cast<std::size_t>(columns * rows));
    for (const auto& screen : state.screens) occupants[slotIndex(screen.slot)] = screen.id;

    std::vector<UiControlRegion> regions;
    UiInteraction interaction;
    if (!deleting && input.interactive) {
        regions.reserve(5 + (moving ? occupants.size() : state.screens.size()));
        regions.push_back({ closeId, closeButton });
        regions.push_back({ moveId, actionButton(0), canMove });
        regions.push_back({ deleteId, actionButton(1), canDelete });
        regions.push_back({ undoId, actionButton(2), state.canUndo });
        regions.push_back({ redoId, actionButton(3), state.canRedo });
        for (std::size_t index = 0; index < occupants.size(); ++index) {
            if (moving == (occupants[index] != 0)) continue;
            regions.push_back({ firstSlotId + index, slotBounds(index) });
        }
        const UiControlId initial = canMove ? firstSlotId + slotIndex(active->slot) : closeId;
        interaction = ui.resolveControls("Maker Overworld Map", regions,
            overworldMapFocus_ == 0 ? initial : overworldMapFocus_, input.navigation);
        overworldMapFocus_ = interaction.focused;
    }

    ui.rect({ {}, viewport }, { 0.012f, 0.016f, 0.017f, 0.72f });
    ui.panel(panel);
    ui.pushClip(panel);
    ui.text(panel.position + Vec2 { inset, 15.0f * scale }, "Overworld Map", ink, 24.0f * scale);
    ui.divider({ panel.position + Vec2 { inset, 53.0f * scale }, { contentWidth, scale } });
    const std::string description = moving ? "Choose an empty slot for screen " + std::to_string(active->id) + "."
        : canMove ? "Screen " + std::to_string(active->id) + " is open. Click a screen to jump."
        : "There are no screens to browse.";
    ui.text(panel.position + Vec2 { inset, 61.0f * scale }, description, mutedInk, 15.0f * scale);
    ui.rect(map, background);
    ui.pushClip(map);
    const float stroke = std::min(scale, pitch * 0.18f);
    for (int column = 0; pitch > 0.0f && column <= columns; ++column) {
        const float x = gridOrigin.x + static_cast<float>(column) * pitch;
        ui.rect({ { x, gridOrigin.y }, { stroke, gridSize.y + stroke } }, divider);
    }
    for (int row = 0; pitch > 0.0f && row <= rows; ++row) {
        const float y = gridOrigin.y + static_cast<float>(row) * pitch;
        ui.rect({ { gridOrigin.x, y }, { gridSize.x + stroke, stroke } }, divider);
    }
    const float cellInset = std::min(2.0f * scale, pitch * 0.14f);
    // Very large worlds still need space in the command buffer for the modal
    // and its controls. Draw their coverage at an overview scale and keep the
    // active, focused, and hovered cells individually resolved.
    const bool dense = state.screens.size() > 3500;
    if (dense) {
        constexpr int blockSize = 3;
        for (int row = 0; row < rows; row += blockSize) {
            const int height = std::min(blockSize, rows - row);
            for (int column = 0; column < columns; column += blockSize) {
                const int width = std::min(blockSize, columns - column);
                int count = 0;
                for (int dy = 0; dy < height; ++dy) {
                    for (int dx = 0; dx < width; ++dx) {
                        const auto index = static_cast<std::size_t>((row + dy) * columns + column + dx);
                        if (occupants[index] != 0) ++count;
                    }
                }
                if (count == 0) continue;
                const auto index = static_cast<std::size_t>(row * columns + column);
                const Vec2 position = slotBounds(index).position + Vec2 { cellInset, cellInset };
                const Vec2 block { static_cast<float>(width) * pitch, static_cast<float>(height) * pitch };
                const float density = static_cast<float>(count) / static_cast<float>(width * height);
                ui.rect({ position, block - Vec2 { 2.0f * cellInset, 2.0f * cellInset } },
                    { 0.22f, 0.30f, 0.29f, 0.22f + density * 0.78f });
            }
        }
    }
    constexpr Vec4 hoverFill { 0.30f, 0.49f, 0.46f, 1.0f };
    for (std::size_t index = 0; index < occupants.size(); ++index) {
        const auto occupant = occupants[index];
        const UiControlId id = firstSlotId + index;
        const bool highlight = interaction.hovered == id || interaction.focused == id;
        const bool selected = occupant != 0 && occupant == state.activeScreen;
        if (!highlight && (occupant == 0 || (dense && !selected))) continue;
        const UiRect cell = slotBounds(index);
        const UiRect face { cell.position + Vec2 { cellInset, cellInset },
            cell.size - Vec2 { 2.0f * cellInset, 2.0f * cellInset } };
        ui.rect(face, selected ? accentInk : highlight ? hoverFill : chrome, std::min(4.0f * scale, pitch * 0.15f));
        if (occupant != 0 && pitch >= 18.0f * scale) {
            const auto label = std::to_string(occupant);
            ui.centeredText(face, label, selected ? background : ink,
                fitLabel(ui, label, 17.0f * scale, face.size.x - 4.0f * scale));
        }
    }
    ui.popClip();
    std::string coordinates;
    const UiControlId pointed = interaction.hovered >= firstSlotId ? interaction.hovered : interaction.focused;
    if (pointed >= firstSlotId && pointed - firstSlotId < occupants.size()) {
        const auto index = static_cast<std::size_t>(pointed - firstSlotId);
        const auto slot = slotAt(index);
        coordinates = occupants[index] == 0 ? "Empty slot" : "Screen " + std::to_string(occupants[index]);
        coordinates += "  (" + std::to_string(slot.x) + ", " + std::to_string(slot.y) + ")";
    } else if (canMove) {
        coordinates = "Slot (" + std::to_string(active->slot.x) + ", " + std::to_string(active->slot.y) + ")";
    }
    const UiRect status { { map.position.x, map.position.y + map.size.y + gap }, { contentWidth, 23.0f * scale } };
    const float coordinateWidth = std::min(contentWidth, ui.measureText(coordinates, 14.0f * scale).x);
    if (!coordinates.empty()) {
        const UiRect slotStatus { { right - coordinateWidth, status.position.y }, { coordinateWidth, status.size.y } };
        ui.pushClip(slotStatus);
        ui.text(slotStatus.position, coordinates, mutedInk, 14.0f * scale);
        ui.popClip();
    }
    if (!state.status.empty()) {
        const UiRect editStatus { status.position, { std::max(0.0f, contentWidth - coordinateWidth - gap), status.size.y } };
        ui.pushClip(editStatus);
        ui.text(editStatus.position, state.status, mutedInk, 14.0f * scale);
        ui.popClip();
    }

    const auto button = [&](UiRect bounds, UiControlId id, std::string_view label, Tone tone, bool enabled) {
        return uiControls::button(ui, bounds, label, {
            .tone = tone,
            .focused = interaction.focused == id,
            .activate = interaction.activated == id,
            .enabled = enabled,
            .contentScale = 0.70f * scale,
        });
    };
    const bool close = button(closeButton, closeId, "Back to Board", Tone::Normal, true);
    const bool move = button(actionButton(0), moveId, moving ? "Cancel Move" : "Move", Tone::Accent, canMove);
    const bool remove = button(actionButton(1), deleteId, "Delete", Tone::Danger, canDelete);
    const bool undo = button(actionButton(2), undoId, "Undo", Tone::Normal, state.canUndo);
    const bool redo = button(actionButton(3), redoId, "Redo", Tone::Normal, state.canRedo);
    ui.popClip();

    if (deleting) {
        constexpr UiControlId cancelId = 1;
        constexpr UiControlId confirmId = 2;
        const Vec2 confirmSize { std::min(440.0f * scale, size.x), 184.0f * scale };
        const UiRect confirm { (viewport - confirmSize) * 0.5f, confirmSize };
        const float confirmRight = confirm.position.x + confirmSize.x - inset;
        const float confirmBottom = confirm.position.y + confirmSize.y - inset;
        const UiRect confirmButton { { confirmRight - 128.0f * scale, confirmBottom - buttonHeight },
            { 128.0f * scale, buttonHeight } };
        const UiRect cancelButton {
            confirmButton.position - Vec2 { 94.0f * scale + gap, 0.0f }, { 94.0f * scale, buttonHeight },
        };
        ui.rect({ {}, viewport }, { 0.012f, 0.016f, 0.017f, 0.72f });
        ui.panel(confirm);
        ui.pushClip(confirm);
        const auto title = "Delete screen " + std::to_string(*overworldMapDelete_) + "?";
        ui.text(confirm.position + Vec2 { inset, 14.0f * scale }, title, ink, 24.0f * scale);
        ui.divider({ confirm.position + Vec2 { inset, 49.0f * scale }, { confirmSize.x - 2.0f * inset, scale } });
        const auto& lines = ui.wrapText("This removes the screen and its contents. You can undo this change.",
            confirmSize.x - 2.0f * inset, 16.0f * scale);
        float lineY = confirm.position.y + 60.0f * scale;
        for (const auto& line : lines) {
            ui.text({ confirm.position.x + inset, lineY }, line, mutedInk, 16.0f * scale);
            lineY += 22.0f * scale;
        }
        if (input.interactive) {
            const std::array confirmRegions { UiControlRegion { cancelId, cancelButton },
                UiControlRegion { confirmId, confirmButton } };
            interaction = ui.resolveControls("Maker Delete Screen", confirmRegions,
                overworldMapFocus_ == 0 ? cancelId : overworldMapFocus_, input.navigation);
            overworldMapFocus_ = interaction.focused;
        }
        const bool cancel = button(cancelButton, cancelId, "Cancel", Tone::Normal, true);
        const bool submit = button(confirmButton, confirmId, "Delete Screen", Tone::Danger, true);
        ui.popClip();
        const auto screen = *overworldMapDelete_;
        if (cancel || submit) beginOverworldMap();
        return submit ? MakerOverworldMapAction { .command = Command::Delete, .screen = screen }
                      : MakerOverworldMapAction {};
    }
    if (close) return { .command = Command::Close };
    if (move) {
        if (moving) beginOverworldMap();
        else overworldMapMove_ = MapMove { active->id, active->slot };
        return {};
    }
    if (remove) {
        overworldMapDelete_ = active->id;
        overworldMapFocus_ = 0;
        return {};
    }
    if (undo || redo) {
        beginOverworldMap();
        return { .command = undo ? Command::Undo : Command::Redo };
    }
    if (interaction.activated >= firstSlotId && interaction.activated - firstSlotId < occupants.size()) {
        const auto index = static_cast<std::size_t>(interaction.activated - firstSlotId);
        if (moving) return { .command = Command::Move, .screen = overworldMapMove_->screen, .slot = slotAt(index) };
        return { .command = Command::Jump, .screen = occupants[index] };
    }
    return {};
}

bool MakerView::drawHelp(UiContext& ui, Vec2 viewport, float scale,
    const InputBindings& bindings, const MakerViewInput& input)
{
    const float inset = 18.0f * scale;
    const Vec2 panelSize {
        std::max(0.0f, std::min(704.0f * scale, viewport.x - 2.0f * margin * scale)),
        std::max(0.0f, std::min(620.0f * scale, viewport.y - 2.0f * margin * scale)),
    };
    const UiRect panel { (viewport - panelSize) * 0.5f, panelSize };
    const float headerHeight = 56.0f * scale;
    const float right = panel.position.x + panel.size.x - inset;
    const float trackWidth = 10.0f * scale;
    const UiRect body {
        panel.position + Vec2 { inset, headerHeight + 10.0f * scale },
        { std::max(0.0f, panel.size.x - 2.0f * inset - trackWidth - 8.0f * scale),
            std::max(0.0f, panel.size.y - headerHeight - 10.0f * scale - inset) },
    };
    const UiRect scrollbar { { right - trackWidth, body.position.y }, { trackWidth, body.size.y } };
    const float closeWidth = std::clamp(panel.size.x - 2.0f * inset, 0.0f, 74.0f * scale);
    const UiRect closeButton {
        { right - closeWidth, panel.position.y + 11.0f * scale },
        { closeWidth, 34.0f * scale },
    };
    ui.rect({ {}, viewport }, { 0.012f, 0.016f, 0.017f, 0.72f });
    ui.panel(panel);
    ui.pushClip(panel);
    ui.rect({ panel.position + Vec2 { scale, scale },
        { panel.size.x - 2.0f * scale, headerHeight - scale } }, chrome);
    ui.divider({ { panel.position.x, panel.position.y + headerHeight }, { panel.size.x, scale } });
    ui.pushClip({ panel.position + Vec2 { inset, 0.0f },
        { std::max(0.0f, panel.size.x - 2.0f * inset - closeButton.size.x - 8.0f * scale), headerHeight } });
    ui.text(panel.position + Vec2 { inset, 13.0f * scale }, "Maker Help", ink, 25.0f * scale);
    ui.popClip();

    struct Line {
        std::string text;
        Vec2 offset;
        float size;
        Vec4 color;
    };
    std::vector<Line> lines;
    float y = 3.0f * scale;
    const auto addLine = [&](std::string text, float size, Vec4 color) {
        lines.push_back({ std::move(text), { 0.0f, y }, size, color });
        y += size * 1.25f;
    };
    const auto addWrapped = [&](std::string_view text, float size, Vec4 color) {
        if (ui.measureText(text, size).x <= body.size.x) {
            addLine(std::string(text), size, color);
            return;
        }
        for (const auto& line : ui.wrapText(text, body.size.x, size)) {
            addLine(line, size, color);
        }
    };
    if (body.size.x > 0.0f && body.size.y > 0.0f) {
        constexpr float headingSize = 20.0f;
        const float actionSize = 17.0f * scale;
        const float detailSize = 15.0f * scale;
        for (const auto& section : makerHelp::sections) {
            addWrapped(section.title, headingSize * scale, accentInk);
            y += 8.0f * scale;
            for (const auto& row : section.rows) {
                auto shortcut = makerHelp::shortcutLabel(row, bindings);
                const float startY = y;
                addWrapped(row.action, actionSize, ink);
                if (!shortcut.empty()) {
                    const float keyWidth = ui.measureText(shortcut, detailSize).x;
                    if (ui.measureText(row.action, actionSize).x + keyWidth + 16.0f * scale <= body.size.x) {
                        lines.push_back({ std::move(shortcut), { body.size.x - keyWidth, startY + scale },
                            detailSize, accentInk });
                    } else {
                        addWrapped(shortcut, detailSize, accentInk);
                    }
                }
                addWrapped(row.detail, detailSize, mutedInk);
                y += 10.0f * scale;
            }
            y += 11.0f * scale;
        }
    }
    helpScroll_.setExtents(body.size.y / scale, y / scale);
    helpScroll_.scrollWheel(ui, { body.position, { right - body.position.x, body.size.y } },
        input.wheelY, input.wheelPosition);
    const int scrollDirection = static_cast<int>(input.navigation.next) - static_cast<int>(input.navigation.previous);
    helpScroll_.scrollBy(46.0f * static_cast<float>(scrollDirection));
    helpScroll_.drawScrollbar(ui, "maker.help.scroll", scrollbar);
    const float scroll = helpScroll_.offset() * scale;
    ui.pushClip(body);
    for (const auto& line : lines) {
        const float top = line.offset.y - scroll;
        if (top < body.size.y && top + line.size * 1.25f > 0.0f) {
            ui.text(body.position + Vec2 { line.offset.x, top }, line.text, line.color, line.size);
        }
    }
    ui.popClip();
    constexpr UiControlId closeId = 1;
    const std::array regions { UiControlRegion { closeId, closeButton } };
    const auto interaction = ui.resolveControls("Maker Help", regions, closeId,
        { .activate = input.navigation.activate && !ui.mousePressPosition() });
    const bool close = uiControls::button(ui, closeButton, "Close", {
        .focused = interaction.focused == closeId,
        .activate = interaction.activated == closeId,
        .contentScale = 0.76f * scale,
    });
    ui.popClip();
    return close;
}

void MakerView::beginCameraDialog()
{
    cameraDialogFocus_ = 0;
}

MakerCameraDialogAction MakerView::drawCameraDialog(UiContext& ui, const MakerLayout& layout,
    const MakerCameraDialogState& state, const MakerViewInput& input)
{
    using Command = MakerCameraDialogAction::Command;
    MakerCameraDialogAction action { .angles = state.angles };
    const UiRect panel = layout.palette;
    const float scale = std::min({ layout.scale, panel.size.x / 190.0f, panel.size.y / 350.0f });
    if (scale <= 0.0f) return action;

    const float inset = 12.0f * scale;
    const float width = panel.size.x - 2.0f * inset;
    const float gap = 6.0f * scale;
    const float half = (width - gap) * 0.5f;
    const Vec2 content = panel.position + Vec2 { inset, 0.0f };
    const auto position = [&](float y) { return content + Vec2 { 0.0f, y * scale }; };
    const auto bounds = [&](float offset, float y, float buttonWidth, float height) {
        return UiRect { position(y) + Vec2 { offset, 0.0f }, { buttonWidth, height * scale } };
    };
    ui.pushClip(panel);
    ui.rect(panel, chrome);
    ui.text(position(12.0f), "Screen Camera", ink, fitLabel(ui, "Screen Camera", 24.0f * scale, width));
    ui.divider({ position(45.0f), { width, scale } });

    constexpr UiControlId tiltMinus = 1;
    constexpr UiControlId tiltPlus = 2;
    constexpr UiControlId rotationMinus = 3;
    constexpr UiControlId rotationPlus = 4;
    constexpr UiControlId resetAngles = 5;
    constexpr UiControlId detach = 6;
    constexpr UiControlId resetPose = 7;
    constexpr UiControlId cancel = 8;
    constexpr UiControlId apply = 9;
    constexpr float tiltY = 74.0f;
    constexpr float rotateY = 119.0f;
    const float stepWidth = 28.0f * scale;
    const bool canEdit = !state.detached && !state.mouseCaptured;
    CameraAngles angles = state.angles.value_or(state.defaults);
    bool changed = uiControls::slider(ui, "maker.camera.tilt",
        bounds(stepWidth + gap, tiltY, width - 2.0f * (stepWidth + gap), 24.0f),
        angles.pitchDegrees, 0.0f, CameraAngles::maximumPitchDegrees, false, canEdit, scale);
    changed |= uiControls::slider(ui, "maker.camera.rotation",
        bounds(stepWidth + gap, rotateY, width - 2.0f * (stepWidth + gap), 24.0f),
        angles.yawDegrees, -180.0f, 180.0f, false, canEdit, scale);

    struct Control {
        UiControlRegion region;
        std::string_view label;
    };
    const std::array controls {
        Control { { tiltMinus, bounds(0.0f, tiltY, stepWidth, 24.0f), canEdit && angles.pitchDegrees > 0.0f }, "-" },
        Control { { tiltPlus, bounds(width - stepWidth, tiltY, stepWidth, 24.0f),
            canEdit && angles.pitchDegrees < CameraAngles::maximumPitchDegrees }, "+" },
        Control { { rotationMinus, bounds(0.0f, rotateY, stepWidth, 24.0f), canEdit && angles.yawDegrees > -180.0f }, "-" },
        Control { { rotationPlus, bounds(width - stepWidth, rotateY, stepWidth, 24.0f), canEdit && angles.yawDegrees < 180.0f }, "+" },
        Control { { resetAngles, bounds(0.0f, 154.0f, width, 28.0f), canEdit && state.angles.has_value() }, "Reset Angles" },
        Control { { detach, bounds(0.0f, 199.0f, state.detached ? half : width, 30.0f),
            !state.mouseCaptured && (state.canDetach || state.detached) }, state.detached ? "Reattach" : "Detach Camera" },
        Control { { resetPose, state.detached ? bounds(half + gap, 199.0f, half, 30.0f) : UiRect {},
            state.detached && !state.mouseCaptured }, "Reset View" },
        Control { { cancel, bounds(0.0f, 309.0f, half, 30.0f), !state.mouseCaptured }, "Cancel" },
        Control { { apply, bounds(half + gap, 309.0f, half, 30.0f), !state.mouseCaptured }, "Apply" },
    };
    std::array<UiControlRegion, controls.size()> regions;
    std::ranges::transform(controls, regions.begin(), &Control::region);
    const UiInteraction interaction = ui.resolveControls("Maker Camera", regions, cameraDialogFocus_,
        state.mouseCaptured ? UiNavigation {} : input.navigation);
    cameraDialogFocus_ = interaction.focused;
    for (const auto& [region, label] : controls) {
        if (region.id == resetPose && !state.detached) continue;
        (void)uiControls::button(ui, region.bounds, "", {
            .tone = region.id == apply ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
            .focused = interaction.focused == region.id,
            .activate = interaction.activated == region.id,
            .enabled = region.enabled,
            .contentScale = 0.7f * scale,
        });
        ui.centeredText(region.bounds, label, region.enabled ? buttonInk : disabledButtonInk,
            fitLabel(ui, label, 16.0f * scale, region.bounds.size.x - 6.0f * scale));
    }

    const auto activated = interaction.activated;
    constexpr float step = 5.0f;
    const auto stepAngle = [&](float& angle, float minimum, float maximum, UiControlId minus, UiControlId plus) {
        if (activated != minus && activated != plus) return;
        angle = std::clamp(angle + (activated == plus ? step : -step), minimum, maximum);
        changed = true;
    };
    stepAngle(angles.pitchDegrees, 0.0f, CameraAngles::maximumPitchDegrees, tiltMinus, tiltPlus);
    stepAngle(angles.yawDegrees, -180.0f, 180.0f, rotationMinus, rotationPlus);
    if (changed) action.angles = angles;
    if (activated == resetAngles) action.angles.reset();
    if (activated == detach) action.command = Command::ToggleDetached;
    if (activated == resetPose) action.command = Command::ResetPose;
    if (activated == cancel) action.command = Command::Cancel;
    if (activated == apply) action.command = Command::Apply;

    const auto drawAngle = [&](float y, std::string_view label, float value) {
        ui.text(position(y), label, ink, 16.0f * scale);
        char number[24];
        std::snprintf(number, sizeof(number), "%.1f\xc2\xb0", static_cast<double>(value));
        const float size = 15.0f * scale;
        ui.text(position(y) + Vec2 { width - ui.measureText(number, size).x, 0.0f },
            number, mutedInk, size);
    };
    const CameraAngles current = action.angles.value_or(state.defaults);
    drawAngle(54.0f, "Tilt", current.pitchDegrees);
    drawAngle(99.0f, "Rotation", current.yawDegrees);
    ui.divider({ position(190.0f), { width, scale } });
    const std::string_view hint = state.detached
        ? (state.mouseCaptured
            ? "Esc releases the mouse. WASD moves, Space/E goes up, Ctrl/Q goes down, and Shift moves faster."
            : "Click the board to fly. WASD moves, Space/E goes up, Ctrl/Q goes down, and Shift moves faster.")
        : "Tilt 0\xc2\xb0 looks straight down. The draft stores these angles when saved.";
    drawWrappedText(ui, bounds(0.0f, 240.0f, width, 65.0f), hint, mutedInk, 13.0f * scale);
    ui.popClip();
    return action;
}

void MakerView::beginDraftDialog(MakerDraftDialogMode mode, std::string_view initialName)
{
    blurTileSearch();
    blurDraftName();
    draftNameBounds_ = {};
    draftNamePressHandled_ = false;
    draftDialogFocus_ = 0;
    draftNameAllowsEmpty_ = mode == MakerDraftDialogMode::LecternText;
    draftUsesPicker_ = mode == MakerDraftDialogMode::Load || mode == MakerDraftDialogMode::SelectorTarget;
    if (nameDialog(mode)) {
        draftNameField_ = {};
        draftNameEdit_ = UiTextEdit(draftNameAllowsEmpty_ ? std::numeric_limits<std::size_t>::max() : 256U);
        draftNameEdit_.setText(initialName);
        if (!draftNameAllowsEmpty_) draftNameEdit_.selectAll();
        draftNameFocused_ = true;
        draftDialogFocus_ = draftSubmitId;
    } else if (draftUsesPicker_) {
        draftPicker_.open(initialName);
    } else if (mode == MakerDraftDialogMode::RecoverAutosave) {
        draftDialogFocus_ = draftSubmitId;
    } else if (mode == MakerDraftDialogMode::NewDraft) {
        draftWidth_ = MakerSession::defaultDraftWidth;
        draftHeight_ = MakerSession::defaultDraftHeight;
        resumeBoardSizeDialog();
    }
}

void MakerView::beginResizeBoardDialog(uint32_t width, uint32_t height)
{
    beginDraftDialog(MakerDraftDialogMode::ResizeBoard);
    resizeOriginalWidth_ = draftWidth_ = width;
    resizeOriginalHeight_ = draftHeight_ = height;
    resumeBoardSizeDialog();
}

void MakerView::resumeBoardSizeDialog()
{
    draftDialogFocus_ = draftSubmitId;
}

void MakerView::blurDraftName()
{
    draftPicker_.blurSearch();
    draftNameFocused_ = false;
    draftNameCaret_ = {};
    draftNameEdit_.clearComposition();
}

bool MakerView::focusDraftSearch()
{
    if (!draftUsesPicker_) return false;
    draftPicker_.focusSearch();
    return true;
}

bool MakerView::handleDraftNamePress(UiContext& ui, Vec2 position, bool extendSelection)
{
    if (draftUsesPicker_) return draftPicker_.handleSearchPress(ui, position, extendSelection);
    if (draftNameBounds_.size.x <= 0.0f || draftNameBounds_.size.y <= 0.0f) return false;
    draftNamePressHandled_ = true;
    if (ui.contains(draftNameBounds_, position)) {
        draftNameFocused_ = true;
        draftDialogFocus_ = draftSubmitId;
        draftNameField_.placeCaret(ui, draftNameBounds_, draftNameEdit_, draftNameTextSize_,
            position, extendSelection);
        return true;
    }
    const bool wasFocused = draftNameFocused_;
    blurDraftName();
    return wasFocused;
}

MakerDraftDialogAction MakerView::submitDraftName() const
{
    if (draftUsesPicker_ || (!draftNameAllowsEmpty_ && draftNameEdit_.text().empty()) ||
        !draftNameEdit_.composition().text.empty()) return {};
    return { MakerDraftDialogAction::Command::Submit, draftNameEdit_.text() };
}

MakerDraftDialogAction MakerView::drawDraftDialog(UiContext& ui, Vec2 viewport, float scale,
    const MakerDraftDialogState& state, const MakerViewInput& input)
{
    using Command = MakerDraftDialogAction::Command;
    const bool enterName = nameDialog(state.mode);
    const bool chooseTarget = state.mode == MakerDraftDialogMode::SelectorTarget;
    const DraftDialogCopy copy = draftDialogCopy(state.mode, state.overworld);
    if (state.mode == MakerDraftDialogMode::Load || chooseTarget) {
        const float height = chooseTarget ? 490.0f : 432.0f;
        const auto dialog = UiDialogLayout::centered(viewport, scale, { 510.0f, height },
            chooseTarget ? height + 30.0f : 260.0f);
        std::string description;
        if (chooseTarget) {
            if (state.missingSelectorTarget) description = "The attached saved level is missing.";
            else if (state.legacySelectorTarget) description = "Attached to a legacy puzzle";
            else if (state.pendingName.empty()) description = "No level attached";
            else description = "Attached to " + std::string(state.pendingName);
        }
        const auto result = draftPicker_.draw(ui, dialog, {
            .scope = copy.scope,
            .title = copy.title,
            .submitLabel = copy.submitLabel,
            .choices = state.names,
            .currentChoice = state.missingSelectorTarget ? std::string_view {} : state.pendingName,
            .emptyChoiceLabel = chooseTarget && (!state.pendingName.empty() || state.legacySelectorTarget ||
                state.missingSelectorTarget)
                ? "Detach level" : "",
            .description = description,
            .placeholder = chooseTarget ? "Search saved levels" : "Search saved drafts",
            .emptyMessage = chooseTarget ? "No saved levels yet." : "No saved drafts yet.",
            .noMatchesMessage = chooseTarget ? "No matching levels." : "No matching drafts.",
            .error = state.error,
            .rowRenameLabel = chooseTarget ? "" : "Rename",
            .rowDeleteLabel = chooseTarget ? "" : "Delete",
            .selectEmptyChoiceInitially = state.missingSelectorTarget,
        }, {
            .navigation = input.navigation,
            .wheelPosition = input.wheelPosition,
            .wheelY = input.wheelY,
            .extendSelection = input.extendTextSelection,
            .interactive = input.interactive,
            .rowActionStep = input.pickerRowActionStep,
        });
        if (result.command == UiSearchPickerResult::Command::Cancel) return { .command = Command::Cancel };
        if (result.command == UiSearchPickerResult::Command::Choose) {
            return { Command::Submit, std::string(result.choice) };
        }
        if (result.command == UiSearchPickerResult::Command::Rename) {
            return { Command::Rename, std::string(result.choice) };
        }
        if (result.command == UiSearchPickerResult::Command::Delete) {
            return { Command::Delete, std::string(result.choice) };
        }
        return {};
    }
    const bool resize = state.mode == MakerDraftDialogMode::ResizeBoard;
    const bool chooseDimensions = resize || state.mode == MakerDraftDialogMode::NewDraft;
    const bool discardForNew = state.mode == MakerDraftDialogMode::DiscardNew;
    const bool crop = state.mode == MakerDraftDialogMode::ConfirmCrop;
    const bool deleteLayer = state.mode == MakerDraftDialogMode::ConfirmDeleteLayer;
    const bool deleteDraft = state.mode == MakerDraftDialogMode::DeleteDraft;
    const bool recovering = state.mode == MakerDraftDialogMode::RecoverAutosave;
    const bool discardingAutosave = state.mode == MakerDraftDialogMode::DiscardAutosave;
    const bool previewingResize = resize || crop;
    const bool confirmation = !enterName && !chooseDimensions;
    const bool tallConfirmation = deleteLayer || deleteDraft || recovering;
    const float preferredHeight = chooseDimensions ? 278.0f : tallConfirmation ? 286.0f : 230.0f;
    const UiRect previewPanel = previewingResize
        ? state.resizePreviewPanel.value_or(layout(viewport, scale).palette) : UiRect {};
    const float previewPanelHeight = crop ? 300.0f : 324.0f;
    const auto dialog = UiDialogLayout::centered(viewport, scale, { 510.0f, preferredHeight },
        chooseDimensions || tallConfirmation ? preferredHeight + 30.0f : 260.0f);
    scale = previewingResize
        ? std::min({ scale, previewPanel.size.x / 190.0f, previewPanel.size.y / previewPanelHeight })
        : dialog.scale;
    if (scale <= 0.0f) return {};

    const float inset = (previewingResize ? 14.0f : 20.0f) * scale;
    const UiRect panel = previewingResize
        ? UiRect { previewPanel.position, { previewPanel.size.x, previewPanelHeight * scale } }
        : dialog.panel;
    const float contentX = panel.position.x + inset;
    const float contentWidth = panel.size.x - inset * 2.0f;
    const float buttonY = panel.position.y + panel.size.y - 54.0f * scale;
    const float centeredSubmitWidth = confirmation ? 166.0f : 110.0f;
    const float submitWidth = crop ? contentWidth : contentWidth * 0.53f;
    const UiRect submitButton = previewingResize
        ? UiRect { { contentX + contentWidth - submitWidth, buttonY }, { submitWidth, 34.0f * scale } }
        : dialog.footerButton(centeredSubmitWidth);
    const UiRect cancelButton = crop
        ? UiRect { { contentX, buttonY - 42.0f * scale }, { contentWidth, 34.0f * scale } }
        : resize
            ? UiRect { { contentX, buttonY }, { contentWidth - submitWidth - 8.0f * scale, 34.0f * scale } }
            : dialog.footerButton(96.0f, centeredSubmitWidth + 10.0f);
    auto discardButton = dialog.footerButton(132.0f);
    discardButton.position.x = contentX;
    if (previewingResize) {
        ui.pushClip(previewPanel);
        ui.rect(previewPanel, chrome);
        drawDialogPanel(ui, panel, copy.title, inset, scale);
    } else {
        dialog.draw(ui, copy.title);
    }

    if (enterName) {
        const bool saveDraft = state.mode == MakerDraftDialogMode::Save || state.mode == MakerDraftDialogMode::RenameDraft;
        const bool lectern = state.mode == MakerDraftDialogMode::LecternText;
        const std::string_view label = lectern ? R"(Message (use \n for a new line))"
            : saveDraft ? (state.overworld ? "Overworld name" : "Draft name") : "Group name";
        const UiRect labelBounds { { contentX, panel.position.y + 63.0f * scale },
            { contentWidth, 21.0f * scale } };
        ui.pushClip(labelBounds);
        ui.text(labelBounds.position, label, mutedInk, 15.0f * scale);
        ui.popClip();
        draftNameBounds_ = {
            { contentX, panel.position.y + 86.0f * scale }, { contentWidth, 38.0f * scale },
        };
        draftNameTextSize_ = 18.0f * scale;
        const bool pressHandled = std::exchange(draftNamePressHandled_, false);
        if (const auto press = ui.mousePressPosition(); press && !pressHandled) {
            if (ui.contains(draftNameBounds_, *press)) {
                draftNameFocused_ = true;
                draftDialogFocus_ = draftSubmitId;
            } else {
                blurDraftName();
            }
        }
        draftNameCaret_ = draftNameField_.draw(ui, "maker.draft.name",
            draftNameBounds_, draftNameEdit_, {
            .focused = draftNameFocused_,
            .extendSelection = input.extendTextSelection,
            .pressAlreadyHandled = pressHandled,
            .placeholder = lectern ? "Write the message for this lectern"
                : saveDraft ? "Name this draft" : "Name this group",
            .textSize = draftNameTextSize_,
        }).caret;
    } else if (confirmation) {
        std::string message;
        if (recovering) {
            message = state.canRecoverAutosave ? "Open unsaved edits to " + std::string(state.pendingName) +
                " as the working draft. Use Save to keep them." : "Maker found an autosave it could not read.";
            if (state.otherAutosaveAvailable) {
                message += state.overworld ? " A puzzle autosave is also available in Puzzle Maker."
                                           : " Another autosave is available in Overworld Maker.";
            }
        } else if (discardingAutosave) {
            message = "Remove the autosave for " + std::string(state.pendingName) + "?";
        } else if (deleteDraft) {
            message = std::string(state.overworld ? "Delete the saved overworld " : "Delete the saved level ") +
                "\xe2\x80\x9c" + std::string(state.pendingName) + "\xe2\x80\x9d?";
            if (!state.overworld) message += " Any Level Selectors linked to it will show a missing level.";
        } else if (discardForNew) {
            message = "Create a " + std::to_string(draftWidth_) + " \xc3\x97 " +
                std::to_string(draftHeight_) + " draft and discard your unsaved changes?";
        } else if (crop) {
            message = "Resizing to " + std::to_string(draftWidth_) + " \xc3\x97 " +
                std::to_string(draftHeight_) + " will remove " + std::to_string(state.removedTileCount) +
                (state.removedTileCount == 1 ? " tile." : " tiles.");
        } else if (deleteLayer) {
            message = "Delete layer " + std::to_string(state.layer + 1) +
                (state.overworld ? " across the whole overworld?" : "?");
            struct Loss { std::size_t count; std::string_view name; };
            const std::array losses {
                Loss { state.removedTileCount, "tile" },
                Loss { state.removedSelectorCount, "selector" },
                Loss { state.removedDecorationCount, "decoration" },
                Loss { state.removedElevatorStopCount, "elevator stop" },
            };
            bool first = true;
            for (const auto& loss : losses) {
                if (!loss.count) continue;
                message += first ? " This removes " : ", ";
                first = false;
                message += std::to_string(loss.count) + " " + std::string(loss.name);
                if (loss.count != 1) message += 's';
            }
            if (!first) message += '.';
            if (state.removesWaterLayer) message += " The water layer will be cleared.";
        } else if (state.mode == MakerDraftDialogMode::Overwrite) {
            message = "Replace the saved draft \xe2\x80\x9c" + std::string(state.pendingName) + "\xe2\x80\x9d?";
        } else {
            message = "Load \xe2\x80\x9c" + std::string(state.pendingName) + "\xe2\x80\x9d and discard your unsaved changes?";
        }
        drawWrappedText(ui, { { contentX, panel.position.y + 65.0f * scale },
            { contentWidth, (tallConfirmation ? 122.0f : crop ? 67.0f : 58.0f) * scale } }, message, mutedInk, 17.0f * scale);
    }

    const bool submitEnabled = (chooseDimensions || confirmation ||
        (enterName && (draftNameAllowsEmpty_ || !draftNameEdit_.text().empty()))) &&
        (!recovering || state.canRecoverAutosave);
    UiNavigation navigation = input.navigation;
    if (ui.mousePressPosition()) navigation = {};
    const bool waitingForName = enterName && draftNameFocused_ && !submitEnabled &&
        draftDialogFocus_ == draftSubmitId && !navigation.previous && !navigation.next;
    if (waitingForName) navigation.activate = false;
    FrameArray<UiControlRegion> regions(ui.frameArena(), 6);
    const auto dimensionButton = [&](float y, bool increase) -> UiRect {
        if (resize) {
            return { { contentX + contentWidth - (increase ? 28.0f : 104.0f) * scale,
                         panel.position.y + y * scale },
                { 28.0f * scale, 30.0f * scale } };
        }
        const float offset = increase ? 36.0f : 120.0f;
        return { { contentX + contentWidth - offset * scale, panel.position.y + y * scale },
            { 36.0f * scale, 34.0f * scale } };
    };
    const float widthRow = resize ? 63.0f : 65.0f;
    const float heightRow = resize ? 125.0f : 111.0f;
    const std::array dimensionButtons {
        UiControlRegion { widthDecreaseId, dimensionButton(widthRow, false) },
        UiControlRegion { widthIncreaseId, dimensionButton(widthRow, true) },
        UiControlRegion { heightDecreaseId, dimensionButton(heightRow, false) },
        UiControlRegion { heightIncreaseId, dimensionButton(heightRow, true) },
    };
    constexpr auto minimumDimension = MakerSession::minDraftDimension;
    constexpr auto maximumDimension = MakerSession::maxDraftDimension;
    const uint32_t maxWidth = resize ? std::max(maximumDimension, resizeOriginalWidth_) : maximumDimension;
    const uint32_t maxHeight = resize ? std::max(maximumDimension, resizeOriginalHeight_) : maximumDimension;
    if (chooseDimensions) {
        const auto slider = [&](std::string_view id, float y, uint32_t& dimension, uint32_t maximum) {
            const UiRect bounds = resize
                ? UiRect { { contentX + 7.0f * scale, panel.position.y + (y + 33.0f) * scale },
                      { contentWidth - 14.0f * scale, 22.0f * scale } }
                : UiRect { { contentX + 78.0f * scale, panel.position.y + (y + 1.0f) * scale },
                      { contentWidth - 218.0f * scale, 32.0f * scale } };
            float value = static_cast<float>(dimension);
            if (uiControls::slider(ui, id, bounds, value, static_cast<float>(minimumDimension),
                    static_cast<float>(maximum), false, true, scale)) {
                dimension = static_cast<uint32_t>(std::lround(value));
            }
        };
        slider("maker.draft.width", widthRow, draftWidth_, maxWidth);
        slider("maker.draft.height", heightRow, draftHeight_, maxHeight);
        const std::array dimensionEnabled { draftWidth_ > minimumDimension, draftWidth_ < maxWidth,
            draftHeight_ > minimumDimension, draftHeight_ < maxHeight };
        for (std::size_t index = 0; index < dimensionButtons.size(); ++index) {
            auto region = dimensionButtons[index];
            region.enabled = dimensionEnabled[index];
            regions.push_back(region);
        }
    }
    constexpr UiControlId discardAutosaveId = 7;
    regions.push_back({ draftCancelId, cancelButton });
    regions.push_back({ draftSubmitId, submitButton, submitEnabled });
    if (recovering) regions.push_back({ discardAutosaveId, discardButton });
    const UiInteraction interaction = ui.resolveControls(copy.scope, regions, draftDialogFocus_, navigation);
    if (!waitingForName || interaction.pressed != 0 || interaction.activated != 0) {
        draftDialogFocus_ = interaction.focused;
    }

    if (previewingResize) {
        const auto drawSwatch = [&](float y, Vec4 color, std::string_view label) {
            const Vec2 position { contentX, panel.position.y + y * scale };
            ui.rect({ position + Vec2 { 0.0f, 3.0f } * scale, Vec2 { 11.0f, 11.0f } * scale }, color);
            ui.text(position + Vec2 { 18.0f, 0.0f } * scale, label, ink, 15.0f * scale);
        };
        drawSwatch(crop ? 136.0f : 197.0f, removedTileInk, "Removed tiles");
        drawSwatch(crop ? 157.0f : 218.0f, addedTileInk, "Added ground");
    }
    const float errorY = panel.position.y +
        (resize ? 241.0f : tallConfirmation ? 190.0f : crop ? 178.0f : chooseDimensions ? 155.0f : 130.0f) * scale;
    const float errorBottom = crop ? cancelButton.position.y : buttonY;
    drawWrappedText(ui, { { contentX, errorY }, { contentWidth, errorBottom - errorY - 5.0f * scale } },
        state.error, errorInk, 15.0f * scale);
    const auto drawButton = [&](UiControlId id, UiRect bounds, std::string_view label,
        uiControls::ButtonTone tone, bool enabled) {
        return uiControls::button(ui, bounds, label, {
            .tone = tone,
            .focused = interaction.focused == id,
            .activate = interaction.activated == id,
            .enabled = enabled,
            .contentScale = 0.76f * scale,
        });
    };
    if (chooseDimensions) {
        const auto dimension = [&](std::string_view label, uint32_t& value, uint32_t maximum,
                                   const UiControlRegion& decrease, const UiControlRegion& increase) {
            if (drawButton(decrease.id, decrease.bounds, "\xe2\x88\x92", uiControls::ButtonTone::Normal,
                    value > minimumDimension)) --value;
            if (drawButton(increase.id, increase.bounds, "+", uiControls::ButtonTone::Normal,
                    value < maximum)) ++value;
            ui.text({ contentX, decrease.bounds.position.y + 6.0f * scale }, label, ink, 18.0f * scale);
            const Vec2 numberPosition = decrease.bounds.position + Vec2 { decrease.bounds.size.x, 0.0f };
            ui.centeredText({ numberPosition,
                { increase.bounds.position.x - numberPosition.x, decrease.bounds.size.y } },
                std::to_string(value), ink, 20.0f * scale);
        };
        dimension("Width", draftWidth_, maxWidth, dimensionButtons[0], dimensionButtons[1]);
        dimension("Height", draftHeight_, maxHeight, dimensionButtons[2], dimensionButtons[3]);
    }
    const bool cancel = drawButton(draftCancelId, cancelButton, recovering ? "Back to Title" : "Cancel",
        uiControls::ButtonTone::Normal, true);
    if (recovering && drawButton(discardAutosaveId, discardButton, "Discard Autosave", uiControls::ButtonTone::Danger, true)) {
        return { .command = Command::Discard };
    }
    const bool submit = drawButton(draftSubmitId, submitButton, copy.submitLabel,
        confirmation && !recovering ? uiControls::ButtonTone::Danger : uiControls::ButtonTone::Accent, submitEnabled);
    if (previewingResize) ui.popClip();
    if (cancel) return { Command::Cancel, {} };
    if (!submit) return {};
    if (enterName) return submitDraftName();
    if (chooseDimensions || discardForNew || crop) {
        return { .command = Command::Submit, .width = draftWidth_, .height = draftHeight_ };
    }
    return { Command::Submit, std::string(state.pendingName) };
}

} // namespace sokoban
