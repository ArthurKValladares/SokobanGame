#include "engine/ui/MakerView.hpp"

#include "engine/EditorTilePalette.hpp"
#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace sokoban {
namespace {

constexpr float margin = 12.0f;
constexpr float compactToolbarHeight = 96.0f;
constexpr float wideToolbarHeight = 62.0f;
constexpr float wideToolbarWidth = 940.0f;
constexpr float footerHeight = 34.0f;
constexpr float tileHeight = 32.0f;
constexpr float tileGap = 4.0f;
constexpr float groupHeight = 28.0f;
constexpr float groupGap = 9.0f;

constexpr Vec4 background { 0.061f, 0.078f, 0.081f, 1.0f };
constexpr Vec4 chrome { 0.089f, 0.112f, 0.115f, 1.0f };
constexpr Vec4 divider { 0.22f, 0.30f, 0.29f, 1.0f };
constexpr Vec4 ink { 0.94f, 0.95f, 0.90f, 1.0f };
constexpr Vec4 mutedInk { 0.68f, 0.75f, 0.72f, 1.0f };
constexpr Vec4 accentInk { 0.54f, 0.86f, 0.77f, 1.0f };
constexpr Vec4 buttonInk { 0.92f, 0.94f, 0.92f, 1.0f };
constexpr Vec4 disabledButtonInk { 0.50f, 0.52f, 0.51f, 0.72f };

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
    bool selected = false;
};

std::array<ToolbarControl, 8> toolbarControls(const MakerLayout& layout, const MakerViewState& state)
{
    const float scale = layout.scale;
    const float height = 34.0f * scale;
    const float gap = 6.0f * scale;
    const float inset = margin * scale;
    const float width = layout.toolbar.size.x;
    const bool wide = width >= wideToolbarWidth * scale;
    const float backWidth = std::min(88.0f * scale, width * 0.22f);
    const UiRect back { { width - inset - backWidth, 14.0f * scale }, { backWidth, height } };
    const float toolsStart = wide ? 222.0f * scale : inset;
    const float toolsEnd = wide ? back.position.x - 16.0f * scale : width - inset;
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
    ui.text(Vec2 { margin + 4.0f, 17.0f } * scale, "Sokoban Maker", ink, 24.0f * scale);
}

struct TileControl {
    TileType tile;
    UiRect bounds;
};

float paletteContentHeight()
{
    return static_cast<float>(groups.size()) * (groupHeight + groupGap) +
        static_cast<float>(paletteTileCount) * (tileHeight + tileGap);
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

MakerViewAction MakerView::draw(UiContext& ui, const MakerLayout& layout,
    const MakerViewState& state, const MakerViewInput& input)
{
    const float scale = layout.scale;
    drawChrome(ui, layout);
    const auto controls = toolbarControls(layout, state);
    const UiRect list {
        layout.palette.position + Vec2 { 10.0f, 48.0f } * scale,
        { std::max(0.0f, layout.palette.size.x - 28.0f * scale),
            std::max(0.0f, layout.palette.size.y - 59.0f * scale) },
    };
    const UiRect scrollbar {
        { list.position.x + list.size.x + 5.0f * scale, list.position.y },
        { 6.0f * scale, list.size.y },
    };
    // Keep the scroll offset in logical units across monitor scale changes.
    paletteScroll_.setExtents(list.size.y / scale, paletteContentHeight());
    paletteScroll_.scrollWheel(ui, layout.palette, input.wheelY, input.wheelPosition);
    paletteScroll_.drawScrollbar(ui, "maker.palette.scroll", scrollbar);

    std::array<UiControlRegion, controls.size() + paletteTileCount> regions;
    std::size_t regionCount = 0;
    for (const auto& control : controls) {
        regions[regionCount++] = { commandId(control.command), control.bounds, control.enabled };
    }
    std::array<TileControl, paletteTileCount> tiles;
    std::array<UiRect, groups.size()> labels;
    std::size_t tileIndex = 0;
    float y = list.position.y - paletteScroll_.offset() * scale;
    ui.pushClip(list);
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const auto& group = groups[index];
        labels[index] = { { list.position.x, y }, { list.size.x, groupHeight * scale } };
        y += groupHeight * scale;
        for (TileType tile : group.tiles) {
            const UiRect bounds { { list.position.x, y }, { list.size.x, tileHeight * scale } };
            tiles[tileIndex++] = { tile, bounds };
            regions[regionCount++] = { tileId(tile), ui.visibleRect(bounds) };
            y += (tileHeight + tileGap) * scale;
        }
        y += groupGap * scale;
    }
    ui.popClip();

    const UiInteraction interaction =
        ui.resolveControls("Maker", regions, focus_, input.navigation);
    focus_ = interaction.focused;
    MakerViewAction result;
    for (const auto& control : controls) {
        ui.pushClip(control.bounds);
        if (uiControls::button(ui, control.bounds, "", {
                .tone = control.selected ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
                .focused = focus_ == commandId(control.command),
                .activate = interaction.activated == commandId(control.command),
                .enabled = control.enabled,
                .contentScale = 0.72f * scale,
            })) {
            result.command = control.command;
        }
        ui.centeredText(control.bounds, control.label,
            control.enabled ? buttonInk : disabledButtonInk, 24.0f * 0.72f * scale);
        ui.popClip();
    }
    const auto& lowerLayer = controls[5].bounds;
    const auto& higherLayer = controls[6].bounds;
    const float layerLeft = lowerLayer.position.x + lowerLayer.size.x;
    ui.centeredText({ { layerLeft, lowerLayer.position.y },
                        { higherLayer.position.x - layerLeft, lowerLayer.size.y } },
        "Layer " + std::to_string(state.layer + 1), ink, 15.0f * scale);

    ui.pushClip(layout.palette);
    ui.text(layout.palette.position + Vec2 { 12.0f, 10.0f } * scale, "Tiles", ink, 23.0f * scale);
    ui.divider({ layout.palette.position + Vec2 { 10.0f, 42.0f } * scale,
        { std::max(0.0f, layout.palette.size.x - 20.0f * scale), scale } });
    ui.pushClip(list);
    for (std::size_t index = 0; index < groups.size(); ++index) {
        ui.text(labels[index].position + Vec2 { 2.0f, 4.0f } * scale,
            groups[index].label, mutedInk, 15.0f * scale);
    }
    for (const auto& [tile, bounds] : tiles) {
        const auto visible = ui.visibleRect(bounds);
        if (visible.size.y <= 0.0f) continue;
        const bool selected = !state.erasing && selectedTile(tile, state.brush);
        if (uiControls::button(ui, bounds, "", {
                .tone = selected ? uiControls::ButtonTone::Accent : uiControls::ButtonTone::Normal,
                .focused = focus_ == tileId(tile),
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
    std::string sessionLabel = "This session only";
    if (state.hovered) {
        sessionLabel = "X " + std::to_string(state.hovered->x) +
            "  Y " + std::to_string(state.hovered->y) + "  \xc2\xb7  " + sessionLabel;
    }
    const float sessionWidth = ui.measureText(sessionLabel, footerText).x;
    const float rightWidth = std::min(sessionWidth + footerInset * 2.0f, layout.footer.size.x - leftWidth);
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
    return result;
}

} // namespace sokoban
