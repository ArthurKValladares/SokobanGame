#include "engine/RenderFrameBuilder.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/Rules.hpp"
#include "engine/TileTypes.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/SelectorRenderConfig.hpp"
#include "engine/render/MirrorConfig.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sokoban {
namespace {

RenderAnimation animationFor(
    const AnimationCatalog* catalog,
    AnimationUse use,
    RenderAnimation fallback)
{
    return catalog != nullptr ? catalog->animation(use) : fallback;
}

float animationTimeFor(
    const AnimationCatalog* catalog,
    AnimationUse use,
    float timeSeconds)
{
    return catalog != nullptr
        ? timeSeconds * catalog->effectiveSpeed(use)
        : timeSeconds;
}

RenderAnimation manifestAnimationForUse(
    const AssetManifest& manifest,
    AnimationUse use)
{
    switch (use) {
    case AnimationUse::PlayerIdle:
    case AnimationUse::EnemyIdle:
        return manifest.playerIdleAnimation();
    case AnimationUse::PlayerMove:
        return manifest.playerMoveAnimation();
    case AnimationUse::PlayerPush:
        return manifest.playerPushAnimation();
    case AnimationUse::PlayerDeath:
        return manifest.playerDeathAnimation();
    case AnimationUse::PlayerDeadIdle:
        return manifest.playerDeadIdleAnimation();
    case AnimationUse::EnemyAttack:
        return manifest.enemyAttackAnimation();
    default:
        return noAnimation;
    }
}

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
        .baseElevation = static_cast<float>(z) -
            (submergedEntity ? config::waterDepthBelowGround : 0.0f),
        .height = surfaceEntity
            ? surfaceEntityHeight
            : (conveyor
                    ? config::conveyorTileHeight
                    : (tileTypeIsSolidBlock(tile) ||
                              tileTypeOccupiesLevelCell(tile) ||
                              tileTypeIsMirror(tile) ||
                              tileTypeIsDecorative(tile)
                            ? 1.0f
                            : 0.0f)),
        .modelRotationQuarterTurns = tile == TileType::Player
            ? playerFacingQuarterTurns
            : (rules::conveyorDirectionForTile(tile)
                    ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
                    : mirrorOrientationQuarterTurns(tile).value_or(0)),
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
        return isUnfilledWaterAt(position);
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

template <typename TileAt>
std::optional<RenderFrameData::CameraExtent> gameplayExtentForTiles(
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    TileAt tileAt);

void includeCameraCell(
    std::optional<RenderFrameData::CameraExtent>& extent,
    GridPosition3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0) {
        return;
    }
    if (!extent) {
        extent = RenderFrameData::CameraExtent {
            .originX = cell.x,
            .originY = cell.y,
            .originZ = cell.z,
        };
        return;
    }

    const int maxX = std::max(
        extent->originX + static_cast<int>(extent->width),
        cell.x + 1);
    const int maxY = std::max(
        extent->originY + static_cast<int>(extent->height),
        cell.y + 1);
    const int maxZ = std::max(
        extent->originZ + static_cast<int>(extent->depth),
        cell.z + 1);
    extent->originX = std::min(extent->originX, cell.x);
    extent->originY = std::min(extent->originY, cell.y);
    extent->originZ = std::min(extent->originZ, cell.z);
    extent->width = static_cast<uint32_t>(maxX - extent->originX);
    extent->height = static_cast<uint32_t>(maxY - extent->originY);
    extent->depth = static_cast<uint32_t>(maxZ - extent->originZ);
}

template <typename TileAt>
std::optional<RenderFrameData::CameraExtent> gameplayExtentForTiles(
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    TileAt tileAt)
{
    std::optional<RenderFrameData::CameraExtent> extent;
    for (uint32_t z = 0; z < depth; ++z) {
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                if (tileTypeAffectsCameraFit(tileAt(x, y, z))) {
                    includeCameraCell(
                        extent,
                        {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        });
                }
            }
        }
    }
    return extent;
}

RenderFrameData::WaterGridBounds waterGridBoundsFor(
    const std::optional<RenderFrameData::CameraExtent>& gameplayExtent)
{
    if (!gameplayExtent) {
        return {};
    }
    return {
        .originX = gameplayExtent->originX,
        .originY = gameplayExtent->originY,
        .width = gameplayExtent->width,
        .height = gameplayExtent->height,
    };
}

struct MirrorRenderSegment {
    Vec3 from {};
    Vec3 to {};
    float opacity = 1.0f;
};

Vec3 toRenderPoint(GridPosition3 point)
{
    return {
        static_cast<float>(point.x),
        static_cast<float>(point.y),
        static_cast<float>(point.z),
    };
}

Vec3 interpolate(Vec3 from, Vec3 to, float amount)
{
    return {
        from.x + (to.x - from.x) * amount,
        from.y + (to.y - from.y) * amount,
        from.z + (to.z - from.z) * amount,
    };
}

void appendMirrorBeamPrism(
    RenderFrameData& frame,
    const MirrorRenderSegment& segment,
    float width,
    Vec4 color)
{
    const Vec2 from {
        segment.from.x + 0.5f,
        segment.from.y + 0.5f,
    };
    const Vec2 to {
        segment.to.x + 0.5f,
        segment.to.y + 0.5f,
    };
    const Vec2 delta { to.x - from.x, to.y - from.y };
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.0001f) {
        return;
    }

    const Vec2 perpendicular {
        -delta.y / length * width * 0.5f,
        delta.x / length * width * 0.5f,
    };
    const float bottom = segment.from.z +
        config::mirrorBeamElevation - config::mirrorBeamThickness * 0.5f;
    const float top = bottom + config::mirrorBeamThickness;
    const std::array<Vec3, 8> corners {
        Vec3 { from.x - perpendicular.x, from.y - perpendicular.y, bottom },
        Vec3 { from.x + perpendicular.x, from.y + perpendicular.y, bottom },
        Vec3 { to.x + perpendicular.x, to.y + perpendicular.y, bottom },
        Vec3 { to.x - perpendicular.x, to.y - perpendicular.y, bottom },
        Vec3 { from.x - perpendicular.x, from.y - perpendicular.y, top },
        Vec3 { from.x + perpendicular.x, from.y + perpendicular.y, top },
        Vec3 { to.x + perpendicular.x, to.y + perpendicular.y, top },
        Vec3 { to.x - perpendicular.x, to.y - perpendicular.y, top },
    };
    auto appendFace = [&](std::array<Vec3, 4> vertices) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .color = color,
            .effect = RenderSurfaceEffect::MirrorEnergy,
        });
    };
    appendFace({ corners[4], corners[7], corners[6], corners[5] });
    appendFace({ corners[0], corners[4], corners[5], corners[1] });
    appendFace({ corners[3], corners[2], corners[6], corners[7] });
    appendFace({ corners[0], corners[3], corners[7], corners[4] });
    appendFace({ corners[1], corners[5], corners[6], corners[2] });
}

