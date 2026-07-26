// Headless tests for mutable presentation settings, entity interpolation, and
// render-frame construction.

#include "engine/AssetManifest.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/Rules.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/MirrorConfig.hpp"

#include <cmath>
#include <iostream>
#include <ranges>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

const AssetManifest& testManifest()
{
    static const AssetManifest manifest = AssetManifest::parse(R"json({
      "format": 1,
      "textures": [
        { "name": "Tex", "path": "t.png" }
      ],
      "models": [
        { "name": "Stone", "path": "stone.gltf" },
        { "name": "Glass", "path": "glass.gltf" },
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Conveyor", "path": "conveyor.gltf", "beltScroll": true },
        { "name": "Mirror", "path": "mirror.gltf" },
        {
          "name": "Hero",
          "path": "hero.glb",
          "geometry": "skinned",
          "material": { "mode": "texture", "texture": "Tex" },
          "role": "player"
        }
      ],
      "animations": [
        { "name": "Idle", "path": "a.glb", "role": "player-idle" },
        { "name": "Move", "path": "a.glb", "role": "player-move" },
        { "name": "Push", "path": "a.glb", "role": "player-push" },
        { "name": "Death", "path": "a.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" }
      ],
      "tiles": [
        { "tile": "Wall", "model": "Bricks" },
        { "tile": "Rock", "model": "Stone" },
        { "tile": "Ice", "model": "Glass" },
        { "tile": "Conveyor Up", "model": "Conveyor" },
        { "tile": "Conveyor Down", "model": "Conveyor" },
        { "tile": "Conveyor Right", "model": "Conveyor" },
        { "tile": "Conveyor Left", "model": "Conveyor" },
        { "tile": "Mirror North-West", "model": "Mirror" },
        { "tile": "Mirror North-East", "model": "Mirror" },
        { "tile": "Mirror South-West", "model": "Mirror" },
        { "tile": "Mirror South-East", "model": "Mirror" },
        { "tile": "Player", "model": "Hero" }
      ]
    })json");
    return manifest;
}


bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

GameState stateWithPlayer(GridPosition3 player)
{
    GameState state;
    state.player = player;
    return state;
}

void testCameraPitchTransition()
{
    TEST("cameraPitchTransition");
    GameplayPresentation presentation;
    CHECK(near(
        presentation.cameraPitchDegrees(),
        config::cameraPitchDegrees));

    presentation.updateCameraPitch(
        0.0f,
        config::cameraPitchTransitionSeconds * 0.5f,
        config::cameraPitchTransitionSeconds);
    CHECK(near(
        presentation.cameraPitchDegrees(),
        config::cameraPitchDegrees * 0.5f));
    presentation.updateCameraPitch(
        0.0f,
        config::cameraPitchTransitionSeconds * 0.5f,
        config::cameraPitchTransitionSeconds);
    CHECK(near(presentation.cameraPitchDegrees(), 0.0f));

    presentation.updateCameraPitch(
        config::cameraPitchDegrees,
        config::cameraPitchTransitionSeconds * 0.5f,
        config::cameraPitchTransitionSeconds);
    CHECK(near(
        presentation.cameraPitchDegrees(),
        config::cameraPitchDegrees * 0.5f));
    presentation.updateCameraPitch(
        config::cameraPitchDegrees,
        config::cameraPitchTransitionSeconds * 0.5f,
        config::cameraPitchTransitionSeconds);
    CHECK(near(
        presentation.cameraPitchDegrees(),
        config::cameraPitchDegrees));

    presentation.updateCameraPitch(0.0f, 0.0f, 0.0f);
    CHECK(near(presentation.cameraPitchDegrees(), 0.0f));
}

