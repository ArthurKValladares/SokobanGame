#include "engine/ui/MakerView.hpp"

#include "engine/EditorTilePalette.hpp"
#include "engine/MakerSession.hpp"
#include "engine/ui/LecternDialog.hpp"
#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace sokoban {
namespace {

constexpr float margin = 12.0f;
constexpr float compactToolbarHeight = 96.0f;
constexpr float wideToolbarHeight = 62.0f;
constexpr float wideToolbarWidth = 1300.0f;
constexpr float footerHeight = 34.0f;
constexpr float tileHeight = 32.0f;
constexpr float tileGap = 4.0f;
constexpr float groupHeight = 28.0f;
constexpr float groupGap = 9.0f;
constexpr float searchTop = 48.0f;
constexpr float searchHeight = 32.0f;
constexpr float listTop = searchTop + searchHeight + 8.0f;

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
constexpr UiControlId firstDraftRowId = 3;
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
constexpr std::array objects {
    TileType::RoundRock, TileType::Ice, TileType::MirrorNorthWest, TileType::RobotNorth,
    TileType::Enemy,
};
constexpr std::array mechanisms {
    TileType::PressurePlate, TileType::Gate, TileType::EnemyGate, TileType::LockPlate,
    TileType::ButtonNorth, TileType::LeverNorth, TileType::ConveyorUp,
    TileType::RotatorClockwise, TileType::Elevator, TileType::PortalNorth, TileType::TurretNorth,
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
    TileGroup { "Objects", objects },
    TileGroup { "Mechanisms", mechanisms },
    TileGroup { "Rails", rail },
};
constexpr std::size_t paletteTileCount = basics.size() + terrain.size() + characters.size() +
    objects.size() + mechanisms.size() + rail.size();
constexpr UiControlId firstTileId = 100;

UiControlId commandId(MakerCommand command)
{
    return static_cast<UiControlId>(command);
}

UiControlId tileId(TileType tile)
{
    return firstTileId + static_cast<UiControlId>(tile);
}

std::string_view paletteName(TileType tile)
{
    if (const auto* group = editorTilePalette::groupFor(tile); group && group->rotationOnly) {
        return group->name;
    }
    return tileTypeDisplayName(tile);
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
};

bool drawToolbarControl(UiContext& ui, const ToolbarControl& control,
    const UiInteraction& interaction, bool showFocus, float scale)
{
    ui.pushClip(control.bounds);
    const bool activated = uiControls::button(ui, control.bounds, "", {
        .tone = control.accented ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
        .focused = showFocus && interaction.focused == commandId(control.command),
        .activate = interaction.activated == commandId(control.command),
        .enabled = control.enabled,
        .contentScale = 0.72f * scale,
    });
    ui.centeredText(control.bounds, control.label,
        control.enabled ? buttonInk : disabledButtonInk, 24.0f * 0.72f * scale);
    ui.popClip();
    return activated;
}

std::array<ToolbarControl, 13> toolbarControls(const MakerLayout& layout, const MakerViewState& state)
{
    const float scale = layout.scale;
    const float height = 34.0f * scale;
    const float gap = 6.0f * scale;
    const float inset = margin * scale;
    const float width = layout.toolbar.size.x;
    const bool wide = width >= wideToolbarWidth * scale;
    const float backWidth = std::min(88.0f * scale, width * 0.18f);
    const UiRect back { { width - inset - backWidth, 14.0f * scale }, { backWidth, height } };
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
    const float toolsStart = wide ? 222.0f * scale : inset;
    const float toolsEnd = resize.position.x - (wide ? 16.0f : 6.0f) * scale;
    const float toolsWidth = std::max(0.0f, toolsEnd - toolsStart);
    const float y = (wide ? 14.0f : 53.0f) * scale;
    const float layerWidth = std::min(148.0f * scale, toolsWidth * 0.28f);
    const float layerButton = std::min(30.0f * scale, layerWidth * 0.24f);
    const float buttonWidth = std::min(92.0f * scale,
        std::max(0.0f, (toolsWidth - layerWidth - gap * 5.0f) / 5.0f));
    const auto tool = [&](int index) -> UiRect {
        return { { toolsStart + static_cast<float>(index) * (buttonWidth + gap), y },
            { buttonWidth, height } };
    };
    const float layerStart = toolsStart + 5.0f * (buttonWidth + gap);
    return {{
        { MakerCommand::Paint, tool(0), "Paint", true, !state.erasing },
        { MakerCommand::Erase, tool(1), "Erase", true, state.erasing },
        { MakerCommand::Rotate, tool(2), "Rotate", !state.erasing },
        { MakerCommand::Undo, tool(3), "Undo", state.canUndo },
        { MakerCommand::Redo, tool(4), "Redo", state.canRedo },
        { MakerCommand::LayerDown, { { layerStart, y }, { layerButton, height } }, "-", state.layer > 0 },
        { MakerCommand::LayerUp, { { layerStart + layerWidth - layerButton, y },
            { layerButton, height } }, "+", state.layer + 1 < state.layerCount },
        { MakerCommand::ResizeBoard, resize, resizeWidth >= 104.0f * scale ? "Resize Board" : "Resize" },
        { MakerCommand::NewDraft, newDraft, "New Draft" },
        { MakerCommand::Save, save, "Save" },
        { MakerCommand::Load, load, "Load" },
        { MakerCommand::TestPlay, test, "Test Play", true, true },
        { MakerCommand::Back, back, "Back" },
    }};
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

void drawTitle(UiContext& ui, const MakerLayout& layout, float right, std::string_view title)
{
    const float scale = layout.scale;
    ui.pushClip({ {}, { std::max(0.0f, right - margin * scale), layout.toolbar.size.y } });
    ui.text(Vec2 { margin + 4.0f, 17.0f } * scale, title, ink, 24.0f * scale);
    ui.popClip();
}

struct TileControl {
    TileType tile;
    UiRect bounds;
};

struct DraftDialogCopy {
    std::string_view title;
    std::string_view submitLabel;
    std::string_view scope;
};

DraftDialogCopy draftDialogCopy(MakerDraftDialogMode mode)
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
    }
    return {};
}

