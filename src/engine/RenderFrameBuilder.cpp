#include "engine/RenderFrameBuilder.hpp"

#include "engine/Config.hpp"
#include "engine/Rules.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/RenderAssetRequirements.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sokoban {
namespace {

struct StaticRenderCell {
    TileType tile = TileType::Ground;
    bool active = true;
    bool showGrid = true;
    Vec2 size { 1.0f, 1.0f };
    Vec2 positionOffset {};
    float baseElevation = 0.0f;
    float height = 0.0f;
    uint32_t modelRotationQuarterTurns = 0;
};

void applyTileScale(RenderFrameData::Tile& tile, float scale)
{
    scale = std::clamp(scale, config::minTileScale, config::maxTileScale);
    if (std::abs(scale - 1.0f) < 0.0001f) {
        return;
    }

    const Vec2 center {
        tile.position.x + tile.size.x * 0.5f,
        tile.position.y + tile.size.y * 0.5f,
    };
    tile.size = { tile.size.x * scale, tile.size.y * scale };
    tile.position = {
        center.x - tile.size.x * 0.5f,
        center.y - tile.size.y * 0.5f,
    };
    tile.height *= scale;
}

uint32_t facingQuarterTurns(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Down:
        return 0;
    case MoveDirection::Left:
        return 1;
    case MoveDirection::Up:
        return 2;
    case MoveDirection::Right:
        return 3;
    }
    return 0;
}

StaticRenderCell staticRenderCellFor(
    const Level& level,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    bool endUnlocked,
    std::optional<TileType> fallenTile,
    float surfaceEntityHeight,
    float surfaceEntitySize,
    uint32_t playerFacingQuarterTurns)
{
    const TileType tile = fallenTile.value_or(level.tileAt(x, y, z));
    const bool surfaceEntity = tileTypeIsSurfaceEntity(tile);
    const bool conveyor = tileTypeIsConveyor(tile);
    const bool submergedEntity = fallenTile.has_value();
    const float centeredOffset = (1.0f - surfaceEntitySize) * 0.5f;
    return {
        .tile = tile,
        .active = tile != TileType::End || endUnlocked,
        .showGrid = tile != TileType::Player,
        .size = surfaceEntity
            ? Vec2 { surfaceEntitySize, surfaceEntitySize }
            : Vec2 { 1.0f, 1.0f },
        .positionOffset = surfaceEntity
            ? Vec2 { centeredOffset, centeredOffset }
            : Vec2 {},
        .baseElevation = static_cast<float>(z) +
            (tile == TileType::Water ? 1.0f - 2.0f * config::waterDepthBelowGround : 0.0f) -
            (submergedEntity ? config::waterDepthBelowGround : 0.0f),
        .height = surfaceEntity
            ? surfaceEntityHeight
            : (tile == TileType::Water
                    ? config::waterDepthBelowGround
                    : (conveyor
                            ? config::conveyorTileHeight
                            : (tileTypeIsSolidBlock(tile) ||
                                      tileTypeOccupiesLevelCell(tile)
                                    ? 1.0f
                                    : 0.0f))),
        .modelRotationQuarterTurns = tile == TileType::Player
            ? playerFacingQuarterTurns
            : (rules::conveyorDirectionForTile(tile)
                    ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
                    : 0),
    };
}

Vec4 shade(Vec4 color, float multiplier)
{
    return {
        color.x * multiplier,
        color.y * multiplier,
        color.z * multiplier,
        color.w,
    };
}