void testSettingsNormalizeAndConvert()
{
    TEST("settingsNormalizeAndConvert");
    PresentationSettings settings;
    settings.lighting.sunAzimuthDegrees = 999.0f;
    settings.lighting.sunTiltDegrees = -999.0f;
    settings.lighting.sunIntensity = -2.0f;
    settings.lighting.ambientIntensity = 9.0f;
    settings.lighting.specularStrength = 2.0f;
    settings.lighting.specularPower = 0.0f;
    settings.lighting.modelShadowReceive = -1.0f;
    settings.lighting.ambientOcclusionStrength = 4.0f;
    settings.lighting.shadowOpacity = 2.0f;
    settings.lighting.shadowBias = -1.0f;
    settings.grid.color.w = 4.0f;
    settings.grid.lineWidth = -2.0f;
    settings.geometry.surfaceEntityHeight = 8.0f;
    settings.geometry.surfaceEntityWidthDepth = 0.0f;
    settings.setTileScale(TileType::Wall, 99.0f);
    settings.normalize();

    CHECK(near(settings.lighting.sunAzimuthDegrees, 180.0f));
    CHECK(near(settings.lighting.sunTiltDegrees, -90.0f));
    CHECK(near(settings.lighting.sunIntensity, 0.0f));
    CHECK(near(settings.lighting.ambientIntensity, 2.0f));
    CHECK(near(settings.lighting.specularStrength, 1.0f));
    CHECK(near(settings.lighting.specularPower, 1.0f));
    CHECK(near(settings.lighting.modelShadowReceive, 0.0f));
    CHECK(near(settings.lighting.ambientOcclusionStrength, 1.0f));
    CHECK(near(settings.lighting.shadowOpacity, 0.85f));
    CHECK(near(settings.lighting.shadowBias, 0.0f));
    CHECK(near(settings.grid.color.w, 1.0f));
    CHECK(near(settings.grid.lineWidth, 0.0f));
    CHECK(near(settings.geometry.surfaceEntityHeight, 0.5f));
    CHECK(near(settings.geometry.surfaceEntityWidthDepth, 0.1f));
    CHECK(near(settings.tileScale(TileType::Wall), config::maxTileScale));

    settings.lighting.sunAzimuthDegrees = 0.0f;
    settings.lighting.sunTiltDegrees = 90.0f;
    settings.lighting.ambientOcclusionVisualize = true;
    const Vec3 direction = settings.sunDirection();
    CHECK(near(direction.x, 1.0f));
    CHECK(near(direction.y, 0.0f));
    CHECK(near(direction.z, 0.0f));

    const RenderFrameData::Lighting lighting = settings.renderLighting();
    CHECK(lighting.ambientOcclusion.visualize);
    CHECK(near(lighting.shadows.opacity, 0.85f));
    CHECK(near(settings.renderGridOverlay().width, 0.0f));
}

void testPresentationResetClocksAndFallenTargets()
{
    TEST("presentationResetClocksAndFallenTargets");
    GameState state = stateWithPlayer({ 1, 2, 3 });
    state.playerDead = true;
    state.movables.push_back({
        .type = TileType::Rock,
        .cell = { 4, 5, 2 },
        .fallen = true,
    });

    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(state);
    CHECK(near(presentation.player().motion.renderPosition.x, 1.0f));
    CHECK(near(presentation.player().motion.renderPosition.y, 2.0f));
    CHECK(near(
        presentation.player().motion.renderPosition.z,
        3.0f - config::drownedPlayerDepthBelowGround));
    CHECK(!presentation.player().deathTransitionPlaying);
    CHECK(presentation.movables().size() == 1);
    CHECK(near(
        presentation.movables()[0].renderPosition.z,
        2.0f - config::waterDepthBelowGround));

    presentation.advanceClocks(0.5f, false);
    CHECK(near(presentation.worldAnimationTimeSeconds(), 0.5f));
    CHECK(near(presentation.player().clipTimeSeconds, 0.5f));
    presentation.advanceClocks(0.25f, true);
    CHECK(near(presentation.worldAnimationTimeSeconds(), 0.25f));
    CHECK(near(presentation.player().clipTimeSeconds, 0.75f));
    CHECK(near(presentation.conveyorBeltScrollOffset(0.25f), 0.0f));
    CHECK(near(presentation.conveyorBeltScrollOffset(0.0f), 0.0f));
}