bool sameUndirectedSegment(
    const MirrorRenderSegment& left,
    const MirrorRenderSegment& right)
{
    auto samePoint = [](Vec3 first, Vec3 second) {
        return std::abs(first.x - second.x) < 0.0001f &&
            std::abs(first.y - second.y) < 0.0001f &&
            std::abs(first.z - second.z) < 0.0001f;
    };
    return (samePoint(left.from, right.from) &&
               samePoint(left.to, right.to)) ||
        (samePoint(left.from, right.to) &&
            samePoint(left.to, right.from));
}

void appendWaterSurface(
    RenderFrameData& frame,
    GridPosition3 cell,
    Vec2 position,
    Vec2 size,
    bool editorPreview = false,
    uint32_t shorelineMask = 0,
    bool pickable = true)
{
    frame.waterSurfaces.push_back({
        .cell = cell,
        .position = position,
        .size = size,
        .color = frame.waterRendering.surfaceColor,
        .elevation = static_cast<float>(cell.z) + 1.0f -
            config::waterDepthBelowGround +
            (editorPreview ? 0.02f : 0.0f),
        .shorelineMask = shorelineMask,
        .isEditorPreview = editorPreview,
        .pickable = pickable,
    });
}

void appendWaterCellSurface(
    RenderFrameData& frame,
    GridPosition3 cell,
    bool editorPreview = false,
    uint32_t shorelineMask = 0,
    bool pickable = true)
{
    appendWaterSurface(
        frame,
        cell,
        {
            static_cast<float>(cell.x),
            static_cast<float>(cell.y),
        },
        { 1.0f, 1.0f },
        editorPreview,
        shorelineMask,
        pickable);
}

uint32_t shorelineMaskForWaterCell(
    GridPosition3 cell,
    const auto& isShorelineAt)
{
    struct Neighbor {
        GridPosition offset;
        WaterShorelineEdge edge;
    };
    constexpr std::array<Neighbor, 4> neighbors {
        Neighbor { { 0, -1 }, WaterShorelineEdge::NegativeY },
        Neighbor { { 1, 0 }, WaterShorelineEdge::PositiveX },
        Neighbor { { 0, 1 }, WaterShorelineEdge::PositiveY },
        Neighbor { { -1, 0 }, WaterShorelineEdge::NegativeX },
    };

    uint32_t mask = 0;
    std::array<bool, 4> shorelineEdges {};
    std::size_t neighborIndex = 0;
    for (const Neighbor& neighbor : neighbors) {
        const bool shoreline = isShorelineAt({
            cell.x + neighbor.offset.x,
            cell.y + neighbor.offset.y,
            cell.z,
        });
        shorelineEdges[neighborIndex++] = shoreline;
        if (shoreline) {
            mask |= waterShorelineBit(neighbor.edge);
        }
    }

    struct Corner {
        GridPosition offset;
        std::size_t firstEdge;
        std::size_t secondEdge;
        WaterShorelineCorner corner;
    };
    constexpr std::array<Corner, 4> corners {
        Corner {
            { -1, -1 },
            3,
            0,
            WaterShorelineCorner::NegativeXNegativeY,
        },
        Corner {
            { 1, -1 },
            1,
            0,
            WaterShorelineCorner::PositiveXNegativeY,
        },
        Corner {
            { 1, 1 },
            1,
            2,
            WaterShorelineCorner::PositiveXPositiveY,
        },
        Corner {
            { -1, 1 },
            3,
            2,
            WaterShorelineCorner::NegativeXPositiveY,
        },
    };
    for (const Corner& corner : corners) {
        if (!shorelineEdges[corner.firstEdge] &&
            !shorelineEdges[corner.secondEdge] &&
            isShorelineAt({
                cell.x + corner.offset.x,
                cell.y + corner.offset.y,
                cell.z,
            })) {
            mask |= waterShorelineBit(corner.corner);
        }
    }
    return mask;
}

void appendUnboundedWaterExterior(
    RenderFrameData& frame,
    uint32_t width,
    uint32_t height,
    uint32_t waterLayer,
    bool editorPreview,
    const auto& isShorelineAt)
{
    const int right = static_cast<int>(width);
    const int bottom = static_cast<int>(height);
    const int z = static_cast<int>(waterLayer);
    auto appendExteriorCell = [&](int x, int y) {
        const GridPosition3 cell { x, y, z };
        appendWaterCellSurface(
            frame,
            cell,
            editorPreview,
            shorelineMaskForWaterCell(cell, isShorelineAt),
            false);
    };

    for (int x = -1; x <= right; ++x) {
        appendExteriorCell(x, -1);
        appendExteriorCell(x, bottom);
    }
    for (int y = 0; y < bottom; ++y) {
        appendExteriorCell(-1, y);
        appendExteriorCell(right, y);
    }

    const float boardWidth = static_cast<float>(width);
    const float boardHeight = static_cast<float>(height);
    const float margin = std::max(
        std::max(boardWidth, boardHeight) *
            config::waterExteriorMarginScale,
        config::waterExteriorMinimumMarginTiles);
    const float continuation = std::max(margin - 1.0f, 0.0f);
    static_assert(config::waterExteriorMinimumMarginTiles > 1.0f);

    auto appendContinuation = [&](Vec2 position, Vec2 size) {
        appendWaterSurface(
            frame,
            { static_cast<int>(position.x),
              static_cast<int>(position.y),
              z },
            position,
            size,
            editorPreview,
            0,
            false);
    };
    appendContinuation(
        { -margin, -margin },
        { continuation, boardHeight + margin * 2.0f });
    appendContinuation(
        { boardWidth + 1.0f, -margin },
        { continuation, boardHeight + margin * 2.0f });
    appendContinuation(
        { -1.0f, -margin },
        { boardWidth + 2.0f, continuation });
    appendContinuation(
        { -1.0f, boardHeight + 1.0f },
        { boardWidth + 2.0f, continuation });
}

// Ground splat textures are optional: a manifest without them leaves the ids
// unset and ground falls back to the flat tile color. `location` selects that
// screen's splat map; an empty location (the editor) takes the shared map.
[[nodiscard]] GroundSplatTextures groundSplatTextures(
    const AssetManifest& manifest,
    std::optional<LevelLocation> location)
{
    return groundSplatTexturesForScreen(
        [&manifest](std::string_view name) {
            return manifest.findTextureIdByName(name);
        },
        location);
}