void drawDialogText(UiContext& ui, UiRect bounds, std::string_view text, Vec4 color, float size)
{
    if (text.empty() || bounds.size.x <= 0.0f || bounds.size.y <= 0.0f) return;
    const auto lines = LecternDialog::wrapText(text, bounds.size.x,
        [&](std::string_view line) { return ui.measureText(line, size).x; });
    ui.pushClip(bounds);
    float y = bounds.position.y;
    for (const auto& line : lines) {
        if (y >= bounds.position.y + bounds.size.y) break;
        ui.text({ bounds.position.x, y }, line, color, size);
        y += size * 1.25f;
    }
    ui.popClip();
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

MakerViewAction MakerView::draw(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input)
{
    const float scale = layout.scale;
    drawChrome(ui, layout);
    drawTileSearch(ui, layout, input);
    const auto controls = toolbarControls(layout, state);
    const float titleEnd = layout.toolbar.size.x >= wideToolbarWidth * scale
        ? controls[0].bounds.position.x : controls[8].bounds.position.x;
    drawTitle(ui, layout, titleEnd,
        titleEnd >= 210.0f * scale ? "Sokoban Maker" : "Maker");
    const UiRect list {
        layout.palette.position + Vec2 { 10.0f, listTop } * scale,
        { std::max(0.0f, layout.palette.size.x - 28.0f * scale),
            std::max(0.0f, layout.palette.size.y - (listTop + 11.0f) * scale) },
    };
    const UiRect scrollbar {
        { list.position.x + list.size.x + 5.0f * scale, list.position.y },
        { 6.0f * scale, list.size.y },
    };
    const auto& query = searchEdit_.text();
    if (renderedQuery_ != query) {
        paletteScroll_ = {};
        renderedQuery_ = query;
    }
    std::array<TileControl, paletteTileCount> tiles;
    std::array<std::size_t, groups.size()> groupTileCounts {};
    std::size_t tileCount = 0;
    std::size_t groupCount = 0;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        for (TileType tile : groups[index].tiles) {
            if (query.empty() || matchesSearchTerms(paletteName(tile), query)) {
                tiles[tileCount++].tile = tile;
                ++groupTileCounts[index];
            }
        }
        groupCount += groupTileCounts[index] > 0 ? 1U : 0U;
    }
    // Keep the scroll offset in logical units across monitor scale changes.
    const float contentHeight = static_cast<float>(groupCount) * (groupHeight + groupGap) +
        static_cast<float>(tileCount) * (tileHeight + tileGap);
    paletteScroll_.setExtents(list.size.y / scale, contentHeight);
    if (input.interactive) {
        paletteScroll_.scrollWheel(ui, layout.palette, input.wheelY, input.wheelPosition);
        paletteScroll_.drawScrollbar(ui, "maker.palette.scroll", scrollbar);
    }

    std::array<UiControlRegion, controls.size() + paletteTileCount> regions;
    std::size_t regionCount = 0;
    for (const auto& control : controls) {
        regions[regionCount++] = { commandId(control.command), control.bounds, control.enabled };
    }
    std::array<UiRect, groups.size()> labels;
    std::size_t tileIndex = 0;
    float y = list.position.y - paletteScroll_.offset() * scale;
    ui.pushClip(list);
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (groupTileCounts[index] == 0) continue;
        labels[index] = { { list.position.x, y }, { list.size.x, groupHeight * scale } };
        y += groupHeight * scale;
        const std::size_t end = tileIndex + groupTileCounts[index];
        for (; tileIndex < end; ++tileIndex) {
            const UiRect bounds { { list.position.x, y }, { list.size.x, tileHeight * scale } };
            tiles[tileIndex].bounds = bounds;
            regions[regionCount++] = { tileId(tiles[tileIndex].tile), ui.visibleRect(bounds) };
            y += (tileHeight + tileGap) * scale;
        }
        y += groupGap * scale;
    }
    ui.popClip();

    UiInteraction interaction;
    if (input.interactive) {
        interaction = ui.resolveControls("Maker", std::span(regions).first(regionCount),
            focus_, searchFocused_ ? UiNavigation {} : input.navigation);
        focus_ = interaction.focused;
    }
    MakerViewAction result;
    for (const auto& control : controls) {
        if (drawToolbarControl(ui, control, interaction, !searchFocused_, scale)) {
            result.command = control.command;
        }
    }
    const auto& lowerLayer = controls[5].bounds;
    const auto& higherLayer = controls[6].bounds;
    const float layerLeft = lowerLayer.position.x + lowerLayer.size.x;
    ui.centeredText({ { layerLeft, lowerLayer.position.y },
                        { higherLayer.position.x - layerLeft, lowerLayer.size.y } },
        "Layer " + std::to_string(state.layer + 1), ink, 15.0f * scale);

    ui.pushClip(list);
    if (tileCount == 0) {
        ui.text(list.position + Vec2 { 2.0f, 6.0f } * scale, "No matching tiles", mutedInk, 15.0f * scale);
    }
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (groupTileCounts[index] == 0) continue;
        ui.text(labels[index].position + Vec2 { 2.0f, 4.0f } * scale,
            groups[index].label, mutedInk, 15.0f * scale);
    }
    for (const auto& [tile, bounds] : std::span(tiles).first(tileCount)) {
        const auto visible = ui.visibleRect(bounds);
        if (visible.size.y <= 0.0f) continue;
        const bool selected = !state.erasing && selectedTile(tile, state.brush);
        if (uiControls::button(ui, bounds, "", {
                .tone = selected ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
                .focused = input.interactive && !searchFocused_ && focus_ == tileId(tile),
                .activate = interaction.activated == tileId(tile),
                .contentScale = 0.72f * scale,
            })) {
            result.tile = tile;
        }
        ui.rect({ bounds.position + Vec2 { 9.0f, 8.0f } * scale,
            Vec2 { 16.0f, 16.0f } * scale }, tileColor(tile));
        ui.text(bounds.position + Vec2 { 34.0f, 6.0f } * scale, paletteName(tile), ink, 16.0f * scale);
    }
    ui.popClip();

    const float footerY = layout.footer.position.y + 7.0f * scale;
    const float footerInset = margin * scale;
    const float footerText = 15.0f * scale;
    float leftWidth = 0.0f;
    if (state.status.empty()) {
        std::string toolLabel = state.erasing ? "Erase" : std::string(paletteName(state.brush));
        if (!state.erasing && state.quarterTurns != 0) {
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
    ui.text({ std::max(leftWidth, footerInset), footerY }, state.status.empty() ?
        (state.erasing ? "Drag to erase" : "Drag to paint") : state.status, mutedInk, footerText);
    ui.popClip();
    ui.pushClip({ { layout.footer.size.x - rightWidth, layout.footer.position.y },
        { rightWidth, layout.footer.size.y } });
    ui.text({ layout.footer.size.x - footerInset - sessionWidth, footerY },
        sessionLabel, mutedInk, footerText);
    ui.popClip();
    return input.interactive ? result : MakerViewAction {};
}

MakerCommand MakerView::drawPlaytest(UiContext& ui, const MakerLayout& layout, bool solved)
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
        "Return to Maker", true, true,
    };
    drawChrome(ui, layout);
    drawTitle(ui, layout, returnControl.bounds.position.x, "Test Play");
    const std::array regions {
        UiControlRegion { commandId(returnControl.command), returnControl.bounds, true, false },
    };
    const UiInteraction interaction = ui.resolveControls("Maker Test Play", regions, 0);
    const bool returnToMaker = drawToolbarControl(ui, returnControl, interaction, false, scale);

    ui.pushClip(layout.palette);
    const Vec2 panel = layout.palette.position + Vec2 { 12.0f, 12.0f } * scale;
    if (solved) {
        ui.text(panel, "Draft solved", accentInk, 23.0f * scale);
    } else {
        ui.text(panel, "Objective", ink, 23.0f * scale);
        ui.text(panel + Vec2 { 0.0f, 34.0f } * scale, "Put every hero on a goal.", mutedInk, 15.0f * scale);
        ui.text(panel + Vec2 { 0.0f, 58.0f } * scale, "Fill every goal.", mutedInk, 15.0f * scale);
    }
    ui.popClip();
    return returnToMaker ? returnControl.command : MakerCommand::None;
}