void testPresentationInterpolatesActionsAndClips()
{
    TEST("presentationInterpolatesActionsAndClips");
    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    GameState before = stateWithPlayer({ 0, 0, 0 });
    before.movables.push_back({
        .type = TileType::Rock,
        .cell = { 1, 0, 0 },
    });
    presentation.resetEntities(before);

    GameState after = before;
    after.player = { 1, 1, 1 };
    after.movables[0].cell = { 2, 0, 0 };
    GameplaySession::Action action {
        .before = before,
        .after = after,
        .durationSeconds = 3.0f,
        .playerPushing = true,
        .reversed = true,
        .facingDirection = MoveDirection::Left,
    };
    presentation.beginAction(action);

    CHECK(presentation.player().motion.moving);
    CHECK(presentation.player().movingClip == testManifest().playerPushAnimation());
    CHECK(near(presentation.player().clipPlaybackRate, -1.0f));
    CHECK(presentation.player().facingQuarterTurns == 1);
    CHECK(presentation.movables()[0].moving);

    presentation.advanceAnimations(0.5f);
    CHECK(near(presentation.player().motion.renderPosition.x, 0.0f));
    CHECK(near(presentation.player().motion.renderPosition.y, 0.0f));
    CHECK(near(presentation.player().motion.renderPosition.z, 0.5f));
    CHECK(near(presentation.movables()[0].renderPosition.x, 1.0f + 1.0f / 6.0f));

    presentation.advanceAnimations(1.0f);
    CHECK(near(presentation.player().motion.renderPosition.z, 1.0f));
    CHECK(near(presentation.player().motion.renderPosition.x, 0.5f));
    CHECK(near(presentation.player().motion.renderPosition.y, 0.0f));

    presentation.advanceAnimations(1.5f);
    CHECK(!presentation.player().motion.moving);
    CHECK(near(presentation.player().motion.renderPosition.x, 1.0f));
    CHECK(near(presentation.player().motion.renderPosition.y, 1.0f));
    CHECK(near(presentation.player().motion.renderPosition.z, 1.0f));

    presentation.finishAction(after);
    CHECK(near(presentation.player().clipPlaybackRate, 1.0f));
}

void testGameplayFrameUsesSettingsAndPresentation()
{
    TEST("gameplayFrameUsesSettingsAndPresentation");
    const Level level = Level::loadFromLayers({
        { "..." },
        { "C>R" },
    }, "presentation frame");

    GameState state;
    state.player = level.playerStart();
    for (const Level::MovableTile& movable : level.movableTiles()) {
        state.movables.push_back({
            .type = movable.type,
            .cell = movable.position,
        });
    }
    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(state);

    PresentationSettings settings;
    settings.lighting.sunColor = { 0.1f, 0.2f, 0.3f };
    settings.grid.lineWidth = 4.0f;
    settings.setTileScale(TileType::Player, 2.0f);
    GameplaySession::Action action;
    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .activeAction = action,
        .presentation = presentation,
        .settings = settings,
        .conveyorBeltScrollOffset = 0.75f,
    });

    CHECK(frame.viewMode == RenderViewMode::Isometric3D);
    const RenderFrameData overheadFrame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .activeAction = action,
        .presentation = presentation,
        .settings = settings,
        .cameraPitchDegrees = 0.0f,
    });
    CHECK(overheadFrame.viewMode == RenderViewMode::Isometric3D);
    CHECK(overheadFrame.cameraPitchDegrees == 0.0f);
    CHECK(frame.levelWidth == 3);
    CHECK(frame.levelHeight == 1);
    CHECK(frame.levelDepth == 2);
    CHECK(near(frame.lighting.sun.color.x, 0.1f));
    CHECK(near(frame.gridOverlay.width, 4.0f));

    const RenderFrameData::Tile* player = nullptr;
    const RenderFrameData::Tile* conveyor = nullptr;
    const RenderFrameData::Tile* rock = nullptr;
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.model == testManifest().playerModel()) {
            player = &tile;
        } else if (tile.model == testManifest().modelIdByName("Conveyor")) {
            conveyor = &tile;
        } else if (tile.model == testManifest().modelIdByName("Stone")) {
            rock = &tile;
        }
    }
    CHECK(player != nullptr);
    CHECK(conveyor != nullptr);
    CHECK(rock != nullptr);
    CHECK(near(player->size.x, 2.0f));
    CHECK(near(player->position.x, -0.5f));
    CHECK(player->animation == testManifest().playerIdleAnimation());
    CHECK(near(conveyor->beltScrollOffset, 0.75f));
}