template <typename CellAt, typename ScaleForTile>
void appendStaticTiles(
    RenderFrameData& frame,
    const AssetManifest& manifest,
    const Level& level,
    CellAt cellAt,
    ScaleForTile scaleForTile)
{
    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const StaticRenderCell cell = cellAt(x, y, z);
                if (cell.tile == TileType::Air ||
                    cell.tile == TileType::Ladder ||
                    cell.tile == TileType::Water) {
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
                    .affectsCameraFit =
                        tileTypeAffectsCameraFit(cell.tile),
                    .model = manifest.modelForTile(cell.tile),
                    .modelRotationQuarterTurns = cell.modelRotationQuarterTurns,
                    .modelRotationOffsetRadians =
                        tileTypeIsMirror(cell.tile)
                        ? config::mirrorModelRotationOffsetRadians
                        : 0.0f,
                    // Procedural ground tops blend grass/rock through the
                    // splat map; modelled tiles keep their own materials.
                    .effect = cell.tile == TileType::Ground
                        ? RenderSurfaceEffect::GroundSplat
                        : RenderSurfaceEffect::Standard,
                };
                applyTileScale(renderTile, scaleForTile(cell.tile));
                frame.tiles.push_back(renderTile);
            }
        }
    }
}

float yawRadians(Vec4 orientation)
{
    return 2.0f * std::atan2(orientation.z, orientation.w);
}

uint64_t actorAnimationInstance(EntityTarget target)
{
    return target.id;
}

uint64_t mirrorGhostAnimationInstance(std::size_t resultPlayerIndex)
{
    return (uint64_t { 1 } << 62) |
        (static_cast<uint64_t>(resultPlayerIndex) + 1);
}

uint64_t authoredAnimationInstance(TileType tile, GridPosition3 cell)
{
    const uint64_t x = static_cast<uint32_t>(cell.x);
    const uint64_t y = static_cast<uint32_t>(cell.y);
    const uint64_t z = static_cast<uint32_t>(cell.z);
    return (uint64_t { 1 } << 63) |
        (static_cast<uint64_t>(tile) << 56) |
        ((z & 0xffffU) << 40) |
        ((y & 0xfffffU) << 20) |
        (x & 0xfffffU);
}

RenderFrameData::Tile decorationVisual(
    const Level::Decoration& decoration,
    const AssetManifest& manifest,
    bool preview,
    std::optional<std::size_t> editorIndex = std::nullopt,
    RenderFrameData::EditorDecorationHighlight highlight =
        RenderFrameData::EditorDecorationHighlight::None)
{
    constexpr float radiansPerDegree =
        3.14159265358979323846f / 180.0f;
    const RenderModel model = manifest.modelIdByName(decoration.model);
    const Vec3 pivot = manifest.model(model).preserveSourceScale
        ? Vec3 { 0.0f, 0.0f, 0.0f }
        : Vec3 { 0.5f, 0.5f, 0.0f };
    return {
        .cell = {
            static_cast<int>(std::floor(decoration.position.x)),
            static_cast<int>(std::floor(decoration.position.y)),
            static_cast<int>(std::floor(decoration.position.z)),
        },
        .position = {
            decoration.position.x - decoration.scale.x * 0.5f,
            decoration.position.y - decoration.scale.y * 0.5f,
        },
        .size = { decoration.scale.x, decoration.scale.y },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        .baseElevation = decoration.position.z,
        .height = decoration.scale.z,
        .pickable = false,
        .showGrid = false,
        .isEditorPreview = preview,
        .affectsCameraFit = false,
        .model = model,
        .modelTransform = RenderFrameData::ModelTransform {
            .translation = decoration.position,
            .rotationRadians = {
                decoration.rotationDegrees.x * radiansPerDegree,
                decoration.rotationDegrees.y * radiansPerDegree,
                decoration.rotationDegrees.z * radiansPerDegree,
            },
            .scale = decoration.scale,
            .pivot = pivot,
        },
        .editorDecorationIndex = editorIndex
            ? std::optional<uint32_t>(static_cast<uint32_t>(*editorIndex))
            : std::nullopt,
        .editorDecorationHighlight = highlight,
    };
}

void appendDecorations(
    RenderFrameData& frame,
    const std::vector<Level::Decoration>& decorations,
    const AssetManifest& manifest,
    std::optional<std::size_t> selected = std::nullopt,
    std::optional<std::size_t> hovered = std::nullopt,
    bool editorDecorations = false)
{
    for (std::size_t index = 0; index < decorations.size(); ++index) {
        const RenderFrameData::EditorDecorationHighlight highlight =
            selected == index
            ? RenderFrameData::EditorDecorationHighlight::Selected
            : (hovered == index
                ? RenderFrameData::EditorDecorationHighlight::Hovered
                : RenderFrameData::EditorDecorationHighlight::None);
        frame.tiles.push_back(decorationVisual(
            decorations[index],
            manifest,
            false,
            editorDecorations ? std::optional<std::size_t>(index) : std::nullopt,
            highlight));
    }
}

RenderFrameData initializeGameplayFrame(
    const RenderFrameBuilder::GameplayInput& input,
    FrameArena* arena = nullptr)
{
    const auto& primaryPlayerVisual = input.presentation.players().front();
    RenderFrameData frame = arena != nullptr
        ? RenderFrameData(*arena)
        : RenderFrameData {};
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.cameraPitchDegrees = input.cameraPitchDegrees;
    frame.lighting = input.settings.renderLighting();
    frame.gridOverlay = input.settings.renderGridOverlay();
    frame.waterRendering = input.settings.water;
    frame.levelWidth = input.level.width();
    frame.levelHeight = input.level.height();
    frame.levelDepth = input.level.depth();
    const std::optional<RenderFrameData::CameraExtent> gameplayExtent =
        gameplayExtentForTiles(
        input.level.width(),
        input.level.height(),
        input.level.depth(),
        [&](uint32_t x, uint32_t y, uint32_t z) {
            return input.level.authoredTileAt(x, y, z);
        });
    std::optional<RenderFrameData::CameraExtent> authoredGameplayExtent =
        gameplayExtent;
    includeCameraCell(authoredGameplayExtent, input.level.playerStart());
    for (const Level::MovableTile& movable : input.level.movableTiles()) {
        includeCameraCell(authoredGameplayExtent, movable.position);
    }
    for (GridPosition3 enemy : input.level.enemyStarts()) {
        includeCameraCell(authoredGameplayExtent, enemy);
    }
    frame.waterGridBounds = waterGridBoundsFor(authoredGameplayExtent);
    frame.cameraExtent = authoredGameplayExtent.value_or(
        RenderFrameData::CameraExtent {});
    frame.groundSplat = groundSplatTextures(input.manifest, input.levelLocation);
    frame.waterAnimationTimeSeconds =
        input.presentation.worldAnimationTimeSeconds();
    frame.effectAnimationTimeSeconds =
        input.presentation.worldAnimationTimeSeconds();
    frame.playerPosition = {
        primaryPlayerVisual.motion.renderPosition.x,
        primaryPlayerVisual.motion.renderPosition.y,
    };
    return frame;
}

