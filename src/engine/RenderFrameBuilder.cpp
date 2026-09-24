#include "engine/RenderFrameBuilder.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/RenderFrameParts.hpp"
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

// The gameplay frame.
//
// Everything both builders share - water, ladders, decorations, the selector,
// the camera extent and the small shaping helpers - lives in
// RenderFrameParts.hpp. What is left here is what only gameplay does: the
// static world, actors and their animations, and the mirror preview.

namespace sokoban {

using namespace renderFrameParts;

namespace {

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
    case AnimationUse::PlayerPull:
        return manifest.playerPullAnimation();
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
        .showGrid = !tileTypeIsPlayerStart(tile),
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
        .modelRotationQuarterTurns = tileTypeIsPlayerStart(tile)
            ? playerFacingQuarterTurns
            : (rules::conveyorDirectionForTile(tile)
                    ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
                    : (rules::turretDirectionForTile(tile)
                            ? facingQuarterTurns(
                                  *rules::turretDirectionForTile(tile))
                            : mirrorOrientationQuarterTurns(tile).value_or(0))),
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
                    .color = tileTypeIsPlayerStart(cell.tile)
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

// Extracts the yaw from a rotation that is known to be about z only, which
// is what actor facing is. Not valid for a general orientation.
float yawRadians(Quat orientation)
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

std::size_t primaryPlayerIndex(
    const RenderFrameBuilder::GameplayInput& input)
{
    if (input.activeHeroController != invalidEntityId) {
        for (std::size_t i = 0; i < input.state.players.size(); ++i) {
            if (rules::playerControllerId(input.state, i) ==
                input.activeHeroController) {
                return i;
            }
        }
    }
    return 0;
}

RenderFrameData initializeGameplayFrame(
    const RenderFrameBuilder::GameplayInput& input,
    FrameArena* arena = nullptr)
{
    const std::size_t primaryIndex = primaryPlayerIndex(input);
    const auto& primaryPlayerVisual =
        input.presentation.players().at(primaryIndex);
    RenderFrameData frame = arena != nullptr
        ? RenderFrameData(*arena)
        : RenderFrameData {};
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.cameraPitchDegrees = input.cameraPitchDegrees;
    frame.lighting = input.settings.renderLighting();
    frame.gridOverlay = input.settings.renderGridOverlay();
    frame.outputTransform = input.settings.renderOutputTransform();
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
    for (const Level::PlayerStart& player : input.level.playerStarts()) {
        includeCameraCell(authoredGameplayExtent, player.position);
    }
    for (const Level::MovableTile& movable : input.level.movableTiles()) {
        includeCameraCell(authoredGameplayExtent, movable.position);
    }
    for (GridPosition3 enemy : input.level.enemyStarts()) {
        includeCameraCell(authoredGameplayExtent, enemy);
    }
    frame.waterGridBounds = waterGridBoundsFor(authoredGameplayExtent);
    frame.cameraExtent = authoredGameplayExtent.value_or(
        RenderFrameData::CameraExtent {});
    if (input.cameraExtent) {
        frame.cameraExtent = input.cameraExtent;
    }
    frame.cameraExtentTransitionTarget =
        input.cameraExtentTransitionTarget;
    frame.cameraExtentTransitionProgress =
        input.cameraExtentTransitionProgress;
    frame.cameraOffset = input.cameraOffset;
    frame.groundSplat = groundSplatTextures(input.manifest, input.levelLocation);
    for (const RenderFrameBuilder::GameplayInput::GroundSplatRegion& source :
         input.groundSplatRegions) {
        if (frame.groundSplatRegionCount >=
            RenderFrameData::groundSplatRegionCapacity) {
            break;
        }
        frame.groundSplatRegions[frame.groundSplatRegionCount++] = {
            .origin = source.origin,
            .width = source.width,
            .height = source.height,
            .textures = groundSplatTexturesForOverworldScreen(
                [&input](std::string_view name) {
                    return input.manifest.findTextureIdByName(name);
                },
                source.screenId),
        };
    }
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
    const std::function<ScreenSelectorViewState(LevelLocation)>& stateFor,
    std::optional<uint32_t> previewId = std::nullopt,
    bool pickable = false,
    const std::function<bool(GridPosition3)>& visibleCell = {})
{
    for (const Level::ScreenSelector& selector : selectors) {
        if (visibleCell && !visibleCell(selector.cell)) {
            continue;
        }
        appendSelector(
            frame,
            selector,
            manifest,
            stateFor,
            previewId == selector.id,
            pickable);
    }
}

// The water surfaces and the shoreline masks around them: three passes over
// the level volume, plus the two lookups they share.
//
// Half of appendGameplayWorld() by line count, and the half that reads the
// level rather than the presentation - which is why it moves as a unit and
// takes only the frame, the input and the state.
void appendGameplayWaterAndShorelines(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input,
    const GameState& state)
{
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
                const GridPosition3 waterCell {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                if ((!input.visibleCell || input.visibleCell(waterCell)) &&
                    rules::isUnfilledWater(input.level, state, {
                        waterCell.x,
                        waterCell.y,
                        waterCell.z + 1,
                    })) {
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
    if (input.level.waterLayer() && !input.visibleCell) {
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
                const GridPosition3 cell {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                };
                if (input.visibleCell && !input.visibleCell(cell)) {
                    continue;
                }
                appendLadderRungsForCell(
                    frame,
                    cell,
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
            },
            [&, z](GridPosition position) {
                return !input.visibleCell || input.visibleCell({
                    position.x,
                    position.y,
                    static_cast<int>(z),
                });
            });
    }
}

void appendGameplayWorld(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& primaryPlayerVisual =
        input.presentation.players().at(primaryPlayerIndex(input));
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
            if (input.visibleCell && !input.visibleCell(position)) {
                return StaticRenderCell { .tile = TileType::Air };
            }
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
        frame,
        input.level.decorations(),
        input.manifest,
        std::nullopt,
        std::nullopt,
        false,
        input.visibleCell);
    appendSelectors(
        frame,
        input.level.selectors(),
        input.manifest,
        input.selectorState,
        std::nullopt,
        false,
        input.visibleCell);

    appendGameplayWaterAndShorelines(frame, input, state);
}

void appendBardAuraFace(
    RenderFrameData& frame,
    Vec2 minimum,
    Vec2 maximum,
    float elevation,
    Vec4 color)
{
    frame.isoFaces.push_back({
        .vertices = {
            Vec3 { minimum.x, minimum.y, elevation },
            Vec3 { maximum.x, minimum.y, elevation },
            Vec3 { maximum.x, maximum.y, elevation },
            Vec3 { minimum.x, maximum.y, elevation },
        },
        .normal = { 0.0f, 0.0f, 1.0f },
        .color = color,
        .translucent = true,
        .castsShadows = false,
    });
}

void appendBardWallRect(
    RenderFrameData& frame,
    Vec3 origin,
    Vec3 tangent,
    Vec3 normal,
    float alongMinimum,
    float alongMaximum,
    float heightMinimum,
    float heightMaximum,
    float outwardOffset,
    Vec4 color)
{
    const auto point = [&](float along, float height) {
        return origin + tangent * along + normal * outwardOffset +
            Vec3 { 0.0f, 0.0f, height };
    };
    frame.isoFaces.push_back({
        .vertices = {
            point(alongMinimum, heightMinimum),
            point(alongMaximum, heightMinimum),
            point(alongMaximum, heightMaximum),
            point(alongMinimum, heightMaximum),
        },
        .normal = normal,
        .color = color,
        .translucent = true,
        .castsShadows = false,
    });
}

void appendBardWallNote(
    RenderFrameData& frame,
    Vec3 origin,
    Vec3 tangent,
    Vec3 normal,
    float along,
    float height,
    Vec4 color)
{
    constexpr float headHalfWidth = 0.09f;
    constexpr float headHalfHeight = 0.065f;
    constexpr float stemHalfWidth = 0.022f;
    constexpr float stemHeight = 0.28f;
    constexpr float flagWidth = 0.14f;
    constexpr float flagHeight = 0.045f;
    constexpr float noteOffset = 0.012f;
    appendBardWallRect(
        frame,
        origin,
        tangent,
        normal,
        along - headHalfWidth,
        along + headHalfWidth,
        height - headHalfHeight,
        height + headHalfHeight,
        noteOffset,
        color);
    appendBardWallRect(
        frame,
        origin,
        tangent,
        normal,
        along + headHalfWidth - stemHalfWidth * 2.0f,
        along + headHalfWidth,
        height,
        height + stemHeight,
        noteOffset,
        color);
    appendBardWallRect(
        frame,
        origin,
        tangent,
        normal,
        along + headHalfWidth - stemHalfWidth * 2.0f,
        along + headHalfWidth + flagWidth,
        height + stemHeight - flagHeight,
        height + stemHeight,
        noteOffset,
        color);
}

void appendBardMusicSheetWall(
    RenderFrameData& frame,
    Vec3 origin,
    Vec3 tangent,
    Vec3 normal,
    float animationTimeSeconds,
    std::size_t wallIndex)
{
    const float pulse = 0.5f + 0.5f * std::sin(animationTimeSeconds * 2.4f);
    appendBardWallRect(
        frame,
        origin,
        tangent,
        normal,
        0.0f,
        5.0f,
        -1.0f,
        1.0f,
        0.0f,
        { 0.68f, 0.22f, 0.92f, 0.035f + pulse * 0.015f });

    constexpr std::array<float, 5> staffHeights {
        -0.60f,
        -0.30f,
        0.0f,
        0.30f,
        0.60f,
    };
    constexpr float staffHalfWidth = 0.012f;
    const Vec4 staffColor {
        0.96f, 0.60f, 1.0f, 0.38f + pulse * 0.12f,
    };
    for (const float height : staffHeights) {
        appendBardWallRect(
            frame,
            origin,
            tangent,
            normal,
            0.0f,
            5.0f,
            height - staffHalfWidth,
            height + staffHalfWidth,
            0.006f,
            staffColor);
    }

    constexpr std::array<float, 2> notePositions {
        1.25f,
        3.75f,
    };
    for (std::size_t noteIndex = 0;
         noteIndex < notePositions.size();
         ++noteIndex) {
        const float phase = static_cast<float>(wallIndex * 2 + noteIndex);
        const float bob = 0.055f * std::sin(
            animationTimeSeconds * 2.8f + phase * 0.9f);
        const float height = (noteIndex == 0 ? -0.30f : 0.30f) + bob;
        appendBardWallNote(
            frame,
            origin,
            tangent,
            normal,
            notePositions[noteIndex],
            height,
            { 1.0f, 0.72f, 1.0f, 0.72f });
    }
}

void appendBardAura(
    RenderFrameData& frame,
    Vec3 bardPosition,
    float animationTimeSeconds)
{
    const float left = bardPosition.x - 2.0f;
    const float top = bardPosition.y - 2.0f;
    const float right = bardPosition.x + 3.0f;
    const float bottom = bardPosition.y + 3.0f;
    const float elevation = bardPosition.z + 0.025f;
    const float pulse = 0.5f + 0.5f * std::sin(animationTimeSeconds * 2.4f);
    appendBardAuraFace(
        frame,
        { left, top },
        { right, bottom },
        elevation,
        { 0.68f, 0.22f, 0.92f, 0.075f + pulse * 0.025f });

    const std::array<Vec3, 4> origins {
        Vec3 { left, top, elevation },
        Vec3 { right, top, elevation },
        Vec3 { right, bottom, elevation },
        Vec3 { left, bottom, elevation },
    };
    const std::array<Vec3, 4> tangents {
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { -1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, -1.0f, 0.0f },
    };
    const std::array<Vec3, 4> normals {
        Vec3 { 0.0f, -1.0f, 0.0f },
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { -1.0f, 0.0f, 0.0f },
    };
    for (std::size_t wallIndex = 0; wallIndex < origins.size(); ++wallIndex) {
        appendBardMusicSheetWall(
            frame,
            origins[wallIndex],
            tangents[wallIndex],
            normals[wallIndex],
            animationTimeSeconds,
            wallIndex);
    }
}

void appendGameplayEntities(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& playerVisuals = input.presentation.players();
    const auto& movableVisuals = input.presentation.movables();
    const std::size_t primaryIndex = primaryPlayerIndex(input);
    const EntityId activeController =
        rules::playerControllerId(state, primaryIndex);

    for (std::size_t playerIndex = 0;
         playerIndex < state.players.size() &&
         playerIndex < playerVisuals.size();
         ++playerIndex) {
        if (input.visibleCell &&
            !input.visibleCell(state.players[playerIndex].cell)) {
            continue;
        }
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

        const bool primary = playerIndex == primaryIndex;
        const bool controlled = rules::playerControllerId(state, playerIndex) ==
            activeController;
        if (!state.players[playerIndex].dead &&
            state.players[playerIndex].character.value_or(
                input.level.character()) == CharacterType::Bard) {
            appendBardAura(
                frame,
                visual.motion.renderPosition,
                input.presentation.worldAnimationTimeSeconds());
        }
        RenderFrameData::Tile playerTile {
            .cell = state.players[playerIndex].cell,
            .position = {
                visual.motion.renderPosition.x,
                visual.motion.renderPosition.y,
            },
            .color = controlled
                ? Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }
                : Vec4 { 0.72f, 0.72f, 0.72f, 1.0f },
            .baseElevation = visual.motion.renderPosition.z,
            .height = 1.0f,
            .showGrid = false,
            .affectsCameraFit = false,
            .isPrimaryPlayer = primary,
            .model = input.manifest.characterModel(
                state.players[playerIndex].character.value_or(
                    input.level.character())),
            .animation = animation,
            .animationFallback = fallback,
            .animationInstanceId = actorAnimationInstance(visual.motion.target),
            .renderableId = visual.motion.target.id,
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
        if (enemy.dead || (enemy.fallen && !visual.motion.moving)) {
            continue;
        }
        if (input.visibleCell && !input.visibleCell(enemy.cell)) {
            continue;
        }
        RenderFrameData::Tile enemyTile {
            .cell = enemy.cell,
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
            .renderableId = visual.motion.target.id,
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
        const bool reviving =
            input.moving && movable.dead &&
            movableIndex < input.projectedState.movables.size() &&
            !input.projectedState.movables[movableIndex].dead;
        if (movable.dead && !reviving) {
            continue;
        }
        if (movable.fallen && !visual.moving && !movingOutOfWater) {
            continue;
        }
        const bool projectedVisible =
            movableIndex < input.projectedState.movables.size() &&
            (!input.visibleCell || input.visibleCell(
                input.projectedState.movables[movableIndex].cell));
        if (input.visibleCell && !input.visibleCell(movable.cell) &&
            !projectedVisible) {
            continue;
        }

        Vec4 color = tileTypeIsTurret(movable.type)
            ? Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }
            : tileColor(movable.type);
        if (movable.type == TileType::Ice) {
            color.w = config::iceTintAlpha;
        }
        const Vec2 turretRecoil = tileTypeIsTurret(movable.type)
            ? input.presentation.turretRecoilOffset(visual.target.id)
            : Vec2 {};
        RenderFrameData::Tile movableTile {
            .cell = movable.cell,
            .position = {
                visual.renderPosition.x + turretRecoil.x,
                visual.renderPosition.y + turretRecoil.y,
            },
            .color = color,
            .baseElevation = visual.renderPosition.z,
            .height = 1.0f,
            .blurBehind = movable.type == TileType::Ice,
            .affectsCameraFit = false,
            .model = input.manifest.modelForTile(movable.type),
            .renderableId = visual.target.id,
            .modelRotationQuarterTurns =
                rules::turretDirectionForTile(movable.type)
                ? facingQuarterTurns(
                      *rules::turretDirectionForTile(movable.type))
                : 0U,
        };
        applyTileScale(
            movableTile,
            input.settings.tileScale(movable.type));
        frame.tiles.push_back(movableTile);
    }
}

// Shared state for building one previewed mirror entity's beam segments and
// destination ghost.
struct MirrorEntityPreviewContext {
    const rules::MirrorEntityPreview& entity;
    const rules::MirrorEntityPreview* matchingEndEntity = nullptr;
    const GameplayPresentation::PlayerVisual* previewPlayer = nullptr;
    const GameplayPresentation::EnemyVisual* previewEnemy = nullptr;
    const GameplayPresentation::EntityVisual* visual = nullptr;
    float progress = 0.0f;
    bool animatePreview = false;
    float previewOpacity = 1.0f;
};

// The beam segments this entity contributes, in the order the renderer wants
// them.
void appendMirrorEntitySegments(
    const MirrorEntityPreviewContext& preview,
    std::vector<MirrorRenderSegment>& entitySegments,
    std::vector<MirrorRenderSegment>& beamSegments)
{
    const rules::MirrorEntityPreview& entity = preview.entity;
    const rules::MirrorEntityPreview* matchingEndEntity =
        preview.matchingEndEntity;
    const GameplayPresentation::EntityVisual* visual = preview.visual;
    const float progress = preview.progress;
    const bool animatePreview = preview.animatePreview;
    const float previewOpacity = preview.previewOpacity;
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
}

// The translucent ghost tile showing where the entity ends up.
void appendMirrorGhostTile(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input,
    const GameState& state,
    const MirrorEntityPreviewContext& preview)
{
    const rules::MirrorEntityPreview& entity = preview.entity;
    const rules::MirrorEntityPreview* matchingEndEntity =
        preview.matchingEndEntity;
    const GameplayPresentation::PlayerVisual* previewPlayer =
        preview.previewPlayer;
    const GameplayPresentation::EnemyVisual* previewEnemy =
        preview.previewEnemy;
    const float progress = preview.progress;
    const bool animatePreview = preview.animatePreview;
    const float previewOpacity = preview.previewOpacity;
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
            ? input.manifest.characterModel(
                  state.players[entity.playerIndex].character.value_or(
                      input.level.character()))
            : (entity.enemy
                    ? input.manifest.enemyModel()
                    : input.manifest.modelForTile(
                          state.movables[entity.movableIndex].type)),
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
            : (entity.enemy
                    ? animationFor(
                          input.animations,
                          AnimationUse::EnemyIdle,
                          input.manifest.playerIdleAnimation())
                    : noAnimation),
        .animationInstanceId = entity.player
            ? mirrorGhostAnimationInstance(
                  entity.resultPlayerIndex)
            : (entity.enemy
                    ? mirrorGhostAnimationInstance(
                          state.players.size() + entity.enemyIndex)
                    : uint64_t { 0 }),
        .animationLoops = true,
        .animationTimeSeconds = previewPlayer
            ? animationTimeFor(
                  input.animations,
                  ghostFallen
                      ? AnimationUse::MirrorPreviewPlayerDeadIdle
                      : AnimationUse::MirrorPreviewPlayerIdle,
                  previewPlayer->clipTimeSeconds)
            : (previewEnemy
                    ? animationTimeFor(
                          input.animations,
                          AnimationUse::EnemyIdle,
                          previewEnemy->clipTimeSeconds)
                    : 0.0f),
        .modelRotationQuarterTurns = entity.player
            ? (previewPlayer
                    ? previewPlayer->facingQuarterTurns
                    : 0U)
            : 0U,
        .modelRotationOffsetRadians = previewEnemy
            ? yawRadians(previewEnemy->orientation)
            : 0.0f,
        .effect = RenderSurfaceEffect::MirrorEnergy,
    };
    applyTileScale(
        ghost,
        input.settings.tileScale(
            entity.player
                ? TileType::Player
                : (entity.enemy
                        ? TileType::Enemy
                        : state.movables[entity.movableIndex].type)));
    frame.tiles.push_back(ghost);
}

void appendMirrorPreview(
    RenderFrameData& frame,
    const RenderFrameBuilder::GameplayInput& input)
{
    const GameState& state = input.state;
    const auto& playerVisuals = input.presentation.players();
    const auto& movableVisuals = input.presentation.movables();
    const auto& enemyVisuals = input.presentation.enemies();

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
                                candidate.enemy == entity.enemy &&
                                (entity.player
                                    ? candidate.playerIndex ==
                                            entity.playerIndex &&
                                        candidate.reflectionIndex ==
                                            entity.reflectionIndex
                                    : (entity.enemy
                                            ? candidate.enemyIndex ==
                                                  entity.enemyIndex
                                            : candidate.movableIndex ==
                                                  entity.movableIndex));
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
                const GameplayPresentation::EnemyVisual* previewEnemy =
                    entity.enemy && entity.enemyIndex < enemyVisuals.size()
                    ? &enemyVisuals[entity.enemyIndex]
                    : nullptr;
                const GameplayPresentation::EntityVisual* visual =
                    entity.player
                    ? (previewPlayer ? &previewPlayer->motion : nullptr)
                    : (entity.enemy
                            ? (previewEnemy ? &previewEnemy->motion : nullptr)
                            : (entity.movableIndex < movableVisuals.size()
                                    ? &movableVisuals[entity.movableIndex]
                                    : nullptr));
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
                const MirrorEntityPreviewContext preview {
                    entity,
                    matchingEndEntity,
                    previewPlayer,
                    previewEnemy,
                    visual,
                    progress,
                    animatePreview,
                    previewOpacity,
                };
                appendMirrorEntitySegments(
                    preview, entitySegments, beamSegments);
                appendMirrorGhostTile(frame, input, state, preview);
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

} // namespace sokoban