void testMirrorTilesUseTheirModelAndOrientation()
{
    TEST("mirrorTilesUseTheirModelAndOrientation");
    const Level level = Level::loadFromLayers({
        { "....." },
        { "C1234" },
    }, "mirror presentation frame");
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);
    const PresentationSettings settings;

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .activeAction = {},
        .presentation = presentation,
        .settings = settings,
    });

    std::array<const RenderFrameData::Tile*, 4> mirrors {};
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.model == testManifest().modelIdByName("Mirror") &&
            tile.cell.x >= 1 && tile.cell.x <= 4) {
            mirrors[static_cast<std::size_t>(tile.cell.x - 1)] = &tile;
        }
    }
    constexpr std::array<uint32_t, 4> expectedRotations { 0, 1, 3, 2 };
    for (std::size_t i = 0; i < mirrors.size(); ++i) {
        CHECK(mirrors[i] != nullptr);
        if (mirrors[i]) {
            CHECK(near(mirrors[i]->height, 1.0f));
            CHECK(mirrors[i]->modelRotationQuarterTurns == expectedRotations[i]);
            CHECK(near(
                mirrors[i]->modelRotationOffsetRadians,
                config::mirrorModelRotationOffsetRadians));
        }
    }
}

void testMirrorActivationBuildsBeamAndDestinationGhost()
{
    TEST("mirrorActivationBuildsBeamAndDestinationGhost");
    const Level level = Level::loadFromLayers({
        {
            ".....",
            ".....",
            ".....",
            ".....",
            ".....",
        },
        {
            "     ",
            "     ",
            "  3  ",
            "     ",
            "  C  ",
        },
    }, "mirror visualization");
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(state);
    presentation.advanceClocks(0.75f, false);

    auto buildFrame = [&](bool moving) {
        return RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = moving,
            .activeAction = {},
            .presentation = presentation,
            .settings = {},
        });
    };
    const RenderFrameData frame = buildFrame(false);
    CHECK(near(frame.effectAnimationTimeSeconds, 0.75f));
    auto sameColor = [](Vec4 left, Vec4 right) {
        return near(left.x, right.x) &&
            near(left.y, right.y) &&
            near(left.z, right.z) &&
            near(left.w, right.w);
    };

    const auto ghost = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.effect == RenderSurfaceEffect::MirrorEnergy;
        });
    CHECK(ghost != frame.tiles.end());
    if (ghost != frame.tiles.end()) {
        CHECK(ghost->cell == (GridPosition3 { 0, 2, 1 }));
        CHECK(near(ghost->position.x, 0.0f));
        CHECK(near(ghost->position.y, 2.0f));
        CHECK(ghost->model == testManifest().playerModel());
        CHECK(ghost->animation == testManifest().playerIdleAnimation());
        CHECK(sameColor(ghost->color, config::mirrorGhostColor));
    }

    const std::size_t energyFaceCount = std::ranges::count_if(
        frame.isoFaces,
        [&](const RenderFrameData::IsoFace& face) {
            return face.effect == RenderSurfaceEffect::MirrorEnergy;
        });
    // Two reflection legs, each represented by a halo and core five-face prism.
    CHECK(energyFaceCount == 20);
    CHECK(std::ranges::any_of(
        frame.isoFaces,
        [&](const RenderFrameData::IsoFace& face) {
            return face.effect == RenderSurfaceEffect::MirrorEnergy &&
                sameColor(face.color, config::mirrorBeamHaloColor);
        }));
    CHECK(std::ranges::any_of(
        frame.isoFaces,
        [&](const RenderFrameData::IsoFace& face) {
            return face.effect == RenderSurfaceEffect::MirrorEnergy &&
                sameColor(face.color, config::mirrorBeamCoreColor);
        }));

    GameState moved = state;
    moved.player = { 2, 3, 1 };
    const GameplaySession::Action moveAction {
        .before = state,
        .after = moved,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Up,
    };
    presentation.beginAction(moveAction);
    presentation.advanceAnimations(0.5f);
    const RenderFrameData movingFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = true,
            .activeAction = moveAction,
            .presentation = presentation,
            .settings = {},
        });
    const auto movingGhost = std::ranges::find_if(
        movingFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.effect == RenderSurfaceEffect::MirrorEnergy;
        });
    CHECK(movingGhost != movingFrame.tiles.end());
    if (movingGhost != movingFrame.tiles.end()) {
        CHECK(near(movingGhost->position.x, 0.5f));
        CHECK(near(movingGhost->position.y, 2.0f));
    }
    CHECK(std::ranges::any_of(
        movingFrame.isoFaces,
        [](const RenderFrameData::IsoFace& face) {
            return face.effect == RenderSurfaceEffect::MirrorEnergy;
        }));

    presentation.resetEntities(state);
    GameState movedSideways = state;
    movedSideways.player = { 3, 4, 1 };
    const GameplaySession::Action sidewaysAction {
        .before = state,
        .after = movedSideways,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Right,
    };
    presentation.beginAction(sidewaysAction);
    presentation.advanceAnimations(0.1f);
    const RenderFrameData sidewaysFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = true,
            .activeAction = sidewaysAction,
            .presentation = presentation,
            .settings = {},
        });
    const auto sidewaysGhost = std::ranges::find_if(
        sidewaysFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.effect == RenderSurfaceEffect::MirrorEnergy;
        });
    CHECK(sidewaysGhost != sidewaysFrame.tiles.end());
    if (sidewaysGhost != sidewaysFrame.tiles.end()) {
        CHECK(near(sidewaysGhost->position.x, 0.0f));
        CHECK(near(sidewaysGhost->position.y, 2.0f));
        CHECK(sidewaysGhost->color.w > 0.0f);
        CHECK(sidewaysGhost->color.w < config::mirrorGhostColor.w);
    }

    const auto verticalBeam = std::ranges::find_if(
        sidewaysFrame.isoFaces,
        [&](const RenderFrameData::IsoFace& face) {
            if (face.effect != RenderSurfaceEffect::MirrorEnergy ||
                !near(face.color.x, config::mirrorBeamCoreColor.x) ||
                !near(face.color.y, config::mirrorBeamCoreColor.y) ||
                !near(face.color.z, config::mirrorBeamCoreColor.z) ||
                face.color.w <= 0.0f ||
                face.color.w >= config::mirrorBeamCoreColor.w) {
                return false;
            }
            float minX = face.vertices[0].x;
            float maxX = face.vertices[0].x;
            float minY = face.vertices[0].y;
            float maxY = face.vertices[0].y;
            for (Vec3 vertex : face.vertices) {
                minX = std::min(minX, vertex.x);
                maxX = std::max(maxX, vertex.x);
                minY = std::min(minY, vertex.y);
                maxY = std::max(maxY, vertex.y);
            }
            return maxY - minY > 1.0f && maxX - minX < 0.2f;
        });
    CHECK(verticalBeam != sidewaysFrame.isoFaces.end());
    if (verticalBeam != sidewaysFrame.isoFaces.end()) {
        float centerX = 0.0f;
        for (Vec3 vertex : verticalBeam->vertices) {
            centerX += vertex.x;
        }
        CHECK(near(centerX * 0.25f, 2.5f));
    }

    presentation.advanceAnimations(0.4f);
    const RenderFrameData fadedOutFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = true,
            .activeAction = sidewaysAction,
            .presentation = presentation,
            .settings = {},
        });
    CHECK(std::ranges::none_of(
        fadedOutFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.effect == RenderSurfaceEffect::MirrorEnergy;
        }));
    CHECK(std::ranges::none_of(
        fadedOutFrame.isoFaces,
        [](const RenderFrameData::IsoFace& face) {
            return face.effect == RenderSurfaceEffect::MirrorEnergy;
        }));
}