void appendSelectors(
    RenderFrameData& frame,
    const std::vector<Level::ScreenSelector>& selectors,
    const AssetManifest& manifest,
    const std::function<bool(LevelLocation)>& solved)
{
    constexpr float flagScale = 0.65f;
    for (const Level::ScreenSelector& selector : selectors) {
        const bool completed = selector.target && solved && solved(*selector.target);
        const RenderModel model = manifest.modelIdByName(
            completed
                ? selectorRender::solvedModelName
                : selectorRender::unsolvedModelName);
        const Vec3 translation {
            static_cast<float>(selector.cell.x) + 0.22f,
            static_cast<float>(selector.cell.y) + 0.22f,
            static_cast<float>(selector.cell.z),
        };
        frame.tiles.push_back({
            .cell = selector.cell,
            .position = { translation.x, translation.y },
            .size = { flagScale, flagScale },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
            .baseElevation = translation.z,
            .height = flagScale * 1.5f,
            .pickable = false,
            .showGrid = false,
            .affectsCameraFit = false,
            .model = model,
            .modelTransform = RenderFrameData::ModelTransform {
                .translation = translation,
                .scale = { flagScale, flagScale, flagScale },
                .pivot = { 0.0f, 0.0f, 0.0f },
            },
        });
    }
}

void appendGameplayWorld(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& primaryPlayerVisual = input.presentation.players().front();
    const auto& movableVisuals = input.presentation.movables();
    const bool endUnlocked = rules::isEndUnlocked(input.level, state);

    frame.tiles.reserve(
        static_cast<std::size_t>(input.level.width()) *
        input.level.height() *
        input.level.depth());
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
            }

            return staticRenderCellFor(
                input.level,
                x,
                y,
                z,
                endUnlocked,
                std::nullopt,
                input.settings.geometry.surfaceEntityHeight,
                input.settings.geometry.surfaceEntityWidthDepth,
                primaryPlayerVisual.facingQuarterTurns);
        };
    appendStaticTiles(
        frame,
        input.manifest,
        input.level,
        staticCellAt,
        [&](TileType tile) {
            return input.settings.tileScale(tile);
        });
    appendDecorations(
        frame, input.level.decorations(), input.manifest);
    appendSelectors(
        frame,
        input.level.selectors(),
        input.manifest,
        input.selectorSolved);

    auto levelTileAt = [&](GridPosition3 position) {
        if (!input.level.inBounds(position)) {
            return TileType::Air;
        }
        return input.level.tileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    };
    auto gameplayShorelineAt = [&](GridPosition3 position) {
        if (!input.level.inBounds(position)) {
            return false;
        }
        const TileType tile = levelTileAt(position);
        if (tileTypeIsSolidBlock(tile)) {
            return true;
        }
        if (tile != TileType::Water) {
            return false;
        }
        return !rules::isUnfilledWater(input.level, state, {
            position.x,
            position.y,
            position.z + 1,
        });
    };
    for (uint32_t z = 0; z < input.level.depth(); ++z) {
        for (uint32_t y = 0; y < input.level.height(); ++y) {
            for (uint32_t x = 0; x < input.level.width(); ++x) {
                if (rules::isUnfilledWater(input.level, state, {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z) + 1,
                    })) {
                    const GridPosition3 waterCell {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    };
                    appendWaterCellSurface(
                        frame,
                        waterCell,
                        false,
                        shorelineMaskForWaterCell(
                            waterCell,
                            gameplayShorelineAt));
                }
            }
        }
    }
    if (input.level.waterLayer()) {
        appendUnboundedWaterExterior(
            frame,
            input.level.width(),
            input.level.height(),
            *input.level.waterLayer(),
            false,
            gameplayShorelineAt);
    }

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
                if (input.level.waterLayer() == z &&
                    (position.x < 0 ||
                     position.y < 0 ||
                     position.x >= static_cast<int>(input.level.width()) ||
                     position.y >= static_cast<int>(input.level.height()))) {
                    return true;
                }
                return rules::isUnfilledWater(input.level, state, {
                    position.x,
                    position.y,
                    static_cast<int>(z) + 1,
                });
            });
    }
}

void appendGameplayEntities(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& playerVisuals = input.presentation.players();
    const auto& movableVisuals = input.presentation.movables();

    for (std::size_t playerIndex = 0;
         playerIndex < state.players.size() &&
         playerIndex < playerVisuals.size();
         ++playerIndex) {
        const GameplayPresentation::PlayerVisual& visual = playerVisuals[playerIndex];
        const AnimationUse animationUse = visual.animationUse;
        RenderAnimation animation = animationFor(
            input.animations,
            animationUse,
            manifestAnimationForUse(input.manifest, animationUse));
        const AnimationUse fallbackUse =
            visual.animationFallbackUse.value_or(animationUse);
        const RenderAnimation fallback = visual.animationFallbackUse
            ? animationFor(
                  input.animations,
                  fallbackUse,
                  manifestAnimationForUse(input.manifest, fallbackUse))
            : noAnimation;

        RenderFrameData::Tile playerTile {
            .position = {
                visual.motion.renderPosition.x,
                visual.motion.renderPosition.y,
            },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
            .baseElevation = visual.motion.renderPosition.z,
            .height = 1.0f,
            .showGrid = false,
            .affectsCameraFit = false,
            .model = input.manifest.playerModel(),
            .animation = animation,
            .animationFallback = fallback,
            .animationInstanceId = actorAnimationInstance(visual.motion.target),
            .animationLoops = visual.animationLoops,
            .animationCrossfades = visual.animationCrossfades,
            .animationTimeSeconds = animationTimeFor(
                input.animations, animationUse, visual.clipTimeSeconds),
            .animationFallbackTimeSeconds = animationTimeFor(
                input.animations, fallbackUse, visual.clipTimeSeconds),
            .modelRotationQuarterTurns = visual.facingQuarterTurns,
        };
        applyTileScale(
            playerTile,
            input.settings.tileScale(TileType::Player));
        frame.tiles.push_back(playerTile);
    }

    const auto& enemyVisuals = input.presentation.enemies();
    for (std::size_t enemyIndex = 0;
         enemyIndex < state.enemies.size() && enemyIndex < enemyVisuals.size();
         ++enemyIndex) {
        const GameState::Enemy& enemy = state.enemies[enemyIndex];
        const GameplayPresentation::EnemyVisual& visual = enemyVisuals[enemyIndex];
        if (enemy.fallen && !visual.motion.moving) {
            continue;
        }
        RenderFrameData::Tile enemyTile {
            .position = {
                visual.motion.renderPosition.x,
                visual.motion.renderPosition.y,
            },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
            .baseElevation = visual.motion.renderPosition.z,
            .height = 1.0f,
            .showGrid = false,
            .affectsCameraFit = false,
            .model = input.manifest.enemyModel(),
            .animation = animationFor(
                input.animations,
                visual.animationUse,
                manifestAnimationForUse(input.manifest, visual.animationUse)),
            .animationFallback = visual.animationFallbackUse
                ? animationFor(
                      input.animations,
                      *visual.animationFallbackUse,
                      manifestAnimationForUse(
                          input.manifest,
                          *visual.animationFallbackUse))
                : noAnimation,
            .animationInstanceId = actorAnimationInstance(visual.motion.target),
            .animationLoops = visual.animationLoops,
            .animationCrossfades = visual.animationCrossfades,
            .animationTimeSeconds = animationTimeFor(
                input.animations,
                visual.animationUse,
                visual.clipTimeSeconds),
            .animationFallbackTimeSeconds = animationTimeFor(
                input.animations,
                visual.animationFallbackUse.value_or(visual.animationUse),
                visual.clipTimeSeconds),
            .modelRotationOffsetRadians = yawRadians(visual.orientation),
        };
        applyTileScale(enemyTile, input.settings.tileScale(TileType::Enemy));
        frame.tiles.push_back(enemyTile);
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
            movableIndex < input.projectedState.movables.size() &&
            movable.fallen &&
            !input.projectedState.movables[movableIndex].fallen;
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
            .affectsCameraFit = false,
            .model = input.manifest.modelForTile(movable.type),
        };
        applyTileScale(
            movableTile,
            input.settings.tileScale(movable.type));
        frame.tiles.push_back(movableTile);
    }
}