void appendLadderRungFace(
    RenderFrameData& frame,
    GridPosition3 groundCell,
    GridPosition3 ladderCell,
    float rungCenter,
    bool preview)
{
    constexpr float rungLengthInset = 0.10f;
    constexpr float rungHalfThickness = 0.07f;
    constexpr float faceOffset = 0.003f;

    const Vec4 color = preview
        ? Vec4 { 0.43f, 0.22f, 0.08f, 0.62f }
        : tileColor(TileType::Ladder);
    const float bottom =
        static_cast<float>(groundCell.z) + rungCenter - rungHalfThickness;
    const float top =
        static_cast<float>(groundCell.z) + rungCenter + rungHalfThickness;
    const float gx = static_cast<float>(groundCell.x);
    const float gy = static_cast<float>(groundCell.y);

    auto appendFace = [&](std::array<Vec3, 4> vertices, Vec3 normal) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .normal = normal,
            .color = color,
        });
    };

    if (ladderCell.x < groundCell.x) {
        const float x = gx - faceOffset;
        const float y0 = gy + rungLengthInset;
        const float y1 = gy + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x, y1, bottom },
            Vec3 { x, y0, bottom },
            Vec3 { x, y0, top },
            Vec3 { x, y1, top },
        }, { -1.0f, 0.0f, 0.0f });
        return;
    }
    if (ladderCell.x > groundCell.x) {
        const float x = gx + 1.0f + faceOffset;
        const float y0 = gy + rungLengthInset;
        const float y1 = gy + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x, y0, bottom },
            Vec3 { x, y1, bottom },
            Vec3 { x, y1, top },
            Vec3 { x, y0, top },
        }, { 1.0f, 0.0f, 0.0f });
        return;
    }
    if (ladderCell.y < groundCell.y) {
        const float y = gy - faceOffset;
        const float x0 = gx + rungLengthInset;
        const float x1 = gx + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x0, y, bottom },
            Vec3 { x1, y, bottom },
            Vec3 { x1, y, top },
            Vec3 { x0, y, top },
        }, { 0.0f, -1.0f, 0.0f });
        return;
    }
    if (ladderCell.y > groundCell.y) {
        const float y = gy + 1.0f + faceOffset;
        const float x0 = gx + rungLengthInset;
        const float x1 = gx + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x1, y, bottom },
            Vec3 { x0, y, bottom },
            Vec3 { x0, y, top },
            Vec3 { x1, y, top },
        }, { 0.0f, 1.0f, 0.0f });
    }
}

void appendLadderRungs(
    RenderFrameData& frame,
    GridPosition3 ladderCell,
    GridPosition3 groundCell,
    bool preview = false)
{
    appendLadderRungFace(frame, groundCell, ladderCell, 0.32f, preview);
    appendLadderRungFace(frame, groundCell, ladderCell, 0.68f, preview);
}

template <typename TileAt>
void appendLadderRungsForCell(
    RenderFrameData& frame,
    GridPosition3 ladderCell,
    TileAt tileAt,
    bool preview = false)
{
    if (tileAt(ladderCell) != TileType::Ladder) {
        return;
    }
    constexpr std::array<GridPosition, 4> offsets {
        GridPosition { 0, -1 },
        GridPosition { 1, 0 },
        GridPosition { 0, 1 },
        GridPosition { -1, 0 },
    };
    for (GridPosition offset : offsets) {
        const GridPosition3 groundCell {
            ladderCell.x + offset.x,
            ladderCell.y + offset.y,
            ladderCell.z,
        };
        if (tileAt(groundCell) == TileType::Ground) {
            appendLadderRungs(frame, ladderCell, groundCell, preview);
        }
    }
}