void testGameplayFrameBuildsProceduralWaterSurface()
{
    TEST("gameplayFrameBuildsProceduralWaterSurface");
    const Level level = Level::loadFromLayers({
        { ".W#" },
        { "C  " },
    }, "procedural water frame");
    GameState state = stateWithPlayer(level.playerStart());

    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(state);
    presentation.advanceClocks(0.75f, false);

    const PresentationSettings settings;
    const GameplaySession::Action action;
    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .activeAction = action,
        .presentation = presentation,
        .settings = settings,
    });

    CHECK(frame.waterSurfaces.size() == 1);
    if (!frame.waterSurfaces.empty()) {
        const RenderFrameData::WaterSurface& water =
            frame.waterSurfaces.front();
        CHECK((water.cell == GridPosition3 { 1, 0, 0 }));
        CHECK(near(water.position.x, 1.0f));
        CHECK(near(water.position.y, 0.0f));
        CHECK(near(
            water.elevation,
            1.0f - config::waterDepthBelowGround));
        CHECK(near(water.color.w, config::waterSurfaceColor.w));
        const uint32_t expectedShorelineMask =
            waterShorelineBit(WaterShorelineEdge::NegativeX) |
            waterShorelineBit(WaterShorelineEdge::PositiveX);
        CHECK(water.shorelineMask == expectedShorelineMask);
    }
    CHECK(near(frame.waterAnimationTimeSeconds, 0.75f));
    CHECK(std::ranges::none_of(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { 1, 0, 0 };
        }));

    state.movables.push_back({
        .type = TileType::Rock,
        .cell = { 1, 0, 1 },
        .fallen = true,
    });
    const RenderFrameData filledFrame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .activeAction = action,
        .presentation = presentation,
        .settings = settings,
    });
    CHECK(filledFrame.waterSurfaces.empty());
}