void appendMirrorPreview(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& playerVisuals = input.presentation.players();
    const auto& movableVisuals = input.presentation.movables();

    if (!rules::anyPlayerDead(state)) {
        std::optional<rules::MirrorActivationPreview> mirrorPreview =
            rules::previewMirrorActivation(input.level, state);
        std::optional<rules::MirrorActivationPreview> actionEndPreview;
        if (input.moving &&
            !rules::anyPlayerDead(input.projectedState)) {
            actionEndPreview = rules::previewMirrorActivation(
                input.level, input.projectedState);
        }
        if (mirrorPreview) {
            std::vector<MirrorRenderSegment> beamSegments;
            for (const rules::MirrorEntityPreview& entity :
                 mirrorPreview->entities) {
                const rules::MirrorEntityPreview* matchingEndEntity = nullptr;
                if (actionEndPreview) {
                    const auto match = std::ranges::find_if(
                        actionEndPreview->entities,
                        [&](const rules::MirrorEntityPreview& candidate) {
                            return candidate.player == entity.player &&
                                (entity.player
                                    ? candidate.playerIndex ==
                                            entity.playerIndex &&
                                        candidate.reflectionIndex ==
                                            entity.reflectionIndex
                                    :
                                    candidate.movableIndex ==
                                        entity.movableIndex);
                        });
                    if (match != actionEndPreview->entities.end()) {
                        matchingEndEntity = &*match;
                    }
                }
                const bool hasMatchingEndEntity =
                    matchingEndEntity != nullptr;

                const GameplayPresentation::PlayerVisual* previewPlayer =
                    entity.player && entity.playerIndex < playerVisuals.size()
                    ? &playerVisuals[entity.playerIndex]
                    : nullptr;
                const GameplayPresentation::EntityVisual* visual =
                    entity.player
                    ? (previewPlayer ? &previewPlayer->motion : nullptr)
                    : (entity.movableIndex < movableVisuals.size()
                            ? &movableVisuals[entity.movableIndex]
                            : nullptr);
                const float progress =
                    visual && visual->animationDuration > 0.0001f
                    ? std::clamp(
                          visual->animationElapsed /
                              visual->animationDuration,
                          0.0f,
                          1.0f)
                    : 0.0f;

                std::vector<MirrorRenderSegment> entitySegments;
                entitySegments.reserve(entity.beamSegments.size());
                const bool sameMirrorPath =
                    hasMatchingEndEntity &&
                    matchingEndEntity->beamSegments.size() ==
                        entity.beamSegments.size() &&
                    [&] {
                        for (std::size_t segmentIndex = 0;
                             segmentIndex < entity.beamSegments.size();
                             segmentIndex += 2) {
                            if (!(entity.beamSegments[segmentIndex].to ==
                                    matchingEndEntity
                                        ->beamSegments[segmentIndex].to)) {
                                return false;
                            }
                        }
                        return true;
                    }();
                const bool visualOnIncomingSightline =
                    visual && !entity.beamSegments.empty() &&
                    [&] {
                        const GridPosition3 start =
                            entity.beamSegments.front().from;
                        const GridPosition3 mirror =
                            entity.beamSegments.front().to;
                        const Vec3 position = visual->renderPosition;
                        if (std::abs(
                                position.z -
                                static_cast<float>(start.z)) > 0.0001f) {
                            return false;
                        }
                        if (start.x == mirror.x) {
                            const float mirrorToStart =
                                static_cast<float>(start.y - mirror.y);
                            const float mirrorToPosition =
                                position.y - static_cast<float>(mirror.y);
                            return std::abs(
                                       position.x -
                                       static_cast<float>(start.x)) <
                                    0.0001f &&
                                mirrorToStart * mirrorToPosition >= 0.0f;
                        }
                        const float mirrorToStart =
                            static_cast<float>(start.x - mirror.x);
                        const float mirrorToPosition =
                            position.x - static_cast<float>(mirror.x);
                        return std::abs(
                                   position.y -
                                   static_cast<float>(start.y)) <
                                0.0001f &&
                            mirrorToStart * mirrorToPosition >= 0.0f;
                    }();
                const bool animatePreview =
                    sameMirrorPath && visualOnIncomingSightline;
                float previewOpacity = 1.0f;
                if (input.moving && !animatePreview) {
                    const float fadeProgress = std::clamp(
                        progress /
                            std::max(
                                config::mirrorPreviewExitFadeProgress,
                                0.0001f),
                        0.0f,
                        1.0f);
                    const float smoothFade =
                        fadeProgress * fadeProgress *
                        (3.0f - 2.0f * fadeProgress);
                    previewOpacity = 1.0f - smoothFade;
                }
                if (previewOpacity <= 0.001f) {
                    continue;
                }
                for (std::size_t segmentIndex = 0;
                     segmentIndex < entity.beamSegments.size();
                     ++segmentIndex) {
                    MirrorRenderSegment segment {
                        .from = toRenderPoint(
                            entity.beamSegments[segmentIndex].from),
                        .to = toRenderPoint(
                            entity.beamSegments[segmentIndex].to),
                        .opacity = previewOpacity,
                    };
                    if (animatePreview) {
                        segment.from = interpolate(
                            segment.from,
                            toRenderPoint(
                                matchingEndEntity
                                    ->beamSegments[segmentIndex].from),
                            progress);
                        segment.to = interpolate(
                            segment.to,
                            toRenderPoint(
                                matchingEndEntity
                                    ->beamSegments[segmentIndex].to),
                            progress);
                    }
                    entitySegments.push_back(segment);
                }
                if (animatePreview && !entitySegments.empty()) {
                    entitySegments.front().from = visual->renderPosition;
                }
                for (const MirrorRenderSegment& segment : entitySegments) {
                    const auto existing = std::ranges::find_if(
                        beamSegments,
                        [&](const MirrorRenderSegment& candidate) {
                            return sameUndirectedSegment(
                                candidate, segment);
                        });
                    if (existing == beamSegments.end()) {
                        beamSegments.push_back(segment);
                    } else {
                        existing->opacity = std::max(
                            existing->opacity, segment.opacity);
                    }
                }

                auto ghostRenderPosition =
                    [](const rules::MirrorEntityPreview& preview) {
                        Vec3 result = toRenderPoint(preview.destination);
                        if (preview.fallen) {
                            result.z -= preview.player
                                ? config::drownedPlayerDepthBelowGround
                                : config::waterDepthBelowGround;
                        }
                        return result;
                    };
                Vec3 ghostPosition = ghostRenderPosition(entity);
                bool ghostFallen = entity.fallen;
                GridPosition3 ghostCell = entity.destination;
                if (animatePreview) {
                    ghostPosition = interpolate(
                        ghostPosition,
                        ghostRenderPosition(*matchingEndEntity),
                        progress);
                    if (progress >= 0.5f) {
                        ghostFallen = matchingEndEntity->fallen;
                        ghostCell = matchingEndEntity->destination;
                    }
                }
                RenderFrameData::Tile ghost {
                    .cell = ghostCell,
                    .position = {
                        ghostPosition.x,
                        ghostPosition.y,
                    },
                    .color = {
                        config::mirrorGhostColor.x,
                        config::mirrorGhostColor.y,
                        config::mirrorGhostColor.z,
                        config::mirrorGhostColor.w * previewOpacity,
                    },
                    .baseElevation = ghostPosition.z,
                    .height = 1.0f,
                    .showGrid = false,
                    .affectsCameraFit = false,
                    .model = entity.player
                        ? input.manifest.playerModel()
                        : input.manifest.modelForTile(
                              state.movables[entity.movableIndex].type),
                    .animation = entity.player
                        ? (ghostFallen
                                ? animationFor(
                                      input.animations,
                                      AnimationUse::MirrorPreviewPlayerDeadIdle,
                                      input.manifest.playerDeadIdleAnimation())
                                : animationFor(
                                      input.animations,
                                      AnimationUse::MirrorPreviewPlayerIdle,
                                      input.manifest.playerIdleAnimation()))
                        : noAnimation,
                    .animationInstanceId = entity.player
                        ? mirrorGhostAnimationInstance(
                              entity.resultPlayerIndex)
                        : uint64_t { 0 },
                    .animationLoops = true,
                    .animationTimeSeconds = previewPlayer
                        ? animationTimeFor(
                              input.animations,
                              ghostFallen
                                  ? AnimationUse::MirrorPreviewPlayerDeadIdle
                                  : AnimationUse::MirrorPreviewPlayerIdle,
                              previewPlayer->clipTimeSeconds)
                        : 0.0f,
                    .modelRotationQuarterTurns = entity.player
                        ? (previewPlayer
                                ? previewPlayer->facingQuarterTurns
                                : 0U)
                        : 0U,
                    .effect = RenderSurfaceEffect::MirrorEnergy,
                };
                applyTileScale(
                    ghost,
                    input.settings.tileScale(
                        entity.player
                            ? TileType::Player
                            : state.movables[entity.movableIndex].type));
                frame.tiles.push_back(ghost);
            }

            for (const MirrorRenderSegment& segment : beamSegments) {
                Vec4 haloColor = config::mirrorBeamHaloColor;
                haloColor.w *= segment.opacity;
                appendMirrorBeamPrism(
                    frame,
                    segment,
                    config::mirrorBeamHaloWidth,
                    haloColor);
                Vec4 coreColor = config::mirrorBeamCoreColor;
                coreColor.w *= segment.opacity;
                appendMirrorBeamPrism(
                    frame,
                    segment,
                    config::mirrorBeamCoreWidth,
                    coreColor);
            }
        }
    }
}