void appendWaterEdgeFaces(
    RenderFrameData& frame,
    uint32_t width,
    uint32_t height,
    float layerElevation,
    const auto& isUnfilledWaterAt)
{
    const float bottom = layerElevation - config::waterDepthBelowGround;
    const float top = layerElevation;
    const Vec4 color = shade(tileColor(TileType::Ground), 0.78f);

    auto appendEdge = [&](std::array<Vec3, 4> vertices, Vec3 normal, Vec4 edgeColor) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .normal = normal,
            .color = edgeColor,
        });
    };
    auto neighborIsOpenWater = [&](GridPosition position) {
        return position.x >= 0 &&
            position.y >= 0 &&
            position.x < static_cast<int>(width) &&
            position.y < static_cast<int>(height) &&
            isUnfilledWaterAt(position);
    };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const GridPosition position {
                static_cast<int>(x),
                static_cast<int>(y),
            };
            if (!isUnfilledWaterAt(position)) {
                continue;
            }
            const float left = static_cast<float>(x);
            const float right = left + 1.0f;
            const float nearY = static_cast<float>(y);
            const float farY = nearY + 1.0f;

            if (!neighborIsOpenWater({ position.x, position.y - 1 })) {
                appendEdge({
                    Vec3 { left, nearY, bottom },
                    Vec3 { right, nearY, bottom },
                    Vec3 { right, nearY, top },
                    Vec3 { left, nearY, top },
                }, { 0.0f, -1.0f, 0.0f }, shade(color, 0.92f));
            }
            if (!neighborIsOpenWater({ position.x + 1, position.y })) {
                appendEdge({
                    Vec3 { right, nearY, bottom },
                    Vec3 { right, farY, bottom },
                    Vec3 { right, farY, top },
                    Vec3 { right, nearY, top },
                }, { 1.0f, 0.0f, 0.0f }, shade(color, 0.82f));
            }
            if (!neighborIsOpenWater({ position.x, position.y + 1 })) {
                appendEdge({
                    Vec3 { right, farY, bottom },
                    Vec3 { left, farY, bottom },
                    Vec3 { left, farY, top },
                    Vec3 { right, farY, top },
                }, { 0.0f, 1.0f, 0.0f }, shade(color, 0.70f));
            }
            if (!neighborIsOpenWater({ position.x - 1, position.y })) {
                appendEdge({
                    Vec3 { left, farY, bottom },
                    Vec3 { left, nearY, bottom },
                    Vec3 { left, nearY, top },
                    Vec3 { left, farY, top },
                }, { -1.0f, 0.0f, 0.0f }, shade(color, 0.82f));
            }
        }
    }
}

template <typename CellAt, typename ScaleForTile>
void appendStaticTiles(
    RenderFrameData& frame,
    const Level& level,
    CellAt cellAt,
    ScaleForTile scaleForTile)
{
    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const StaticRenderCell cell = cellAt(x, y, z);
                if (cell.tile == TileType::Air || cell.tile == TileType::Ladder) {
                    continue;
                }
                RenderFrameData::Tile renderTile {
                    .cell = {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    },
                    .position = {
                        static_cast<float>(x) + cell.positionOffset.x,
                        static_cast<float>(y) + cell.positionOffset.y,
                    },
                    .size = cell.size,
                    .color = cell.tile == TileType::Player
                        ? Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }
                        : tileColor(cell.tile, cell.active),
                    .baseElevation = cell.baseElevation,
                    .height = cell.height,
                    .showGrid = cell.showGrid,
                    .model = renderModelForTile(cell.tile),
                    .modelRotationQuarterTurns = cell.modelRotationQuarterTurns,
                };
                applyTileScale(renderTile, scaleForTile(cell.tile));
                frame.tiles.push_back(renderTile);
            }
        }
    }
}

} // namespace