void testWaterLayerBuildsUnboundedNonPickableExterior()
{
    TEST("waterLayerBuildsUnboundedNonPickableExterior");
    const Level level = Level::loadFromLayers(
        {
            { " . " },
            { "C  " },
        },
        "unbounded water frame",
        0U);
    const GameState state = stateWithPlayer(level.playerStart());
    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(state);

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .activeAction = {},
        .presentation = presentation,
        .settings = {},
    });

    CHECK(frame.levelWidth == 3);
    CHECK(frame.levelHeight == 1);
    CHECK(frame.levelDepth == 2);
    CHECK(frame.waterSurfaces.size() == 18);
    CHECK(std::ranges::count_if(
              frame.waterSurfaces,
              [](const RenderFrameData::WaterSurface& water) {
                  return water.pickable;
              }) == 2);
    CHECK(std::ranges::count_if(
              frame.waterSurfaces,
              [](const RenderFrameData::WaterSurface& water) {
                  return water.size.x > 1.0f ||
                      water.size.y > 1.0f;
              }) == 4);

    const auto exteriorAboveGround = std::ranges::find_if(
        frame.waterSurfaces,
        [](const RenderFrameData::WaterSurface& water) {
            return water.cell == GridPosition3 { 1, -1, 0 };
        });
    CHECK(exteriorAboveGround != frame.waterSurfaces.end());
    if (exteriorAboveGround != frame.waterSurfaces.end()) {
        CHECK(!exteriorAboveGround->pickable);
        CHECK(
            (exteriorAboveGround->shorelineMask &
             waterShorelineBit(WaterShorelineEdge::PositiveY)) != 0);
    }

    const Level allWaterLevel = Level::loadFromLayers(
        {
            { "  " },
            { "C " },
        },
        "unbounded water without banks",
        0U);
    const GameState allWaterState =
        stateWithPlayer(allWaterLevel.playerStart());
    GameplayPresentation allWaterPresentation;
    allWaterPresentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    allWaterPresentation.resetEntities(allWaterState);
    const RenderFrameData allWaterFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = allWaterLevel,
            .state = allWaterState,
            .moving = false,
            .activeAction = {},
            .presentation = allWaterPresentation,
            .settings = {},
        });
    CHECK(allWaterFrame.isoFaces.empty());
}