void applyScrollingMaterials(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (!tile.model.isCube() &&
            input.manifest.model(tile.model).hasScrollingMaterial()) {
            tile.beltScrollOffset = input.conveyorBeltScrollOffset;
        }
    }
}

} // namespace

RenderFrameData RenderFrameBuilder::buildGameplay(const GameplayInput& input)
{
    RenderFrameData frame = initializeGameplayFrame(input);
    appendGameplayWorld(frame, input);
    appendGameplayEntities(frame, input);
    appendMirrorPreview(frame, input);
    applyScrollingMaterials(frame, input);
    return frame;
}

RenderFrameData RenderFrameBuilder::buildGameplay(
    const GameplayInput& input,
    FrameArena& arena)
{
    RenderFrameData frame = initializeGameplayFrame(input, &arena);
    appendGameplayWorld(frame, input);
    appendGameplayEntities(frame, input);
    appendMirrorPreview(frame, input);
    applyScrollingMaterials(frame, input);
    return frame;
}

namespace {

class EditorFrameBuild {
public:
    explicit EditorFrameBuild(
        const RenderFrameBuilder::EditorInput& input,
        FrameArena* arena = nullptr)
        : input_(input)
        , arena_(arena)
        , layers_(input.editor.documentLayers())
        , activeLayer_(input.editor.activeLayer())
        , layerCount_(static_cast<uint32_t>(layers_.size()))
        , waterLayer_(input.editor.waterLayer())
        , layerLocked_(input.editor.layerLocked())
    {
    }

    [[nodiscard]] RenderFrameData build()
    {
        RenderFrameData frame = initializeEditorFrame();
        appendEditorLayers(frame);
        appendEditorPreviews(frame);
        appendEditorCamera(frame);
        applyEditorScrollingMaterials(frame);
        return frame;
    }

private:
    [[nodiscard]] RenderFrameData initializeEditorFrame() const
    {
        RenderFrameData frame = arena_ != nullptr
            ? RenderFrameData(*arena_)
            : RenderFrameData {};
        frame.viewMode = RenderViewMode::Isometric3D;
        frame.lighting = input_.settings.renderLighting();
        frame.gridOverlay = input_.settings.renderGridOverlay();
        frame.waterRendering = input_.settings.water;
        frame.levelWidth = input_.editor.documentWidth();
        frame.levelHeight = input_.editor.documentHeight();
        frame.levelDepth = std::max(layerCount_, 1U);
        frame.gridPickBorder = 1;
        // Previews the edited screen's own map, so what the brush paints is
        // what is on screen. A scratch document belongs to no screen and falls
        // back to the shared map.
        frame.groundSplat =
            groundSplatTextures(input_.manifest, input_.levelLocation);
        frame.waterAnimationTimeSeconds = input_.worldAnimationTimeSeconds;
        frame.effectAnimationTimeSeconds = input_.worldAnimationTimeSeconds;
        frame.tiles.reserve(
            static_cast<std::size_t>(frame.levelWidth) *
                frame.levelHeight *
                layerCount_ *
                2 +
            input_.editor.decorations().size() + 2);

        return frame;
    }

    void appendEditorCamera(RenderFrameData& frame) const
    {
        const auto gameplayExtent = gameplayExtentForTiles(
            frame.levelWidth,
            frame.levelHeight,
            layerCount_,
            [this](uint32_t x, uint32_t y, uint32_t z) {
                return documentTileAt(x, y, z);
            });
        frame.cameraExtent =
            gameplayExtent.value_or(RenderFrameData::CameraExtent {});
        frame.waterGridBounds = waterGridBoundsFor(gameplayExtent);
    }