RenderFrameData RenderFrameBuilder::buildGameplay(const GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& playerVisual = input.presentation.player();
    const auto& movableVisuals = input.presentation.movables();
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.lighting = input.settings.renderLighting();
    frame.gridOverlay = input.settings.renderGridOverlay();
    frame.levelWidth = input.level.width();
    frame.levelHeight = input.level.height();
    frame.levelDepth = input.level.depth();
    frame.playerPosition = {
        playerVisual.motion.renderPosition.x,
        playerVisual.motion.renderPosition.y,
    };
    const bool endUnlocked = rules::isEndUnlocked(input.level, state);

    frame.tiles.reserve(
        static_cast<std::size_t>(input.level.width()) *
        input.level.height() *
        input.level.depth());
    const bool playerMovingOutOfWater =
        input.moving &&
        input.activeAction.before.playerDead &&
        !input.activeAction.after.playerDead;
    auto fallenMovableIsMoving =
        [&state, &movableVisuals](const GameState::Movable* movable) {
            const auto index =
                static_cast<std::size_t>(movable - state.movables.data());
            return index < movableVisuals.size() && movableVisuals[index].moving;
        };
    auto staticCellAt =
        [&](uint32_t x, uint32_t y, uint32_t z) {
            const GridPosition3 position {
                static_cast<int>(x),
                static_cast<int>(y),
                static_cast<int>(z),
            };
            if (input.level.tileAt(x, y, z) == TileType::Water) {
                const GridPosition3 entityPosition {
                    position.x,
                    position.y,
                    position.z + 1,
                };
                if (const GameState::Movable* fallenMovable =
                        rules::fallenMovableAt(state, entityPosition)) {
                    if (!fallenMovableIsMoving(fallenMovable)) {
                        return StaticRenderCell { .tile = TileType::Air };
                    }
                }
            }

            std::optional<TileType> fallenTile;
            if (const GameState::Movable* fallenMovable =
                    rules::fallenMovableAt(state, position)) {
                if (!fallenMovableIsMoving(fallenMovable)) {
                    return StaticRenderCell {
                        .tile = fallenMovable->type,
                        .showGrid = true,
                        .baseElevation =
                            static_cast<float>(std::max(position.z - 1, 0)),
                        .height = 1.0f,
                    };
                }
            } else if (
                state.playerDead &&
                !playerMovingOutOfWater &&
                position == state.player) {
                fallenTile = TileType::Player;
            }

            return staticRenderCellFor(
                input.level,
                x,
                y,
                z,
                endUnlocked,
                fallenTile,
                input.settings.geometry.surfaceEntityHeight,
                input.settings.geometry.surfaceEntityWidthDepth,
                playerVisual.facingQuarterTurns);
        };
    appendStaticTiles(
        frame,
        input.level,
        staticCellAt,
        [&](TileType tile) {
            return input.settings.tileScale(tile);
        });

    auto levelTileAt = [&](GridPosition3 position) {
        if (!input.level.inBounds(position)) {
            return TileType::Air;
        }
        return input.level.tileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    };
    for (uint32_t z = 0; z < input.level.depth(); ++z) {
        for (uint32_t y = 0; y < input.level.height(); ++y) {
            for (uint32_t x = 0; x < input.level.width(); ++x) {
                appendLadderRungsForCell(
                    frame,
                    {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    },
                    levelTileAt);
            }
        }
    }
    for (uint32_t z = 0; z < input.level.depth(); ++z) {
        appendWaterEdgeFaces(
            frame,
            input.level.width(),
            input.level.height(),
            static_cast<float>(z) + 1.0f,
            [&, z](GridPosition position) {
                return rules::isUnfilledWater(input.level, state, {
                    position.x,
                    position.y,
                    static_cast<int>(z) + 1,
                });
            });
    }

    if (!state.playerDead || playerMovingOutOfWater) {
        RenderFrameData::Tile playerTile {
            .position = {
                playerVisual.motion.renderPosition.x,
                playerVisual.motion.renderPosition.y,
            },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
            .baseElevation = playerVisual.motion.renderPosition.z,
            .height = 1.0f,
            .showGrid = false,
            .model = RenderModel::Rogue,
            .animation = playerVisual.motion.moving
                ? playerVisual.movingClip
                : RenderAnimation::RogueIdle,
            .animationTimeSeconds = playerVisual.clipTimeSeconds,
            .modelRotationQuarterTurns = playerVisual.facingQuarterTurns,
        };
        applyTileScale(
            playerTile,
            input.settings.tileScale(TileType::Player));
        frame.tiles.push_back(playerTile);
    }

    for (std::size_t movableIndex = 0;
         movableIndex < state.movables.size() &&
         movableIndex < movableVisuals.size();
         ++movableIndex) {
        const GameState::Movable& movable = state.movables[movableIndex];
        const GameplayPresentation::EntityVisual& visual =
            movableVisuals[movableIndex];
        const bool movingOutOfWater =
            input.moving &&
            movableIndex < input.activeAction.before.movables.size() &&
            movableIndex < input.activeAction.after.movables.size() &&
            input.activeAction.before.movables[movableIndex].fallen &&
            !input.activeAction.after.movables[movableIndex].fallen;
        if (movable.fallen && !visual.moving && !movingOutOfWater) {
            continue;
        }

        Vec4 color = tileColor(movable.type);
        if (movable.type == TileType::Ice) {
            color.w = config::iceTintAlpha;
        }
        RenderFrameData::Tile movableTile {
            .position = {
                visual.renderPosition.x,
                visual.renderPosition.y,
            },
            .color = color,
            .baseElevation = visual.renderPosition.z,
            .height = 1.0f,
            .blurBehind = movable.type == TileType::Ice,
            .model = renderModelForTile(movable.type),
        };
        applyTileScale(
            movableTile,
            input.settings.tileScale(movable.type));
        frame.tiles.push_back(movableTile);
    }

    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.model == RenderModel::Conveyor) {
            tile.beltScrollOffset = input.conveyorBeltScrollOffset;
        }
    }
    return frame;
}