void testFilledWaterUpdatesEdgesAndRoundedCornerCaps()
{
    TEST("filledWaterUpdatesEdgesAndRoundedCornerCaps");
    const Level level = Level::loadFromLayers({
        {
            "WWWW..",
            "WWWW..",
            "WWWW..",
        },
        {
            "     C",
            "      ",
            "      ",
        },
    }, "dynamic water shoreline");
    GameState state = stateWithPlayer(level.playerStart());

    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(state);
    const PresentationSettings settings;

    auto buildFrame = [&] {
        return RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = false,
            .activeAction = {},
            .presentation = presentation,
            .settings = settings,
        });
    };
    const RenderFrameData openFrame = buildFrame();
    CHECK(openFrame.waterSurfaces.size() == 12);

    state.movables.push_back({
        .type = TileType::Rock,
        .cell = { 1, 1, 1 },
        .fallen = true,
    });
    presentation.resetEntities(state);
    const RenderFrameData filledFrame = buildFrame();
    CHECK(filledFrame.waterSurfaces.size() == 11);

    auto waterAt = [&](int x, int y) {
        return std::ranges::find_if(
            filledFrame.waterSurfaces,
            [&](const RenderFrameData::WaterSurface& water) {
                return water.cell == GridPosition3 { x, y, 0 };
            });
    };
    auto hasEdge = [](const RenderFrameData::WaterSurface& water,
                       WaterShorelineEdge edge) {
        return (water.shorelineMask & waterShorelineBit(edge)) != 0;
    };
    auto hasCorner = [](const RenderFrameData::WaterSurface& water,
                         WaterShorelineCorner corner) {
        return (water.shorelineMask & waterShorelineBit(corner)) != 0;
    };

    const auto north = waterAt(1, 0);
    const auto east = waterAt(2, 1);
    const auto south = waterAt(1, 2);
    const auto west = waterAt(0, 1);
    CHECK(north != filledFrame.waterSurfaces.end());
    CHECK(east != filledFrame.waterSurfaces.end());
    CHECK(south != filledFrame.waterSurfaces.end());
    CHECK(west != filledFrame.waterSurfaces.end());
    if (north != filledFrame.waterSurfaces.end()) {
        CHECK(hasEdge(*north, WaterShorelineEdge::PositiveY));
    }
    if (east != filledFrame.waterSurfaces.end()) {
        CHECK(hasEdge(*east, WaterShorelineEdge::NegativeX));
    }
    if (south != filledFrame.waterSurfaces.end()) {
        CHECK(hasEdge(*south, WaterShorelineEdge::NegativeY));
    }
    if (west != filledFrame.waterSurfaces.end()) {
        CHECK(hasEdge(*west, WaterShorelineEdge::PositiveX));
    }

    const auto northWest = waterAt(0, 0);
    const auto northEast = waterAt(2, 0);
    const auto southEast = waterAt(2, 2);
    const auto southWest = waterAt(0, 2);
    CHECK(northWest != filledFrame.waterSurfaces.end());
    CHECK(northEast != filledFrame.waterSurfaces.end());
    CHECK(southEast != filledFrame.waterSurfaces.end());
    CHECK(southWest != filledFrame.waterSurfaces.end());
    if (northWest != filledFrame.waterSurfaces.end()) {
        CHECK(hasCorner(
            *northWest,
            WaterShorelineCorner::PositiveXPositiveY));
    }
    if (northEast != filledFrame.waterSurfaces.end()) {
        CHECK(hasCorner(
            *northEast,
            WaterShorelineCorner::NegativeXPositiveY));
    }
    if (southEast != filledFrame.waterSurfaces.end()) {
        CHECK(hasCorner(
            *southEast,
            WaterShorelineCorner::NegativeXNegativeY));
    }
    if (southWest != filledFrame.waterSurfaces.end()) {
        CHECK(hasCorner(
            *southWest,
            WaterShorelineCorner::PositiveXNegativeY));
    }

    state.movables.push_back({
        .type = TileType::Ice,
        .cell = { 2, 1, 1 },
        .fallen = true,
    });
    presentation.resetEntities(state);
    const RenderFrameData joinedFrame = buildFrame();
    CHECK(joinedFrame.waterSurfaces.size() == 10);

    auto joinedWaterAt = [&](int x, int y) {
        return std::ranges::find_if(
            joinedFrame.waterSurfaces,
            [&](const RenderFrameData::WaterSurface& water) {
                return water.cell == GridPosition3 { x, y, 0 };
            });
    };
    const auto aboveFirst = joinedWaterAt(1, 0);
    const auto aboveSecond = joinedWaterAt(2, 0);
    const auto outerNorthWest = joinedWaterAt(0, 0);
    const auto outerNorthEast = joinedWaterAt(3, 0);
    CHECK(aboveFirst != joinedFrame.waterSurfaces.end());
    CHECK(aboveSecond != joinedFrame.waterSurfaces.end());
    CHECK(outerNorthWest != joinedFrame.waterSurfaces.end());
    CHECK(outerNorthEast != joinedFrame.waterSurfaces.end());
    if (aboveFirst != joinedFrame.waterSurfaces.end()) {
        CHECK(hasEdge(*aboveFirst, WaterShorelineEdge::PositiveY));
        CHECK(!hasCorner(
            *aboveFirst,
            WaterShorelineCorner::PositiveXPositiveY));
    }
    if (aboveSecond != joinedFrame.waterSurfaces.end()) {
        CHECK(hasEdge(*aboveSecond, WaterShorelineEdge::PositiveY));
        CHECK(!hasCorner(
            *aboveSecond,
            WaterShorelineCorner::NegativeXPositiveY));
    }
    if (outerNorthWest != joinedFrame.waterSurfaces.end()) {
        CHECK(hasCorner(
            *outerNorthWest,
            WaterShorelineCorner::PositiveXPositiveY));
    }
    if (outerNorthEast != joinedFrame.waterSurfaces.end()) {
        CHECK(hasCorner(
            *outerNorthEast,
            WaterShorelineCorner::NegativeXPositiveY));
    }
}