    [[nodiscard]] TileType documentTileAt(
        uint32_t x,
        uint32_t y,
        uint32_t z) const
    {
        if (z >= layers_.size() ||
            y >= layers_[z].size() ||
            x >= layers_[z][y].size()) {
            return TileType::Air;
        }
        const TileType authored =
            charToTileType(layers_[z][y][x]).value_or(TileType::Air);
        if (authored == TileType::Air && waterLayer_ == z) {
            return TileType::Water;
        }
        return authored;
    }

    [[nodiscard]] TileType documentTileAt(GridPosition3 position) const
    {
        if (position.x < 0 || position.y < 0 || position.z < 0) {
            return TileType::Air;
        }
        return documentTileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    }

    void appendEditorTile(
        RenderFrameData& frame,
        int x,
        int y,
        int z,
        TileType tile,
        bool preview,
        bool pickOnly = false) const
    {
        if (tile == TileType::Air) {
            return;
        }
        if (tile == TileType::Water) {
            if (pickOnly) {
                frame.tiles.push_back({
                    .cell = { x, y, z },
                    .position = {
                        static_cast<float>(x),
                        static_cast<float>(y),
                    },
                    .baseElevation = static_cast<float>(z) + 1.0f -
                        config::waterDepthBelowGround,
                    .pickOnly = true,
                    .showGrid = false,
                    .affectsCameraFit = false,
                });
                return;
            }
            const GridPosition3 waterCell { x, y, z };
            appendWaterCellSurface(
                frame,
                waterCell,
                preview,
                shorelineMaskForWaterCell(
                    waterCell,
                    [this](GridPosition3 position) {
                        return tileTypeIsSolidBlock(
                            documentTileAt(position));
                    }));
            return;
        }
        if (tile == TileType::Ladder) {
            if (pickOnly) {
                frame.tiles.push_back({
                    .cell = { x, y, z },
                    .position = {
                        static_cast<float>(x),
                        static_cast<float>(y),
                    },
                    .baseElevation = static_cast<float>(z) + 1.0f,
                    .pickOnly = true,
                    .showGrid = false,
                    .affectsCameraFit = false,
                });
                return;
            }
            const auto tileAtForLadder =
                [this, preview, x, y, z](GridPosition3 position) {
                    if (preview &&
                        position.x == x &&
                        position.y == y &&
                        position.z == z) {
                        return TileType::Ladder;
                    }
                    return documentTileAt(position);
                };
            appendLadderRungsForCell(
                frame,
                { x, y, z },
                tileAtForLadder,
                preview);
            return;
        }

        // Shared with the thumbnail bake, so a palette icon cannot end up
        // looking different from the tile the editor draws.
        RenderFrameData::Tile renderTile = tileVisual(
            tile, { x, y, z }, input_.manifest, input_.settings);
        renderTile.baseElevation += preview ? 0.02f : 0.0f;
        renderTile.pickOnly = pickOnly;
        renderTile.isEditorPreview = preview;
        const bool animatedActor =
            tile == TileType::Player || tile == TileType::Enemy;
        const AnimationUse editorUse = tile == TileType::Enemy
            ? AnimationUse::EditorEnemyIdle
            : AnimationUse::EditorPlayerIdle;
        renderTile.animation = animatedActor
            ? animationFor(
                  input_.animations,
                  editorUse,
                  input_.manifest.playerIdleAnimation())
            : noAnimation;
        renderTile.animationTimeSeconds = animatedActor
            ? animationTimeFor(
                  input_.animations,
                  editorUse,
                  input_.worldAnimationTimeSeconds)
            : 0.0f;
        frame.tiles.push_back(renderTile);
    }

    static void appendEditorPickCell(
        RenderFrameData& frame,
        GridPosition3 cell,
        bool affectsCameraFit = false)
    {
        frame.tiles.push_back({
            .cell = cell,
            .position = {
                static_cast<float>(cell.x),
                static_cast<float>(cell.y),
            },
            // Pick the visible top of the edited cell. Picking its lower plane
            // introduces perspective parallax against a block preview.
            .baseElevation = static_cast<float>(cell.z) + 1.0f,
            .pickOnly = true,
            .showGrid = false,
            .affectsCameraFit = affectsCameraFit,
        });
    }

    void appendExpansionPickCells(RenderFrameData& frame) const
    {
        const int expansionPickLayer = layerLocked_
            ? static_cast<int>(activeLayer_)
            : 0;
        const int editorWidth = static_cast<int>(frame.levelWidth);
        const int editorHeight = static_cast<int>(frame.levelHeight);
        for (int x = -1; x <= editorWidth; ++x) {
            appendEditorPickCell(
                frame, { x, -1, expansionPickLayer }, true);
            appendEditorPickCell(
                frame, { x, editorHeight, expansionPickLayer }, true);
        }
        for (int y = 0; y < editorHeight; ++y) {
            appendEditorPickCell(
                frame, { -1, y, expansionPickLayer }, true);
            appendEditorPickCell(
                frame, { editorWidth, y, expansionPickLayer }, true);
        }
    }

    void appendAuthoredCells(RenderFrameData& frame) const
    {
        for (uint32_t z = 0; z < layerCount_; ++z) {
            if (layerLocked_ && z != activeLayer_) {
                continue;
            }
            for (uint32_t y = 0; y < frame.levelHeight; ++y) {
                for (uint32_t x = 0; x < frame.levelWidth; ++x) {
                    const TileType tile = documentTileAt(x, y, z);
                    if (layerLocked_ && tile == TileType::Air) {
                        appendEditorPickCell(frame, {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        });
                        continue;
                    }
                    if (!layerLocked_ && z == 0 &&
                        tile == TileType::Air &&
                        columnIsEmpty(x, y)) {
                        appendEditorPickCell(frame, {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            0,
                        });
                        continue;
                    }
                    const bool deletePreviewTarget =
                        input_.deleting && input_.hoverCell &&
                        *input_.hoverCell == GridPosition3 {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        };
                    appendEditorTile(
                        frame,
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                        tile,
                        false,
                        deletePreviewTarget);
                }
            }
        }
    }

    [[nodiscard]] bool columnIsEmpty(uint32_t x, uint32_t y) const
    {
        for (uint32_t layer = 1; layer < layerCount_; ++layer) {
            if (documentTileAt(x, y, layer) != TileType::Air) {
                return false;
            }
        }
        return true;
    }

