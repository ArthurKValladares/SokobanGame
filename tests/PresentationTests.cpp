// Headless tests for mutable presentation settings, entity interpolation, and
// render-frame construction.

#include "engine/AssetManifest.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"

#include <cmath>
#include <iostream>

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
        { "name": "Water", "path": "water.gltf" },
        { "name": "Glass", "path": "glass.gltf" },
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Conveyor", "path": "conveyor.gltf", "beltScroll": true },
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
        { "name": "Push", "path": "a.glb", "role": "player-push" }
      ],
      "tiles": [
        { "tile": "Wall", "model": "Bricks" },
        { "tile": "Rock", "model": "Stone" },
        { "tile": "Water", "model": "Water" },
        { "tile": "Ice", "model": "Glass" },
        { "tile": "Conveyor Up", "model": "Conveyor" },
        { "tile": "Conveyor Down", "model": "Conveyor" },
        { "tile": "Conveyor Right", "model": "Conveyor" },
        { "tile": "Conveyor Left", "model": "Conveyor" },
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
        3.0f - config::waterDepthBelowGround));
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

} // namespace

int main()
{
    testSettingsNormalizeAndConvert();
    testPresentationResetClocksAndFallenTargets();
    testPresentationInterpolatesActionsAndClips();
    testGameplayFrameUsesSettingsAndPresentation();

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