void testDrownedPlayerRemainsVisibleBelowWaterAndPlaysDeathTransition()
{
    TEST("drownedPlayerRemainsVisibleBelowWaterAndPlaysDeathTransition");
    const Level level = Level::loadFromLayers({
        { ".W#" },
        { "C  " },
    }, "drowned player frame");
    const GameState before = stateWithPlayer(level.playerStart());
    GameState drowned = before;
    drowned.player = { 1, 0, 1 };
    drowned.playerDead = true;

    GameplayPresentation presentation;
    presentation.setPlayerClips(
        testManifest().playerMoveAnimation(),
        testManifest().playerPushAnimation());
    presentation.resetEntities(before);
    const GameplaySession::Action action {
        .before = before,
        .after = drowned,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Right,
    };
    presentation.beginAction(action);
    presentation.advanceAnimations(1.0f);

    CHECK(presentation.player().deathTransitionPlaying);
    CHECK(near(presentation.player().motion.renderPosition.z, 0.0f));

    const PresentationSettings settings;
    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = drowned,
        .moving = false,
        .activeAction = action,
        .presentation = presentation,
        .settings = settings,
    });
    CHECK(frame.waterSurfaces.size() == 1);

    const auto player = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().playerModel();
        });
    CHECK(player != frame.tiles.end());
    if (player != frame.tiles.end()) {
        CHECK(near(player->baseElevation, 0.0f));
        CHECK(player->animation == testManifest().playerDeathAnimation());
        CHECK(player->animationFallback == testManifest().playerDeadIdleAnimation());
        CHECK(!player->animationLoops);
    }

    presentation.resetEntities(drowned);
    const RenderFrameData restoredFrame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = drowned,
        .moving = false,
        .activeAction = {},
        .presentation = presentation,
        .settings = settings,
    });
    const auto restoredPlayer = std::ranges::find_if(
        restoredFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().playerModel();
        });
    CHECK(restoredPlayer != restoredFrame.tiles.end());
    if (restoredPlayer != restoredFrame.tiles.end()) {
        CHECK(restoredPlayer->animation == testManifest().playerDeadIdleAnimation());
        CHECK(restoredPlayer->animationFallback.isNone());
        CHECK(restoredPlayer->animationLoops);
    }
}

} // namespace

int main()
{
    testCameraPitchTransition();
    testSettingsNormalizeAndConvert();
    testPresentationResetClocksAndFallenTargets();
    testPresentationInterpolatesActionsAndClips();
    testGameplayFrameUsesSettingsAndPresentation();
    testMirrorTilesUseTheirModelAndOrientation();
    testMirrorActivationBuildsBeamAndDestinationGhost();
    testGameplayFrameBuildsProceduralWaterSurface();
    testWaterLayerBuildsUnboundedNonPickableExterior();
    testFilledWaterUpdatesEdgesAndRoundedCornerCaps();
    testDrownedPlayerRemainsVisibleBelowWaterAndPlaysDeathTransition();

    if (failures == 0) {
        std::cout << "PresentationTests: "
                  << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "PresentationTests: "
              << failures
              << " of "
              << checks
              << " checks failed\n";
    return 1;
}
