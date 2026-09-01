#pragma once

#include "engine/AnimationCatalog.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The parts of a render frame that both builders build.
//
// `RenderFrameBuilder.cpp` used to hold the gameplay builder and the editor
// builder in one file, in two anonymous namespaces, and the review recorded it
// as "two independent builders that happen to be typed into one". That was
// wrong: the editor calls sixteen of the gameplay helpers directly and pulls in
// six more transitively - 22 of 40 definitions and 718 of 1698 lines, 42% of
// the helper code. They were never independent; they were one shared layer with
// two entry points sitting on top of it, and nothing said which was which.
//
// This header is that layer, named. What is left in each builder is the part
// that genuinely differs: gameplay places actors, mirrors and the static world;
// the editor places the same water, ladders, decorations and selector against a
// document being edited, with previews and pick targets.
//
// Everything here is internal to those two translation units. It lives in its
// own namespace rather than in `sokoban` because these names - `shade`,
// `animationFor`, `includeCameraCell` - are the kind that collide, and they
// had internal linkage before this split.
//
// Templates take the tile or shoreline lookup as a parameter because gameplay
// asks the level and the editor asks the document under edit. That is the only
// thing the two callers disagree about, so it is the only thing parameterised.

namespace sokoban::renderFrameParts {

// ---------------------------------------------- Small shared shaping helpers
//
// Used by both builders and by `tileVisual`. `shade` in particular is called
// from every face-appending helper below, which is why it sits first.

Vec4 shade(Vec4 color, float multiplier);

void applyTileScale(RenderFrameData::Tile& tile, float scale);

uint32_t facingQuarterTurns(MoveDirection direction);

// --------------------------------------------------------- Animation lookups
//
// A frame names animations by catalogue use, not by index, so both builders
// have to resolve the same fallbacks the same way.

RenderAnimation animationFor(
    const AnimationCatalog* catalog,
    AnimationUse use,
    RenderAnimation fallback);

float animationTimeFor(
    const AnimationCatalog* catalog,
    AnimationUse use,
    float timeSeconds);

uint64_t authoredAnimationInstance(TileType tile, GridPosition3 cell);

// ------------------------------------------------------------------- Ladders
//
// A ladder is drawn as rungs between the ladder cell and the ground beside
// it, so the geometry depends on neighbours - which is why the per-cell entry
// point is a template over however the caller answers "what tile is at".

void appendLadderRungFace(
    RenderFrameData& frame,
    GridPosition3 groundCell,
    GridPosition3 ladderCell,
    float rungCenter,
    bool preview);

void appendLadderRungs(
    RenderFrameData& frame,
    GridPosition3 ladderCell,
    GridPosition3 groundCell,
    bool preview = false);

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

// --------------------------------------------------------------------- Water
//
// The largest shared group, and the reason this header exists at all: every
// one of these is called from the gameplay builder and from the editor, and
// the three shaped as templates take a predicate because gameplay asks the
// level and the editor asks the document under edit.

void appendWaterSurface(
    RenderFrameData& frame,
    GridPosition3 cell,
    Vec2 position,
    Vec2 size,
    bool editorPreview = false,
    uint32_t shorelineMask = 0,
    bool pickable = true);

void appendWaterCellSurface(
    RenderFrameData& frame,
    GridPosition3 cell,
    bool editorPreview = false,
    uint32_t shorelineMask = 0,
    bool pickable = true);

RenderFrameData::WaterGridBounds waterGridBoundsFor(
    const std::optional<RenderFrameData::CameraExtent>& gameplayExtent);

void appendWaterEdgeFaces(
    RenderFrameData& frame,
    uint32_t width,
    uint32_t height,
    float layerElevation,
    const auto& isUnfilledWaterAt,
    const auto& shouldAppendAt)
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
            if (!shouldAppendAt(position) || !isUnfilledWaterAt(position)) {
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

// ------------------------------------------------------------- Camera extent
//
// What the camera has to contain. Templated over the tile lookup for the same
// reason the ladder helper is.

void includeCameraCell(
    std::optional<RenderFrameData::CameraExtent>& extent,
    GridPosition3 cell);

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

// -------------------------------------------------------------- Ground splat

// Ground splat textures are optional: a manifest without them leaves the ids
// unset and ground falls back to the flat tile color. `location` selects that
// screen's splat map; an empty location (the editor) takes the shared map.
[[nodiscard]] GroundSplatTextures groundSplatTextures(
    const AssetManifest& manifest,
    std::optional<LevelLocation> location);

// --------------------------------------------------------------- Decorations
//
// Decorations are authored objects rather than grid tiles, so both builders
// place them from the same list with the same rules; only the highlighting
// differs, which is what the trailing arguments are for.

RenderFrameData::Tile decorationVisual(
    const Level::Decoration& decoration,
    const AssetManifest& manifest,
    bool preview,
    std::optional<std::size_t> editorIndex = std::nullopt,
    RenderFrameData::EditorDecorationHighlight highlight =
        RenderFrameData::EditorDecorationHighlight::None);

Vec3 decorationLightPosition(const Level::Decoration& decoration);

void appendDecorations(
    RenderFrameData& frame,
    const std::vector<Level::Decoration>& decorations,
    const AssetManifest& manifest,
    std::optional<std::size_t> selected = std::nullopt,
    std::optional<std::size_t> hovered = std::nullopt,
    bool editorDecorations = false,
    const std::function<bool(GridPosition3)>& visibleCell = {});

// ----------------------------------------------------------- Screen selector

void appendSelector(
    RenderFrameData& frame,
    const Level::ScreenSelector& selector,
    const AssetManifest& manifest,
    const std::function<ScreenSelectorViewState(LevelLocation)>& stateFor,
    bool preview = false,
    bool pickable = false);

} // namespace sokoban::renderFrameParts
