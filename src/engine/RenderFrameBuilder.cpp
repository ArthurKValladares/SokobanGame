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

        RenderFrameData::Tile playerTile {
            .cell = state.players[playerIndex].cell,
            .position = {
                visual.motion.renderPosition.x,
                visual.motion.renderPosition.y,
            },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
            .baseElevation = visual.motion.renderPosition.z,
            .height = 1.0f,
            .showGrid = false,
            .affectsCameraFit = false,
            .isPrimaryPlayer = playerIndex == 0,
            .model = input.manifest.playerModel(),
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
        if (enemy.fallen && !visual.motion.moving) {
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

        Vec4 color = tileColor(movable.type);
        if (movable.type == TileType::Ice) {
            color.w = config::iceTintAlpha;
        }
        RenderFrameData::Tile movableTile {
            .cell = movable.cell,
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
            .renderableId = visual.target.id,
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

} // namespace sokoban