RenderFrameData RenderFrameBuilder::buildEditor(const EditorInput& input)
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.lighting = input.settings.renderLighting();
    frame.gridOverlay = input.settings.renderGridOverlay();
    frame.levelWidth = input.editor.documentWidth();
    frame.levelHeight = input.editor.documentHeight();

    const Level::LayerRows& layers = input.editor.documentLayers();
    const uint32_t activeLayer = input.editor.activeLayer();
    const uint32_t layerCount = static_cast<uint32_t>(layers.size());
    const bool layerLocked = input.editor.layerLocked();
    frame.levelDepth = std::max(layerCount, 1U);
    frame.tiles.reserve(
        static_cast<std::size_t>(frame.levelWidth) *
            frame.levelHeight *
            layerCount *
            2 +
        2);

    auto documentTileAt = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (z >= layers.size() ||
            y >= layers[z].size() ||
            x >= layers[z][y].size()) {
            return TileType::Air;
        }
        return charToTileType(layers[z][y][x]).value_or(TileType::Air);
    };
    auto documentTileAtPosition = [&](GridPosition3 position) {
        if (position.x < 0 || position.y < 0 || position.z < 0) {
            return TileType::Air;
        }
        return documentTileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    };
    auto appendEditorTile =
        [&](uint32_t x, uint32_t y, uint32_t z, TileType tile, bool preview) {
            if (tile == TileType::Air) {
                return;
            }
            if (tile == TileType::Ladder) {
                auto tileAtForLadder = [&](GridPosition3 position) {
                    if (preview &&
                        position.x == static_cast<int>(x) &&
                        position.y == static_cast<int>(y) &&
                        position.z == static_cast<int>(z)) {
                        return TileType::Ladder;
                    }
                    return documentTileAtPosition(position);
                };
                appendLadderRungsForCell(
                    frame,
                    {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    },
                    tileAtForLadder,
                    preview);
                return;
            }

            const bool surfaceEntity = tileTypeIsSurfaceEntity(tile);
            const bool conveyor = tileTypeIsConveyor(tile);
            const float tileSize = surfaceEntity
                ? input.settings.geometry.surfaceEntityWidthDepth
                : 1.0f;
            const float centeredOffset = (1.0f - tileSize) * 0.5f;
            Vec4 color = tileColor(tile);
            if (tile == TileType::Player) {
                color = { 1.0f, 1.0f, 1.0f, 1.0f };
            }
            if (tile == TileType::Ice) {
                color.w = config::iceTintAlpha;
            }

            const float previewOffset = preview ? 0.02f : 0.0f;
            RenderFrameData::Tile renderTile {
                .cell = {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                },
                .position = {
                    static_cast<float>(x) + centeredOffset,
                    static_cast<float>(y) + centeredOffset,
                },
                .size = { tileSize, tileSize },
                .color = color,
                .baseElevation = static_cast<float>(z) +
                    (tile == TileType::Water
                            ? 1.0f - 2.0f * config::waterDepthBelowGround
                            : 0.0f) +
                    previewOffset,
                .height = surfaceEntity
                    ? input.settings.geometry.surfaceEntityHeight
                    : (tile == TileType::Water
                            ? config::waterDepthBelowGround
                            : (conveyor
                                    ? config::conveyorTileHeight
                                    : (tileTypeIsSolidBlock(tile) ||
                                              tileTypeOccupiesLevelCell(tile)
                                            ? 1.0f
                                            : 0.0f))),
                .blurBehind = tile == TileType::Ice,
                .showGrid = tile != TileType::Player,
                .isEditorPreview = preview,
                .model = renderModelForTile(tile),
                .animation = tile == TileType::Player
                    ? RenderAnimation::RogueIdle
                    : RenderAnimation::None,
                .animationTimeSeconds = tile == TileType::Player
                    ? input.worldAnimationTimeSeconds
                    : 0.0f,
                .modelRotationQuarterTurns =
                    rules::conveyorDirectionForTile(tile)
                    ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
                    : 0,
            };
            applyTileScale(renderTile, input.settings.tileScale(tile));
            frame.tiles.push_back(renderTile);
        };

    for (uint32_t z = 0; z < layerCount; ++z) {
        if (layerLocked && z != activeLayer) {
            continue;
        }
        for (uint32_t y = 0; y < frame.levelHeight; ++y) {
            for (uint32_t x = 0; x < frame.levelWidth; ++x) {
                if (input.hoverCell &&
                    input.hoverCell->z == static_cast<int>(z) &&
                    input.hoverCell->x == static_cast<int>(x) &&
                    input.hoverCell->y == static_cast<int>(y)) {
                    continue;
                }

                const TileType tile = documentTileAt(x, y, z);
                if (layerLocked && tile == TileType::Air) {
                    frame.tiles.push_back({
                        .cell = {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        },
                        .position = {
                            static_cast<float>(x),
                            static_cast<float>(y),
                        },
                        .baseElevation = static_cast<float>(z),
                        .pickOnly = true,
                    });
                    continue;
                }
                if (!layerLocked && z == 0 && tile == TileType::Air) {
                    bool columnEmpty = true;
                    for (uint32_t layer = 1;
                         layer < layerCount && columnEmpty;
                         ++layer) {
                        columnEmpty =
                            documentTileAt(x, y, layer) == TileType::Air;
                    }
                    if (columnEmpty) {
                        frame.tiles.push_back({
                            .cell = {
                                static_cast<int>(x),
                                static_cast<int>(y),
                                0,
                            },
                            .position = {
                                static_cast<float>(x),
                                static_cast<float>(y),
                            },
                            .baseElevation = 0.0f,
                            .pickOnly = true,
                        });
                        continue;
                    }
                }
                appendEditorTile(x, y, z, tile, false);
            }
        }
    }

    auto documentHasOpenWater = [&](GridPosition position, uint32_t z) {
        if (position.x < 0 ||
            position.y < 0 ||
            position.x >= static_cast<int>(frame.levelWidth) ||
            position.y >= static_cast<int>(frame.levelHeight)) {
            return false;
        }
        return documentTileAt(
                   static_cast<uint32_t>(position.x),
                   static_cast<uint32_t>(position.y),
                   z) == TileType::Water;
    };
    for (uint32_t z = 0; z < layerCount; ++z) {
        if (layerLocked && z != activeLayer) {
            continue;
        }
        appendWaterEdgeFaces(
            frame,
            frame.levelWidth,
            frame.levelHeight,
            static_cast<float>(z) + 1.0f,
            [&, z](GridPosition position) {
                return documentHasOpenWater(position, z);
            });
    }

    if (input.hoverCell &&
        input.hoverCell->x >= 0 &&
        input.hoverCell->y >= 0 &&
        input.hoverCell->x < static_cast<int>(frame.levelWidth) &&
        input.hoverCell->y < static_cast<int>(frame.levelHeight)) {
        const TileType selectedTile = input.deleting
            ? TileType::Air
            : input.editor.selectedTile();
        const TileType hoveredTile = documentTileAt(
            static_cast<uint32_t>(input.hoverCell->x),
            static_cast<uint32_t>(input.hoverCell->y),
            static_cast<uint32_t>(input.hoverCell->z));
        const TileType previewTile =
            selectedTile == TileType::Air ? hoveredTile : selectedTile;
        appendEditorTile(
            static_cast<uint32_t>(input.hoverCell->x),
            static_cast<uint32_t>(input.hoverCell->y),
            static_cast<uint32_t>(input.hoverCell->z),
            previewTile,
            true);
    }

    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.model == RenderModel::Conveyor) {
            tile.beltScrollOffset = input.conveyorBeltScrollOffset;
        }
    }
    return frame;
}

} // namespace sokoban