void MakerView::beginDraftDialog(MakerDraftDialogMode mode, std::string_view initialName)
{
    blurTileSearch();
    blurDraftName();
    draftNameBounds_ = {};
    draftNamePressHandled_ = false;
    draftDialogFocus_ = 0;
    draftListScroll_ = {};
    draftListSelection_ = 0;
    if (mode == MakerDraftDialogMode::Save) {
        draftNameField_ = {};
        draftNameEdit_.setText(initialName);
        draftNameEdit_.selectAll();
        draftNameFocused_ = true;
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
    draftNameFocused_ = false;
    draftNameCaret_ = {};
    draftNameEdit_.clearComposition();
}

bool MakerView::handleDraftNamePress(UiContext& ui, Vec2 position, bool extendSelection)
{
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
    if (draftNameEdit_.text().empty() || !draftNameEdit_.composition().text.empty()) return {};
    return { MakerDraftDialogAction::Command::Submit, draftNameEdit_.text() };
}

MakerDraftDialogAction MakerView::drawDraftDialog(UiContext& ui, Vec2 viewport, float scale,
    const MakerDraftDialogState& state, const MakerViewInput& input)
{
    using Command = MakerDraftDialogAction::Command;
    const bool save = state.mode == MakerDraftDialogMode::Save;
    const bool load = state.mode == MakerDraftDialogMode::Load;
    const bool resize = state.mode == MakerDraftDialogMode::ResizeBoard;
    const bool chooseDimensions = resize || state.mode == MakerDraftDialogMode::NewDraft;
    const bool discardForNew = state.mode == MakerDraftDialogMode::DiscardNew;
    const bool crop = state.mode == MakerDraftDialogMode::ConfirmCrop;
    const bool previewingResize = resize || crop;
    const bool confirmation = !save && !load && !chooseDimensions;
    const DraftDialogCopy copy = draftDialogCopy(state.mode);
    const float preferredHeight = load ? 432.0f : chooseDimensions ? 278.0f : 230.0f;
    const UiRect previewPanel = previewingResize
        ? state.resizePreviewPanel.value_or(layout(viewport, scale).palette) : UiRect {};
    const float previewPanelHeight = crop ? 300.0f : 324.0f;
    scale = previewingResize
        ? std::min({ scale, previewPanel.size.x / 190.0f, previewPanel.size.y / previewPanelHeight })
        : std::min({ scale, viewport.x / 360.0f, viewport.y / (chooseDimensions ? preferredHeight + 30.0f : 260.0f) });
    if (scale <= 0.0f) return {};

    const float inset = (previewingResize ? 14.0f : 20.0f) * scale;
    const Vec2 panelSize {
        std::min(510.0f * scale, viewport.x - 24.0f * scale),
        std::min(preferredHeight * scale, viewport.y - 24.0f * scale),
    };
    const UiRect panel = previewingResize
        ? UiRect { previewPanel.position, { previewPanel.size.x, previewPanelHeight * scale } }
        : UiRect { (viewport - panelSize) * 0.5f, panelSize };
    const float contentX = panel.position.x + inset;
    const float contentWidth = panel.size.x - inset * 2.0f;
    const float buttonY = panel.position.y + panel.size.y - 54.0f * scale;
    const float submitWidth = crop ? contentWidth : resize ? contentWidth * 0.53f
        : (confirmation ? 166.0f : 110.0f) * scale;
    const UiRect submitButton {
        { contentX + contentWidth - submitWidth, buttonY }, { submitWidth, 34.0f * scale },
    };
    const UiRect cancelButton = crop
        ? UiRect { { contentX, buttonY - 42.0f * scale }, { contentWidth, 34.0f * scale } }
        : resize
            ? UiRect { { contentX, buttonY }, { contentWidth - submitWidth - 8.0f * scale, 34.0f * scale } }
            : UiRect { { submitButton.position.x - 106.0f * scale, buttonY }, { 96.0f * scale, 34.0f * scale } };
    if (previewingResize) {
        ui.pushClip(previewPanel);
        ui.rect(previewPanel, chrome);
    } else {
        ui.rect({ {}, viewport }, { 0.012f, 0.016f, 0.017f, 0.65f });
    }
    ui.panel(panel);
    ui.text({ contentX, panel.position.y + 15.0f * scale }, copy.title, ink, 25.0f * scale);
    ui.divider({ { contentX, panel.position.y + 51.0f * scale }, { contentWidth, scale } });

    if (save) {
        ui.text({ contentX, panel.position.y + 63.0f * scale }, "Draft name", mutedInk, 15.0f * scale);
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
        draftNameCaret_ = draftNameField_.draw(ui, "maker.draft.name", draftNameBounds_, draftNameEdit_, {
            .focused = draftNameFocused_,
            .extendSelection = input.extendTextSelection,
            .pressAlreadyHandled = pressHandled,
            .placeholder = "Name this draft",
            .textSize = draftNameTextSize_,
        }).caret;
    } else if (confirmation) {
        std::string message;
        if (discardForNew) {
            message = "Create a " + std::to_string(draftWidth_) + " \xc3\x97 " +
                std::to_string(draftHeight_) + " draft and discard your unsaved changes?";
        } else if (crop) {
            message = "Resizing to " + std::to_string(draftWidth_) + " \xc3\x97 " +
                std::to_string(draftHeight_) + " will remove " + std::to_string(state.removedTileCount) +
                (state.removedTileCount == 1 ? " tile." : " tiles.");
        } else if (state.mode == MakerDraftDialogMode::Overwrite) {
            message = "Replace the saved draft \xe2\x80\x9c" + std::string(state.pendingName) + "\xe2\x80\x9d?";
        } else {
            message = "Load \xe2\x80\x9c" + std::string(state.pendingName) + "\xe2\x80\x9d and discard your unsaved changes?";
        }
        drawDialogText(ui, { { contentX, panel.position.y + 65.0f * scale },
            { contentWidth, (crop ? 67.0f : 58.0f) * scale } }, message, mutedInk, 17.0f * scale);
    }

    const bool hasDrafts = load && !state.names.empty();
    const bool submitEnabled = chooseDimensions || confirmation || hasDrafts || (save && !draftNameEdit_.text().empty());
    UiNavigation navigation = input.navigation;
    if (ui.mousePressPosition()) navigation = {};
    const bool waitingForName = save && draftNameFocused_ && !submitEnabled &&
        draftDialogFocus_ == draftSubmitId && !navigation.previous && !navigation.next;
    if (waitingForName) navigation.activate = false;
    constexpr float rowHeight = 36.0f;
    constexpr float rowPitch = 41.0f;
    FrameArray<UiControlRegion> regions(ui.frameArena(), static_cast<std::size_t>(432.0f / rowPitch) + 4);
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
    const UiRect list { { contentX, panel.position.y + 65.0f * scale },
        { contentWidth - 14.0f * scale, buttonY - panel.position.y -
            (state.error.empty() ? 77.0f : 119.0f) * scale } };
    std::size_t firstVisible = 0;
    std::size_t lastVisible = 0;
    const auto rowBounds = [&](std::size_t index) -> UiRect {
        return { { list.position.x, list.position.y +
                (static_cast<float>(index) * rowPitch - draftListScroll_.offset()) * scale },
            { list.size.x, rowHeight * scale } };
    };
    if (hasDrafts) {
        draftListSelection_ = std::min(draftListSelection_, state.names.size() - 1);
        draftListScroll_.setExtents(list.size.y / scale, static_cast<float>(state.names.size()) * rowPitch);
        draftListScroll_.scrollWheel(ui, list, input.wheelY, input.wheelPosition);
        const UiControlId lastRow = firstDraftRowId + state.names.size() - 1;
        if (draftDialogFocus_ == 0 || draftDialogFocus_ > lastRow) {
            draftDialogFocus_ = firstDraftRowId + draftListSelection_;
        }
        const bool moveFocus = navigation.previous != navigation.next;
        if (moveFocus) {
            if (navigation.next) {
                draftDialogFocus_ = draftDialogFocus_ == lastRow ? draftCancelId : draftDialogFocus_ + 1;
            } else {
                draftDialogFocus_ = draftDialogFocus_ == draftCancelId ? lastRow : draftDialogFocus_ - 1;
            }
        }
        if ((moveFocus || navigation.activate) && draftDialogFocus_ >= firstDraftRowId) {
            draftListSelection_ = static_cast<std::size_t>(draftDialogFocus_ - firstDraftRowId);
            const float top = static_cast<float>(draftListSelection_) * rowPitch;
            draftListScroll_.reveal(top, top + rowHeight);
        }
        navigation.previous = navigation.next = false;
        draftListScroll_.drawScrollbar(ui, "maker.draft.scroll", {
            { list.position.x + list.size.x + 6.0f * scale, list.position.y }, { 8.0f * scale, list.size.y },
        });
        firstVisible = static_cast<std::size_t>(draftListScroll_.offset() / rowPitch);
        lastVisible = std::min(state.names.size(),
            firstVisible + static_cast<std::size_t>(list.size.y / (rowPitch * scale)) + 2);
        ui.pushClip(list);
        for (std::size_t i = firstVisible; i < lastVisible; ++i) {
            regions.push_back({ firstDraftRowId + i, ui.visibleRect(rowBounds(i)) });
        }
        ui.popClip();
    } else if (load && state.error.empty()) {
        ui.text(list.position, "No saved drafts yet.", mutedInk, 16.0f * scale);
    }

    regions.push_back({ draftCancelId, cancelButton });
    regions.push_back({ draftSubmitId, submitButton, submitEnabled });
    const UiInteraction interaction = ui.resolveControls(copy.scope, regions, draftDialogFocus_, navigation);
    if ((!hasDrafts && !waitingForName) || interaction.pressed != 0 || interaction.activated != 0) {
        // Wheel and scrollbar movement leave logical focus on the selected draft
        // even when that row scrolls out of view.
        draftDialogFocus_ = interaction.focused;
    }
    if (hasDrafts && interaction.activated >= firstDraftRowId) {
        draftListSelection_ = static_cast<std::size_t>(interaction.activated - firstDraftRowId);
    }
    ui.pushClip(list);
    for (std::size_t i = firstVisible; i < lastVisible; ++i) {
        const UiRect row = rowBounds(i);
        const auto tone = i == draftListSelection_ ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal;
        (void)uiControls::button(ui, interaction, firstDraftRowId + i, row, "", tone);
        ui.pushClip(row);
        ui.text(row.position + Vec2 { 10.0f, 6.0f } * scale, state.names[i], ink, 17.0f * scale);
        ui.popClip();
    }
    ui.popClip();

    if (previewingResize) {
        const auto drawSwatch = [&](float y, Vec4 color, std::string_view label) {
            const Vec2 position { contentX, panel.position.y + y * scale };
            ui.rect({ position + Vec2 { 0.0f, 3.0f } * scale, Vec2 { 11.0f, 11.0f } * scale }, color);
            ui.text(position + Vec2 { 18.0f, 0.0f } * scale, label, ink, 15.0f * scale);
        };
        drawSwatch(crop ? 136.0f : 197.0f, removedTileInk, "Removed tiles");
        drawSwatch(crop ? 157.0f : 218.0f, addedTileInk, "Added ground");
    }
    const float errorY = load ? buttonY - 43.0f * scale : panel.position.y +
        (resize ? 241.0f : crop ? 178.0f : chooseDimensions ? 155.0f : 130.0f) * scale;
    const float errorBottom = crop ? cancelButton.position.y : buttonY;
    drawDialogText(ui, { { contentX, errorY }, { contentWidth, errorBottom - errorY - 5.0f * scale } },
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
    const bool cancel = drawButton(draftCancelId, cancelButton, "Cancel", uiControls::ButtonTone::Normal, true);
    const bool submit = drawButton(draftSubmitId, submitButton, copy.submitLabel,
        confirmation ? uiControls::ButtonTone::Danger : uiControls::ButtonTone::Accent, submitEnabled);
    if (previewingResize) ui.popClip();
    if (cancel) return { Command::Cancel, {} };
    const bool activateRow = hasDrafts && interaction.activated >= firstDraftRowId && interaction.released == 0;
    if (!submit && !activateRow) return {};
    if (save) return submitDraftName();
    if (load) return { Command::Submit, state.names[draftListSelection_] };
    if (chooseDimensions || discardForNew || crop) {
        return { .command = Command::Submit, .width = draftWidth_, .height = draftHeight_ };
    }
    return { Command::Submit, std::string(state.pendingName) };
}

} // namespace sokoban