    void appendEditorWater(RenderFrameData& frame) const
    {
        if (waterLayer_ &&
            (!layerLocked_ || *waterLayer_ == activeLayer_)) {
            appendUnboundedWaterExterior(
                frame,
                frame.levelWidth,
                frame.levelHeight,
                *waterLayer_,
                false,
                [this](GridPosition3 position) {
                    return tileTypeIsSolidBlock(documentTileAt(position));
                });
        }

        for (uint32_t z = 0; z < layerCount_; ++z) {
            if (layerLocked_ && z != activeLayer_) {
                continue;
            }
            appendWaterEdgeFaces(
                frame,
                frame.levelWidth,
                frame.levelHeight,
                static_cast<float>(z) + 1.0f,
                [this, &frame, z](GridPosition position) {
                    if (position.x < 0 ||
                        position.y < 0 ||
                        position.x >=
                            static_cast<int>(frame.levelWidth) ||
                        position.y >=
                            static_cast<int>(frame.levelHeight)) {
                        return waterLayer_ == z;
                    }
                    return documentTileAt(
                               static_cast<uint32_t>(position.x),
                               static_cast<uint32_t>(position.y),
                               z) == TileType::Water;
                });
        }
    }

    void appendEditorLayers(RenderFrameData& frame) const
    {
        appendExpansionPickCells(frame);
        appendAuthoredCells(frame);
        appendEditorWater(frame);
        appendDecorations(
            frame,
            input_.editor.decorations(),
            input_.manifest,
            input_.editor.selectedDecorationIndex(),
            input_.hoverDecoration,
            true);
        appendSelectors(
            frame,
            input_.editor.selectors(),
            input_.manifest,
            input_.selectorSolved);
    }

    void appendEditorPreviews(RenderFrameData& frame) const
    {
        if (input_.editor.tool() == LevelEditor::Tool::Tiles &&
            input_.hoverCell &&
            input_.hoverCell->z >= 0 &&
            input_.hoverCell->x >= -1 &&
            input_.hoverCell->y >= -1 &&
            input_.hoverCell->x <= static_cast<int>(frame.levelWidth) &&
            input_.hoverCell->y <= static_cast<int>(frame.levelHeight)) {
            const TileType selectedTile = input_.deleting
                ? TileType::Air
                : input_.editor.selectedTile();
            const TileType hoveredTile =
                documentTileAt(*input_.hoverCell);
            const TileType previewTile = selectedTile == TileType::Air
                ? hoveredTile
                : selectedTile;
            appendEditorTile(
                frame,
                input_.hoverCell->x,
                input_.hoverCell->y,
                input_.hoverCell->z,
                previewTile,
                true);
        }

        if (input_.editor.tool() == LevelEditor::Tool::Decorations &&
            !input_.editor.selectedDecorationModel().empty() &&
            !input_.hoverDecoration &&
            input_.hoverCell &&
            input_.hoverCell->x >= 0 &&
            input_.hoverCell->y >= 0 &&
            input_.hoverCell->x < static_cast<int>(frame.levelWidth) &&
            input_.hoverCell->y < static_cast<int>(frame.levelHeight) &&
            input_.hoverCell->z >= 0) {
            frame.tiles.push_back(decorationVisual(
                {
                    .model = input_.editor.selectedDecorationModel(),
                    .position = {
                        static_cast<float>(input_.hoverCell->x) + 0.5f,
                        static_cast<float>(input_.hoverCell->y) + 0.5f,
                        static_cast<float>(input_.hoverCell->z),
                    },
                },
                input_.manifest,
                true));
        }
    }

    void applyEditorScrollingMaterials(RenderFrameData& frame) const
    {
        for (RenderFrameData::Tile& tile : frame.tiles) {
            if (!tile.model.isCube() &&
                input_.manifest.model(tile.model).hasScrollingMaterial()) {
                tile.beltScrollOffset = input_.conveyorBeltScrollOffset;
            }
        }
    }

    const RenderFrameBuilder::EditorInput& input_;
    FrameArena* arena_ = nullptr;
    const Level::LayerRows& layers_;
    uint32_t activeLayer_ = 0;
    uint32_t layerCount_ = 0;
    std::optional<uint32_t> waterLayer_;
    bool layerLocked_ = false;
};

} // namespace

RenderFrameData RenderFrameBuilder::buildEditor(const EditorInput& input)
{
    return EditorFrameBuild(input).build();
}

RenderFrameData RenderFrameBuilder::buildEditor(
    const EditorInput& input,
    FrameArena& arena)
{
    return EditorFrameBuild(input, &arena).build();
}

RenderFrameData::Tile tileVisual(
    TileType tile,
    GridPosition3 cell,
    const AssetManifest& manifest,
    const PresentationSettings& settings)
{
    const bool surfaceEntity = tileTypeIsSurfaceEntity(tile);
    const bool conveyor = tileTypeIsConveyor(tile);
    const float tileSize = surfaceEntity
        ? settings.geometry.surfaceEntityWidthDepth
        : 1.0f;
    const float centeredOffset = (1.0f - tileSize) * 0.5f;

    Vec4 color = tileColor(tile);
    if (tile == TileType::Player || tile == TileType::Enemy) {
        color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }
    if (tile == TileType::Ice) {
        color.w = config::iceTintAlpha;
    }

    RenderFrameData::Tile visual {
        .cell = cell,
        .position = {
            static_cast<float>(cell.x) + centeredOffset,
            static_cast<float>(cell.y) + centeredOffset,
        },
        .size = { tileSize, tileSize },
        .color = color,
        .baseElevation = static_cast<float>(cell.z),
        // Conveyors are the reason this is shared: they are neither a surface
        // entity nor a solid block, so anything that only tests those two ends
        // up drawing them flat.
        .height = surfaceEntity
            ? settings.geometry.surfaceEntityHeight
            : (conveyor
                    ? config::conveyorTileHeight
                    : (tileTypeIsSolidBlock(tile) ||
                              tileTypeOccupiesLevelCell(tile) ||
                              tileTypeIsMirror(tile) ||
                              tileTypeIsDecorative(tile)
                            ? 1.0f
                            : 0.0f)),
        .blurBehind = tile == TileType::Ice,
        .showGrid = tile != TileType::Player,
        .affectsCameraFit = tileTypeAffectsCameraFit(tile),
        .model = manifest.modelForTile(tile),
        .animation = tile == TileType::Player || tile == TileType::Enemy
            ? manifest.playerIdleAnimation()
            : noAnimation,
        .animationInstanceId = tile == TileType::Player || tile == TileType::Enemy
            ? authoredAnimationInstance(tile, cell)
            : uint64_t { 0 },
        // Conveyors carry their direction in the tile type; mirrors carry an
        // orientation. Both are rotations of one shared model, so dropping
        // either collapses a whole family into identical-looking tiles.
        .modelRotationQuarterTurns =
            rules::conveyorDirectionForTile(tile)
            ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
            : mirrorOrientationQuarterTurns(tile).value_or(0),
        .modelRotationOffsetRadians = tileTypeIsMirror(tile)
            ? config::mirrorModelRotationOffsetRadians
            : 0.0f,
        .effect = tile == TileType::Ground
            ? RenderSurfaceEffect::GroundSplat
            : RenderSurfaceEffect::Standard,
    };
    applyTileScale(visual, settings.tileScale(tile));
    return visual;
}

} // namespace sokoban
