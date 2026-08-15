// Headless tests for mutable presentation settings, entity interpolation, and
// render-frame construction.

#include "engine/AnimationCatalog.hpp"
#include "engine/AnimationPreviewScene.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/GameplayPresentation.hpp"
#include "engine/PresentationTransactionBuilder.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/Rules.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/WaterConfig.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/MirrorConfig.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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

struct TemporaryEditorProject {
    TemporaryEditorProject()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
            ("sokoban_presentation_tests_" + std::to_string(unique));
        source = root / "source";
        runtime = root / "runtime";
        std::filesystem::create_directories(source);
        std::filesystem::create_directories(runtime);
    }

    ~TemporaryEditorProject()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::filesystem::path source;
    std::filesystem::path runtime;
};

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
        {
          "name": "Conveyor",
          "path": "conveyor.gltf",
          "material": {
            "mode": "primitive-materials",
            "slots": [
              { "texture": "Tex", "scrollV": true }
            ]
          }
        },
        { "name": "Mirror", "path": "mirror.gltf" },
        {
          "name": "Decoration",
          "path": "decoration.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "ScreenSelectorAPlayable",
          "path": "flag-a-blue.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "ScreenSelectorASolved",
          "path": "flag-a-green.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "ScreenSelectorAUnavailable",
          "path": "flag-a-red.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "ScreenSelectorBPlayable",
          "path": "flag-b-blue.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "ScreenSelectorBSolved",
          "path": "flag-b-green.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "ScreenSelectorBUnavailable",
          "path": "flag-b-red.gltf",
          "preserveSourceScale": true
        },
        {
          "name": "Hero",
          "path": "hero.glb",
          "geometry": "skinned",
          "material": { "mode": "texture", "texture": "Tex" },
          "role": "player"
        },
        {
          "name": "Enemy",
          "path": "enemy.glb",
          "geometry": "skinned",
          "material": { "mode": "texture", "texture": "Tex" },
          "role": "enemy"
        }
      ],
      "animations": [
        { "name": "Idle", "path": "a.glb", "role": "player-idle" },
        { "name": "Move", "path": "a.glb", "role": "player-move" },
        { "name": "Push", "path": "a.glb", "role": "player-push" },
        { "name": "Death", "path": "a.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" },
        { "name": "EnemyAttack", "path": "a.glb", "role": "enemy-attack" }
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
        { "tile": "Player", "model": "Hero" },
        { "tile": "Enemy", "model": "Enemy" }
      ]
    })json");
    return manifest;
}

AnimationCatalog testAnimationCatalog()
{
    return AnimationCatalog::parse(R"json({
      "format": 2,
      "clips": [
        { "animation": "Idle", "speed": 1.0, "duration": 2.0 },
        { "animation": "Move", "speed": 1.0, "duration": 1.0 },
        { "animation": "Push", "speed": 1.0, "duration": 1.0 },
        { "animation": "Death", "speed": 1.0, "duration": 1.0 },
        { "animation": "DeadIdle", "speed": 1.0, "duration": 0.0 },
        { "animation": "EnemyAttack", "speed": 1.0, "duration": 1.0 }
      ],
      "uses": [
        { "id": "player.idle", "animation": "Idle", "speed": 1.0 },
        { "id": "player.move", "animation": "Move", "speed": 1.0 },
        { "id": "player.push", "animation": "Push", "speed": 1.0 },
        { "id": "player.death", "animation": "Death", "speed": 1.0,
          "startAfter": { "use": "enemy.attack", "event": "attack-connected" } },
        { "id": "player.dead-idle", "animation": "DeadIdle", "speed": 1.0 },
        { "id": "enemy.idle", "animation": "Idle", "speed": 1.0 },
        { "id": "enemy.attack", "animation": "EnemyAttack", "speed": 1.0,
          "events": [{ "id": "attack-connected", "at": 0.9 }] },
        { "id": "mirror-preview.player-idle", "animation": "Idle", "speed": 1.0 },
        { "id": "mirror-preview.player-dead-idle", "animation": "DeadIdle", "speed": 1.0 },
        { "id": "editor.player-idle", "animation": "Idle", "speed": 1.0 },
        { "id": "editor.enemy-idle", "animation": "Idle", "speed": 1.0 },
        { "id": "thumbnail.player-idle", "animation": "Idle", "speed": 1.0 },
        { "id": "thumbnail.enemy-idle", "animation": "Idle", "speed": 1.0 }
      ]
    })json", testManifest());
}


bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

GameState stateWithPlayer(GridPosition3 player)
{
    GameState state;
    state.players.push_back({ .cell = player });
    return state;
}

void testPresentationTransactionResolvesActorIndependentDependencies()
{
    TEST("presentationTransactionResolvesActorIndependentDependencies");
    AnimationCatalog animations = testAnimationCatalog();
    animations.setGlobalSpeed(testManifest().enemyAttackAnimation(), 2.0f);

    const EntityTarget sourceTarget { EntityKind::Enemy, 41 };
    const EntityTarget dependentTarget { EntityKind::Player, 73 };
    PresentationTransactionBuilder builder(&animations);
    builder.setInitialAnimation(sourceTarget, AnimationUse::EnemyIdle, 0.25f);
    builder.setInitialAnimation(dependentTarget, AnimationUse::PlayerIdle, 0.5f);
    builder.addMotion({
        .target = dependentTarget,
        .from = { 0.0f, 0.0f, 1.0f },
        .to = { 1.0f, 0.0f, 1.0f },
        .durationSeconds = 0.2f,
    });
    const auto source = builder.addAnimation({
        .target = sourceTarget,
        .use = AnimationUse::EnemyAttack,
        .completionUse = AnimationUse::EnemyIdle,
    });
    const auto dependent = builder.addAnimation({
        .target = dependentTarget,
        .use = AnimationUse::PlayerDeath,
        .completionUse = AnimationUse::PlayerDeadIdle,
    });
    CHECK(builder.startAfterCatalogEvent(dependent, source));

    const ActionPresentationTimeline timeline = builder.build();
    CHECK(timeline.motions.size() == 1);
    CHECK(timeline.motions[0].target == dependentTarget);
    CHECK(timeline.animations.size() == 2);

    const auto dependentTrack = std::ranges::find(
        timeline.animations,
        dependentTarget,
        &ActionAnimationTrack::target);
    CHECK(dependentTrack != timeline.animations.end());
    if (dependentTrack != timeline.animations.end()) {
        CHECK(dependentTrack->segments.size() == 1);
        CHECK(near(dependentTrack->initialClipTimeSeconds, 0.5f));
        CHECK(near(dependentTrack->segments[0].startSeconds, 0.45f));
        CHECK(dependentTrack->segments[0].completionUse ==
            AnimationUse::PlayerDeadIdle);
    }
    CHECK(near(timeline.durationSeconds, 1.45f));
}

void testPresentationTransactionRejectsDependencyCycles()
{
    TEST("presentationTransactionRejectsDependencyCycles");
    AnimationCatalog animations = testAnimationCatalog();
    PresentationTransactionBuilder builder(&animations);
    const auto first = builder.addAnimation({
        .target = { EntityKind::Player, 1 },
        .use = AnimationUse::PlayerMove,
        .completionUse = AnimationUse::PlayerIdle,
    });
    const auto second = builder.addAnimation({
        .target = { EntityKind::Movable, 2 },
        .use = AnimationUse::PlayerPush,
        .completionUse = AnimationUse::PlayerIdle,
    });
    CHECK(builder.startAfterEvent(first, second, "first-ready"));
    CHECK(builder.startAfterEvent(second, first, "second-ready"));

    bool threw = false;
    try {
        (void)builder.build();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
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

void testAnimationPreviewBuildsIsolatedStage()
{
    TEST("animationPreviewBuildsIsolatedStage");
    const RenderModel model = testManifest().enemyModel();
    const PresentationSettings settings;
    const RenderFrameData frame = animationPreviewScene::build(
        model, testManifest(), settings);

    CHECK(frame.viewMode == RenderViewMode::Isometric3D);
    CHECK(frame.levelWidth == animationPreviewScene::bedSize);
    CHECK(frame.levelHeight == animationPreviewScene::bedSize);
    CHECK(frame.levelDepth == 2);
    CHECK(frame.cameraDistanceMultiplier.has_value());
    if (frame.cameraDistanceMultiplier) {
        CHECK(near(
            *frame.cameraDistanceMultiplier,
            animationPreviewScene::cameraDistanceMultiplier));
    }
    CHECK(frame.tiles.size() == 10);
    CHECK(std::ranges::count_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model.isCube() && tile.showGrid;
        }) == 9);

    const auto actor = std::ranges::find_if(
        frame.tiles,
        [model](const RenderFrameData::Tile& tile) {
            return tile.model == model;
        });
    CHECK(actor != frame.tiles.end());
    if (actor != frame.tiles.end()) {
        const GridPosition3 expectedCell {
            static_cast<int>(animationPreviewScene::bedCenter),
            static_cast<int>(animationPreviewScene::bedCenter),
            1,
        };
        CHECK(actor->cell == expectedCell);
        CHECK(actor->animationInstanceId ==
            animationPreviewScene::animationInstanceId);
        CHECK(actor->animation.isNone());
        CHECK(actor->affectsCameraFit);
    }
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
    settings.water.surfaceColor = { -1.0f, 2.0f, 0.5f, 2.0f };
    settings.water.primaryRippleOpacity = 2.0f;
    settings.water.secondaryRippleOpacity = -1.0f;
    settings.water.rippleSpatialFrequency = 99.0f;
    settings.water.rippleSpeed = -1.0f;
    settings.water.refractionStrength = 1.0f;
    settings.water.rippleCrestHalfWidth = 0.4f;
    settings.water.rippleHaloWidth = 0.1f;
    settings.water.underwaterCausticStrength = 2.0f;
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
    CHECK(near(settings.water.surfaceColor.x, 0.0f));
    CHECK(near(settings.water.surfaceColor.y, 1.0f));
    CHECK(near(settings.water.surfaceColor.w, 0.95f));
    CHECK(near(settings.water.primaryRippleOpacity, 1.0f));
    CHECK(near(settings.water.secondaryRippleOpacity, 0.0f));
    CHECK(near(
        settings.water.rippleSpatialFrequency,
        config::maximumWaterRippleSpatialFrequency));
    CHECK(near(
        settings.water.rippleSpeed,
        config::minimumWaterRippleSpeed));
    CHECK(near(
        settings.water.refractionStrength,
        config::maximumWaterRefractionStrength));
    CHECK(near(
        settings.water.rippleHaloWidth,
        settings.water.rippleCrestHalfWidth));
    CHECK(near(settings.water.underwaterCausticStrength, 1.0f));
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
    state.players[0].dead = true;
    state.players[0].drowned = true;
    state.movables.push_back({
        .type = TileType::Rock,
        .cell = { 4, 5, 2 },
        .fallen = true,
    });

    GameplayPresentation presentation;
    presentation.resetEntities(state);
    CHECK(near(presentation.players()[0].motion.renderPosition.x, 1.0f));
    CHECK(near(presentation.players()[0].motion.renderPosition.y, 2.0f));
    CHECK(near(
        presentation.players()[0].motion.renderPosition.z,
        3.0f - config::drownedPlayerDepthBelowGround));
    CHECK(presentation.players()[0].animationUse ==
        AnimationUse::PlayerDeadIdle);
    CHECK(presentation.movables().size() == 1);
    CHECK(near(
        presentation.movables()[0].renderPosition.z,
        2.0f - config::waterDepthBelowGround));

    presentation.advanceClocks(0.5f, false);
    CHECK(near(presentation.worldAnimationTimeSeconds(), 0.5f));
    CHECK(near(presentation.players()[0].clipTimeSeconds, 0.5f));
    presentation.advanceClocks(0.25f, true);
    CHECK(near(presentation.worldAnimationTimeSeconds(), 0.25f));
    CHECK(near(presentation.players()[0].clipTimeSeconds, 0.75f));
    CHECK(near(presentation.conveyorBeltScrollOffset(0.25f), 0.0f));
    CHECK(near(presentation.conveyorBeltScrollOffset(0.0f), 0.0f));
}

void testPresentationInterpolatesActionsAndClips()
{
    TEST("presentationInterpolatesActionsAndClips");
    GameplayPresentation presentation;
    GameState before = stateWithPlayer({ 0, 0, 0 });
    before.movables.push_back({
        .type = TileType::Rock,
        .cell = { 1, 0, 0 },
    });
    presentation.resetEntities(before);

    GameState after = before;
    after.players[0].cell = { 1, 1, 1 };
    after.movables[0].cell = { 2, 0, 0 };
    GameplaySession::Action action {
        .before = before,
        .after = after,
        .durationSeconds = 3.0f,
        .playerPushing = true,
        .facingDirection = MoveDirection::Left,
    };
    action.presentation = presentation.buildActionPresentation(action);
    presentation.beginAction(action, action.before);
    presentation.seekAction(action, 0.0f);

    CHECK(presentation.players()[0].motion.moving);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerPush);
    CHECK(near(presentation.players()[0].clipPlaybackRate, 1.0f));
    CHECK(presentation.players()[0].facingQuarterTurns == 1);
    CHECK(presentation.movables()[0].moving);

    presentation.seekAction(action, 0.5f);
    CHECK(near(presentation.players()[0].motion.renderPosition.x, 0.0f));
    CHECK(near(presentation.players()[0].motion.renderPosition.y, 0.0f));
    CHECK(near(presentation.players()[0].motion.renderPosition.z, 0.5f));
    CHECK(near(presentation.movables()[0].renderPosition.x, 1.0f + 1.0f / 6.0f));

    presentation.seekAction(action, 1.5f);
    CHECK(near(presentation.players()[0].motion.renderPosition.z, 1.0f));
    CHECK(near(presentation.players()[0].motion.renderPosition.x, 0.5f));
    CHECK(near(presentation.players()[0].motion.renderPosition.y, 0.0f));

    presentation.seekAction(action, 3.0f);
    CHECK(!presentation.players()[0].motion.moving);
    CHECK(near(presentation.players()[0].motion.renderPosition.x, 1.0f));
    CHECK(near(presentation.players()[0].motion.renderPosition.y, 1.0f));
    CHECK(near(presentation.players()[0].motion.renderPosition.z, 1.0f));

    presentation.finishAction(after);
    CHECK(near(presentation.players()[0].clipPlaybackRate, 1.0f));
}

void testGameplayFrameUsesSettingsAndPresentation()
{
    TEST("gameplayFrameUsesSettingsAndPresentation");
    const Level level = Level::loadFromLayers({
        { "..." },
        { "C>R" },
    }, "presentation frame");

    GameState state;
    state.players.push_back({ .cell = level.playerStart() });
    for (const Level::MovableTile& movable : level.movableTiles()) {
        state.movables.push_back({
            .type = movable.type,
            .cell = movable.position,
        });
    }
    GameplayPresentation presentation;
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
        .projectedState = action.after,
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
        .projectedState = action.after,
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

void testSelectorFlagReflectsTargetCompletion()
{
    TEST("selectorFlagReflectsTargetCompletion");
    const Level level = Level::loadFromLayers(
        {
            { ".." },
            { "C " },
        },
        "selector flag",
        std::nullopt,
        {},
        { Level::ScreenSelector {
            .id = 1,
            .cell = { 1, 0, 1 },
            .target = LevelLocation { 0, 0 },
        } });
    GameState state;
    state.players.push_back({ .cell = level.playerStart() });
    GameplayPresentation presentation;
    presentation.resetEntities(state);

    auto build = [&](ScreenSelectorViewState selectorState) {
        return RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = false,
            .projectedState = state,
            .presentation = presentation,
            .settings = PresentationSettings {},
            .selectorState = [selectorState](LevelLocation) {
                return selectorState;
            },
        });
    };

    auto checkState = [&](ScreenSelectorViewState state, std::string_view name) {
        const RenderFrameData frame = build(state);
        CHECK(std::ranges::count_if(
            frame.tiles,
            [&](const RenderFrameData::Tile& tile) {
                return tile.model == testManifest().modelIdByName(name);
            }) == 1);
        return frame;
    };

    const RenderFrameData playable = checkState(
        { .status = ScreenSelectorStatus::Playable },
        "ScreenSelectorAPlayable");
    (void)checkState(
        { .status = ScreenSelectorStatus::Solved },
        "ScreenSelectorASolved");
    (void)checkState(
        { .status = ScreenSelectorStatus::Unavailable },
        "ScreenSelectorAUnavailable");
    (void)checkState(
        { .status = ScreenSelectorStatus::Playable, .lastScreenInLevel = true },
        "ScreenSelectorBPlayable");
    (void)checkState(
        { .status = ScreenSelectorStatus::Solved, .lastScreenInLevel = true },
        "ScreenSelectorBSolved");
    (void)checkState(
        { .status = ScreenSelectorStatus::Unavailable, .lastScreenInLevel = true },
        "ScreenSelectorBUnavailable");
    const auto centeredFlag = std::ranges::find_if(
        playable.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().modelIdByName(
                "ScreenSelectorAPlayable");
        });
    CHECK(centeredFlag != playable.tiles.end());
    if (centeredFlag != playable.tiles.end() && centeredFlag->modelTransform) {
        CHECK(near(centeredFlag->modelTransform->translation.x, 1.5f));
        CHECK(near(centeredFlag->modelTransform->translation.y, 0.5f));
        CHECK(near(
            centeredFlag->position.x + centeredFlag->size.x * 0.5f,
            centeredFlag->modelTransform->translation.x));
        CHECK(near(
            centeredFlag->position.y + centeredFlag->size.y * 0.5f,
            centeredFlag->modelTransform->translation.y));
    }
}

void testDecorativeTileRendersWithoutChangingCameraExtent()
{
    TEST("decorativeTileRendersWithoutChangingCameraExtent");
    const Level level = Level::loadFromLayers({
        { "...  " },
        { "C    " },
        { "     " },
        { "    D" },
    }, "decorative presentation");
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .projectedState = {},
        .presentation = presentation,
        .settings = {},
    });

    CHECK(frame.levelWidth == 5);
    CHECK(frame.levelDepth == 4);
    CHECK(frame.cameraExtent.has_value());
    if (frame.cameraExtent) {
        CHECK(frame.cameraExtent->originX == 0);
        CHECK(frame.cameraExtent->originY == 0);
        CHECK(frame.cameraExtent->originZ == 0);
        CHECK(frame.cameraExtent->width == 3);
        CHECK(frame.cameraExtent->height == 1);
        CHECK(frame.cameraExtent->depth == 2);
    }
    CHECK(frame.waterGridBounds.originX == 0);
    CHECK(frame.waterGridBounds.originY == 0);
    CHECK(frame.waterGridBounds.width == 3);
    CHECK(frame.waterGridBounds.height == 1);
    const auto decorative = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { 4, 0, 3 };
        });
    CHECK(decorative != frame.tiles.end());
    if (decorative != frame.tiles.end()) {
        CHECK(near(decorative->height, 1.0f));
        CHECK(!decorative->affectsCameraFit);
    }
}

void testGameplayCameraExtentComesOnlyFromAuthoredLayout()
{
    TEST("gameplayCameraExtentComesOnlyFromAuthoredLayout");
    const Level level = Level::loadFromLayers({
        { "...." },
        { "CR  " },
    }, "stable authored camera extent");
    GameState state = rules::initialState(level);
    state.players[0].cell = { 40, 20, 8 };
    CHECK(!state.movables.empty());
    if (!state.movables.empty()) {
        state.movables.front().cell = { 60, 30, 12 };
    }
    GameplayPresentation presentation;
    presentation.resetEntities(state);

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .projectedState = {},
        .presentation = presentation,
        .settings = {},
    });

    CHECK(frame.cameraExtent.has_value());
    if (frame.cameraExtent) {
        CHECK(frame.cameraExtent->originX == 0);
        CHECK(frame.cameraExtent->originY == 0);
        CHECK(frame.cameraExtent->originZ == 0);
        CHECK(frame.cameraExtent->width == 4);
        CHECK(frame.cameraExtent->height == 1);
        CHECK(frame.cameraExtent->depth == 2);
    }
}

void testEditorFrameProvidesInvisibleExpansionBorderAndPreview()
{
    TEST("editorFrameProvidesInvisibleExpansionBorderAndPreview");
    LevelEditor editor;
    editor.newDocument(2, 2, false);
    editor.setSelectedTile(TileType::Decorative);

    const RenderFrameData frame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
        .hoverCell = GridPosition3 { -1, 0, 0 },
        .worldAnimationTimeSeconds = 1.25f,
    });

    CHECK(frame.levelWidth == 2);
    CHECK(frame.levelHeight == 2);
    CHECK(frame.gridPickBorder == 1);
    CHECK(near(frame.effectAnimationTimeSeconds, 1.25f));
    const auto pickCell = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { -1, 0, 0 } &&
                tile.pickOnly;
        });
    CHECK(pickCell != frame.tiles.end());
    if (pickCell != frame.tiles.end()) {
        CHECK(pickCell->affectsCameraFit);
        CHECK(near(pickCell->baseElevation, 1.0f));
    }

    const auto preview = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { -1, 0, 0 } &&
                tile.isEditorPreview;
        });
    CHECK(preview != frame.tiles.end());
    if (preview != frame.tiles.end()) {
        CHECK(preview->model == testManifest().modelForTile(
            TileType::Decorative));
    }

    editor.setCell({ -1, 0, 0 }, TileType::Decorative);
    const RenderFrameData expandedFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
    });
    CHECK(expandedFrame.levelWidth == 3);
    CHECK(expandedFrame.cameraExtent.has_value());
    if (expandedFrame.cameraExtent) {
        CHECK(expandedFrame.cameraExtent->originX == 1);
        CHECK(expandedFrame.cameraExtent->originY == 0);
        CHECK(expandedFrame.cameraExtent->width == 2);
        CHECK(expandedFrame.cameraExtent->height == 2);
    }
    CHECK(expandedFrame.waterGridBounds.originX == 1);
    CHECK(expandedFrame.waterGridBounds.originY == 0);
    CHECK(expandedFrame.waterGridBounds.width == 2);
    CHECK(expandedFrame.waterGridBounds.height == 2);
    const auto expandedDecoration = std::ranges::find_if(
        expandedFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { 0, 0, 0 } &&
                !tile.isEditorPreview && !tile.pickOnly;
        });
    CHECK(expandedDecoration != expandedFrame.tiles.end());
    if (expandedDecoration != expandedFrame.tiles.end()) {
        CHECK(!expandedDecoration->affectsCameraFit);
    }

    editor.setActiveLayer(1);
    editor.setLayerLocked(true);
    const GridPosition3 emptyHover { 2, 1, 1 };
    const RenderFrameData lockedFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
        .hoverCell = emptyHover,
    });
    const auto stablePickSurface = std::ranges::find_if(
        lockedFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == emptyHover && tile.pickOnly &&
                !tile.isEditorPreview;
        });
    const auto lockedPreview = std::ranges::find_if(
        lockedFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == emptyHover && tile.isEditorPreview;
        });
    CHECK(stablePickSurface != lockedFrame.tiles.end());
    CHECK(lockedPreview != lockedFrame.tiles.end());
    if (stablePickSurface != lockedFrame.tiles.end()) {
        CHECK(near(stablePickSurface->baseElevation, 2.0f));
    }

    editor.setActiveLayer(0);
    const GridPosition3 occupiedHover { 1, 0, 0 };
    const RenderFrameData replacementFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
        .hoverCell = occupiedHover,
    });
    const auto stableOccupiedTile = std::ranges::find_if(
        replacementFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == occupiedHover && !tile.pickOnly &&
                !tile.isEditorPreview;
        });
    const auto replacementPreview = std::ranges::find_if(
        replacementFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == occupiedHover && tile.isEditorPreview;
        });
    CHECK(stableOccupiedTile != replacementFrame.tiles.end());
    CHECK(replacementPreview != replacementFrame.tiles.end());

    const RenderFrameData deletionFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
        .hoverCell = occupiedHover,
        .deleting = true,
    });
    const auto deletionPickProxy = std::ranges::find_if(
        deletionFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == occupiedHover && tile.pickOnly &&
                !tile.isEditorPreview;
        });
    const auto deletionPreview = std::ranges::find_if(
        deletionFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == occupiedHover && tile.isEditorPreview;
        });
    const auto drawableDeletedTile = std::ranges::find_if(
        deletionFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == occupiedHover && !tile.pickOnly &&
                !tile.isEditorPreview;
        });
    CHECK(deletionPickProxy != deletionFrame.tiles.end());
    CHECK(deletionPreview != deletionFrame.tiles.end());
    CHECK(drawableDeletedTile == deletionFrame.tiles.end());

    LevelEditor moveEditor;
    moveEditor.newDocument(2, 2, false);
    const GridPosition3 moveSource { 1, 0, 1 };
    const GridPosition3 moveDestination { 0, 1, 1 };
    moveEditor.setCell(moveSource, TileType::Decorative);
    CHECK(moveEditor.beginMove(moveSource));
    const RenderFrameData moveFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = moveEditor,
        .settings = {},
        .hoverCell = moveDestination,
        .editorPreviewTile = moveEditor.pendingMove()
            ? std::optional<TileType> { moveEditor.pendingMove()->tile }
            : std::nullopt,
    });
    const auto ditheredMoveSource = std::ranges::find_if(
        moveFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == moveSource && tile.isEditorPreview;
        });
    const auto ditheredMoveDestination = std::ranges::find_if(
        moveFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == moveDestination && tile.isEditorPreview;
        });
    const auto drawableMoveSource = std::ranges::find_if(
        moveFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == moveSource && !tile.pickOnly &&
                !tile.isEditorPreview;
        });
    CHECK(ditheredMoveSource != moveFrame.tiles.end());
    CHECK(ditheredMoveDestination != moveFrame.tiles.end());
    CHECK(drawableMoveSource == moveFrame.tiles.end());

    LevelEditor actorEditor;
    actorEditor.newDocument(2, 2, false);
    actorEditor.setCell({ 0, 0, 0 }, TileType::Enemy);
    actorEditor.setSelectedTile(TileType::Enemy);
    const RenderFrameData actorFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = actorEditor,
        .settings = {},
        .hoverCell = GridPosition3 { 1, 1, 0 },
        .worldAnimationTimeSeconds = 1.25f,
    });
    const auto placedEnemy = std::ranges::find_if(
        actorFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { 0, 0, 0 } &&
                !tile.isEditorPreview && !tile.pickOnly;
        });
    const auto enemyPreview = std::ranges::find_if(
        actorFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.cell == GridPosition3 { 1, 1, 0 } &&
                tile.isEditorPreview;
        });
    CHECK(placedEnemy != actorFrame.tiles.end());
    CHECK(enemyPreview != actorFrame.tiles.end());
    if (placedEnemy != actorFrame.tiles.end()) {
        CHECK(placedEnemy->model == testManifest().enemyModel());
        CHECK(placedEnemy->animation == testManifest().playerIdleAnimation());
        CHECK(placedEnemy->animationInstanceId != 0);
        CHECK(near(placedEnemy->animationTimeSeconds, 1.25f));
    }
    if (enemyPreview != actorFrame.tiles.end()) {
        CHECK(enemyPreview->model == testManifest().enemyModel());
        CHECK(enemyPreview->animation == testManifest().playerIdleAnimation());
        CHECK(enemyPreview->animationInstanceId != 0);
        CHECK(near(enemyPreview->animationTimeSeconds, 1.25f));
    }
}

void testEditorSelectorMoveUsesFlagPreviews()
{
    TEST("editorSelectorMoveUsesFlagPreviews");
    TemporaryEditorProject project;
    LevelEditor editor;
    editor.initialize(project.source, project.runtime, 0, 0);
    editor.newDocument(2, 2, false);
    CHECK(editor.saveDocument(project.source / "overworld.scr"));

    const GridPosition3 source { 1, 0, 1 };
    const GridPosition3 destination { 0, 1, 1 };
    CHECK(editor.placeSelector(source));
    editor.setTool(LevelEditor::Tool::Tiles);
    editor.setSelectedTile(TileType::Rock);

    const RenderModel flagModel =
        testManifest().modelIdByName("ScreenSelectorAUnavailable");
    const RenderModel rockModel = testManifest().modelForTile(TileType::Rock);
    const RenderFrameData hoverFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
        .hoverCell = source,
        .deleting = true,
        .selectingMoveSource = true,
    });
    const auto ditheredHoveredFlag = std::ranges::find_if(
        hoverFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == source && tile.model == flagModel &&
                tile.isEditorPreview;
        });
    const auto hoveredFlagPickProxy = std::ranges::find_if(
        hoverFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == source && tile.pickOnly &&
                !tile.isEditorPreview;
        });
    CHECK(ditheredHoveredFlag != hoverFrame.tiles.end());
    CHECK(hoveredFlagPickProxy != hoverFrame.tiles.end());

    CHECK(editor.beginMove(source));
    const RenderFrameData moveFrame = RenderFrameBuilder::buildEditor({
        .manifest = testManifest(),
        .editor = editor,
        .settings = {},
        .hoverCell = destination,
    });
    const auto ditheredSourceFlag = std::ranges::find_if(
        moveFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == source && tile.model == flagModel &&
                tile.isEditorPreview;
        });
    const auto destinationFlag = std::ranges::find_if(
        moveFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == destination && tile.model == flagModel &&
                tile.isEditorPreview;
        });
    const auto destinationRock = std::ranges::find_if(
        moveFrame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.cell == destination && tile.model == rockModel &&
                tile.isEditorPreview;
        });
    CHECK(ditheredSourceFlag != moveFrame.tiles.end());
    CHECK(destinationFlag != moveFrame.tiles.end());
    CHECK(destinationRock == moveFrame.tiles.end());
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
        .projectedState = {},
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
    presentation.resetEntities(state);
    presentation.advanceClocks(0.75f, false);

    auto buildFrame = [&](bool moving) {
        return RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = moving,
            .projectedState = {},
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
        CHECK(ghost->animationInstanceId != uint64_t { 0 });
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
    moved.players[0].cell = { 2, 3, 1 };
    GameplaySession::Action moveAction {
        .before = state,
        .after = moved,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Up,
    };
    moveAction.presentation = presentation.buildActionPresentation(moveAction);
    presentation.beginAction(moveAction, moveAction.before);
    presentation.seekAction(moveAction, 0.5f);
    const RenderFrameData movingFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = true,
            .projectedState = moveAction.after,
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
    movedSideways.players[0].cell = { 3, 4, 1 };
    GameplaySession::Action sidewaysAction {
        .before = state,
        .after = movedSideways,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Right,
    };
    sidewaysAction.presentation =
        presentation.buildActionPresentation(sidewaysAction);
    presentation.beginAction(sidewaysAction, sidewaysAction.before);
    presentation.seekAction(sidewaysAction, 0.1f);
    const RenderFrameData sidewaysFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = true,
            .projectedState = sidewaysAction.after,
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

    presentation.seekAction(sidewaysAction, 0.5f);
    const RenderFrameData fadedOutFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = true,
            .projectedState = sidewaysAction.after,
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
    presentation.resetEntities(state);
    presentation.advanceClocks(0.75f, false);

    PresentationSettings settings;
    settings.water.surfaceColor = { 0.12f, 0.24f, 0.36f, 0.48f };
    settings.water.underwaterCausticStrength = 0.61f;
    settings.water.refractionStrength = 0.0042f;
    settings.water.visualizeCausticsOnly = true;
    const GameplaySession::Action action;
    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .projectedState = action.after,
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
        CHECK(near(water.color.x, settings.water.surfaceColor.x));
        CHECK(near(water.color.w, settings.water.surfaceColor.w));
        const uint32_t expectedShorelineMask =
            waterShorelineBit(WaterShorelineEdge::NegativeX) |
            waterShorelineBit(WaterShorelineEdge::PositiveX);
        CHECK(water.shorelineMask == expectedShorelineMask);
    }
    CHECK(near(
        frame.waterRendering.underwaterCausticStrength,
        settings.water.underwaterCausticStrength));
    CHECK(near(
        frame.waterRendering.refractionStrength,
        settings.water.refractionStrength));
    CHECK(frame.waterRendering.visualizeCausticsOnly);
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
        .projectedState = action.after,
        .presentation = presentation,
        .settings = settings,
    });
    CHECK(filledFrame.waterSurfaces.empty());
}

void testPlayerCopiesRenderAndInterpolateTogether()
{
    TEST("playerCopiesRenderAndInterpolateTogether");
    const Level level = Level::loadFromLayers({
        { "....", "....", "...." },
        { "C   ", "    ", "    " },
    }, "player copy presentation");
    GameState before = rules::initialState(level);
    before.players.push_back({ .cell = { 0, 2, 1 } });

    GameplayPresentation presentation;
    presentation.resetEntities(before);
    CHECK(presentation.players().size() == 2);

    GameState after = before;
    after.players[0].cell = { 1, 0, 1 };
    after.players[1].cell = { 1, 2, 1 };
    GameplaySession::Action action {
        .before = before,
        .after = after,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Right,
    };
    action.presentation = presentation.buildActionPresentation(action);
    presentation.beginAction(action, action.before);
    presentation.seekAction(action, 0.5f);
    CHECK(near(presentation.players()[0].motion.renderPosition.x, 0.5f));
    CHECK(near(
        presentation.players()[1].motion.renderPosition.x, 0.5f));

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = before,
        .moving = true,
        .projectedState = action.after,
        .presentation = presentation,
        .settings = {},
    });
    CHECK(std::ranges::count_if(
        frame.tiles,
        [&](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().playerModel() &&
                tile.effect == RenderSurfaceEffect::Standard;
        }) == 2);
}

void testPlayerCopiesShareTheInputFacing()
{
    TEST("playerCopiesShareTheInputFacing");
    // Mirror copies are one character in several places, so one input gives one
    // facing. The copy that cannot move still turns; see the comment on
    // GameplayPresentation::beginAction.
    const Level level = Level::loadFromLayers({
        { "....", "....", "...." },
        { "C   ", "    ", "  # " },
    }, "player copy facing");
    GameState before = rules::initialState(level);
    before.players.push_back({ .cell = { 1, 2, 1 } });

    GameplayPresentation presentation;
    presentation.resetEntities(before);
    CHECK(presentation.players().size() == 2);

    // Only the first copy moves; the second is against the wall at { 2, 2, 1 }.
    GameState after = before;
    after.players[0].cell = { 1, 0, 1 };
    GameplaySession::Action action {
        .before = before,
        .after = after,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Right,
    };
    action.presentation = presentation.buildActionPresentation(action);
    presentation.beginAction(action, action.before);

    const uint32_t right = 3;
    CHECK(presentation.players()[0].facingQuarterTurns == right);
    CHECK(presentation.players()[1].facingQuarterTurns == right);

    // And again on an input that moves nobody: walking into a wall still turns
    // the whole set, because the input is what facing follows.
    GameplaySession::Action blocked {
        .before = after,
        .after = after,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Up,
    };
    presentation.beginAction(blocked, blocked.before);
    const uint32_t up = 2;
    CHECK(presentation.players()[0].facingQuarterTurns == up);
    CHECK(presentation.players()[1].facingQuarterTurns == up);
}

void testMirrorDuplicationPreviewsEveryDestination()
{
    TEST("mirrorDuplicationPreviewsEveryDestination");
    const Level level = Level::loadFromLayers({
        { ".....", ".....", ".....", ".....", "....." },
        { "  3  ", "     ", "  C  ", "     ", "  2  " },
    }, "mirror duplication presentation");
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .projectedState = {},
        .presentation = presentation,
        .settings = {},
    });
    CHECK(std::ranges::count_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.effect == RenderSurfaceEffect::MirrorEnergy;
        }) == 2);
    uint64_t firstGhostInstance = 0;
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.effect != RenderSurfaceEffect::MirrorEnergy) {
            continue;
        }
        CHECK(tile.animationInstanceId != uint64_t { 0 });
        if (firstGhostInstance == 0) {
            firstGhostInstance = tile.animationInstanceId;
        }
        else {
            CHECK(tile.animationInstanceId != firstGhostInstance);
        }
    }
    CHECK(std::ranges::count_if(
        frame.isoFaces,
        [](const RenderFrameData::IsoFace& face) {
            return face.effect == RenderSurfaceEffect::MirrorEnergy;
        }) == 40);
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
    presentation.resetEntities(state);

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .projectedState = {},
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
    allWaterPresentation.resetEntities(allWaterState);
    const RenderFrameData allWaterFrame =
        RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = allWaterLevel,
            .state = allWaterState,
            .moving = false,
            .projectedState = {},
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
    presentation.resetEntities(state);
    const PresentationSettings settings;

    auto buildFrame = [&] {
        return RenderFrameBuilder::buildGameplay({
            .manifest = testManifest(),
            .level = level,
            .state = state,
            .moving = false,
            .projectedState = {},
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
    drowned.players[0].cell = { 1, 0, 1 };
    drowned.players[0].dead = true;
    drowned.players[0].drowned = true;

    AnimationCatalog animations = testAnimationCatalog();
    GameplayPresentation presentation;
    presentation.setAnimationCatalog(&animations);
    presentation.resetEntities(before);
    GameplaySession::Action action {
        .before = before,
        .after = drowned,
        .durationSeconds = 1.0f,
        .facingDirection = MoveDirection::Right,
    };
    action.presentation = presentation.buildActionPresentation(action);
    presentation.beginAction(action, action.before);
    presentation.seekAction(action, 0.9f);

    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerDeath);
    CHECK(presentation.players()[0].motion.renderPosition.z < 1.0f);

    const PresentationSettings settings;
    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = drowned,
        .moving = false,
        .projectedState = action.after,
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
        CHECK(near(
            player->baseElevation,
            presentation.players()[0].motion.renderPosition.z));
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
        .projectedState = {},
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

void testGameplayFrameBuildsManifestDecorationInstances()
{
    TEST("gameplayFrameBuildsManifestDecorationInstances");
    const Level level = Level::loadFromLayers(
        {
            { "..." },
            { "C  " },
        },
        "mesh decorations",
        std::nullopt,
        {
            Level::Decoration {
                .model = "Decoration",
                .position = { 1.5f, 0.5f, 1.0f },
                .rotationDegrees = { 10.0f, 20.0f, 90.0f },
                .scale = { 0.5f, 1.5f, 2.0f },
            },
        });
    GameState state = stateWithPlayer(level.playerStart());
    GameplayPresentation presentation;
    presentation.resetEntities(state);
    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = state,
        .moving = false,
        .projectedState = {},
        .presentation = presentation,
        .settings = PresentationSettings {},
    });

    const RenderModel decoration =
        testManifest().modelIdByName("Decoration");
    const auto found = std::ranges::find_if(
        frame.tiles,
        [decoration](const RenderFrameData::Tile& tile) {
            return tile.model == decoration &&
                tile.modelTransform.has_value();
        });
    CHECK(found != frame.tiles.end());
    if (found != frame.tiles.end()) {
        CHECK(!found->pickable);
        CHECK(!found->affectsCameraFit);
        CHECK(!found->showGrid);
        CHECK(near(found->modelTransform->translation.x, 1.5f));
        CHECK(near(found->modelTransform->scale.y, 1.5f));
        CHECK(near(found->modelTransform->pivot.x, 0.0f));
        CHECK(near(found->modelTransform->pivot.y, 0.0f));
        CHECK(near(
            found->modelTransform->rotationRadians.z,
            1.57079632679f));
    }
}

void testEnemyFacingAttackAndAnimationInstances()
{
    TEST("enemyFacingAttackAndAnimationInstances");
    const Level level = Level::loadFromLayers(
        {
            { "...." },
            { "C N " },
        },
        "enemy presentation");
    GameState before = rules::initialState(level);
    AnimationCatalog animations = testAnimationCatalog();
    animations.setGlobalSpeed(
        testManifest().enemyAttackAnimation(), 2.0f);
    animations.setUseSpeed(AnimationUse::EnemyAttack, 1.5f);
    animations.setGlobalSpeed(testManifest().playerIdleAnimation(), 0.5f);
    animations.setUseSpeed(AnimationUse::EnemyIdle, 0.5f);
    GameplayPresentation presentation;
    presentation.setAnimationCatalog(&animations);
    presentation.resetEntities(before);

    presentation.advanceAnimations(0.01f, before);
    CHECK(presentation.enemies().size() == 1);
    CHECK(presentation.enemies()[0].orientation.z > 0.0f);
    CHECK(presentation.enemies()[0].orientation.z < 1.0f);

    GameState after = before;
    after.players[0].cell = { 1, 0, 1 };
    after.players[0].dead = true;
    GameplaySession::Action action {
        .before = before,
        .after = after,
        .durationSeconds = 0.15f,
        .facingDirection = MoveDirection::Right,
    };
    action.presentation = presentation.buildActionPresentation(action);
    CHECK(action.presentation.animations.size() == 2);
    CHECK(action.presentation.durationSeconds > action.durationSeconds);
    float deathStart = -1.0f;
    for (const ActionAnimationTrack& track : action.presentation.animations) {
        for (const ActionAnimationSegment& segment : track.segments) {
            if (segment.use == AnimationUse::PlayerDeath) {
                deathStart = segment.startSeconds;
            }
        }
    }
    CHECK(near(deathStart, 0.3f));

    presentation.beginAction(action, action.before);
    presentation.seekAction(action, 0.1f);
    CHECK(presentation.enemies()[0].animationUse == AnimationUse::EnemyAttack);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerMove);
    CHECK(near(presentation.players()[0].clipTimeSeconds, 0.1f));

    presentation.seekAction(action, 0.2f);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerIdle);
    const RenderFrameData waitingFrame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = after,
        .moving = true,
        .projectedState = action.after,
        .presentation = presentation,
        .settings = PresentationSettings {},
        .animations = &animations,
    });
    const auto waitingPlayer = std::ranges::find_if(
        waitingFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().playerModel();
        });
    CHECK(waitingPlayer != waitingFrame.tiles.end());
    if (waitingPlayer != waitingFrame.tiles.end()) {
        CHECK(waitingPlayer->animation ==
            testManifest().playerIdleAnimation());
        CHECK(near(waitingPlayer->animationTimeSeconds, 0.025f));
    }

    presentation.seekAction(action, deathStart + 0.01f);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerDeath);
    CHECK(presentation.enemies()[0].animationUse == AnimationUse::EnemyAttack);
    CHECK(near(presentation.players()[0].clipTimeSeconds, 0.01f));

    const RenderFrameData frame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = after,
        .moving = true,
        .projectedState = action.after,
        .presentation = presentation,
        .settings = PresentationSettings {},
        .animations = &animations,
    });
    const auto enemy = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().enemyModel();
        });
    const auto player = std::ranges::find_if(
        frame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().playerModel();
        });
    CHECK(enemy != frame.tiles.end());
    CHECK(player != frame.tiles.end());
    if (enemy != frame.tiles.end() && player != frame.tiles.end()) {
        CHECK(enemy->animation == testManifest().enemyAttackAnimation());
        CHECK(enemy->animationFallback == testManifest().playerIdleAnimation());
        CHECK(!enemy->animationLoops);
        CHECK(enemy->animationInstanceId != 0);
        CHECK(enemy->animationInstanceId != player->animationInstanceId);
        CHECK(near(enemy->baseElevation, 1.0f));
        CHECK(near(enemy->animationTimeSeconds, 0.93f));
        CHECK(near(enemy->animationFallbackTimeSeconds, 0.0775f));
        CHECK(player->animation == testManifest().playerDeathAnimation());
        CHECK(near(player->animationTimeSeconds, 0.01f));
    }

    const GameplaySession::Action undo {
        .before = after,
        .after = before,
        .durationSeconds = 0.15f,
        .reversed = true,
        .facingDirection = MoveDirection::Left,
        .presentation = action.presentation,
    };
    CHECK(near(
        presentation.reverseDuration(undo),
        action.presentation.durationSeconds));
    presentation.beginAction(undo, undo.before);
    presentation.seekAction(undo, 0.0f);
    CHECK(presentation.players()[0].animationUse ==
        AnimationUse::PlayerDeadIdle);
    CHECK(!presentation.players()[0].motion.moving);
    CHECK(near(presentation.players()[0].clipPlaybackRate, -1.0f));

    const float deathSampleElapsed =
        action.presentation.durationSeconds - (deathStart + 0.01f);
    presentation.seekAction(undo, deathSampleElapsed);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerDeath);
    CHECK(!presentation.players()[0].motion.moving);
    CHECK(presentation.enemies()[0].animationUse == AnimationUse::EnemyAttack);
    CHECK(near(presentation.players()[0].clipTimeSeconds, 0.01f));

    const float waitingSampleElapsed =
        action.presentation.durationSeconds - (deathStart - 0.01f);
    presentation.seekAction(undo, waitingSampleElapsed);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerIdle);
    CHECK(presentation.enemies()[0].animationUse == AnimationUse::EnemyAttack);

    const float movementSampleElapsed =
        action.presentation.durationSeconds - 0.1f;
    presentation.seekAction(undo, movementSampleElapsed);
    CHECK(presentation.players()[0].motion.moving);
    CHECK(presentation.players()[0].motion.renderPosition.x < 1.0f);
    CHECK(presentation.players()[0].motion.renderPosition.x > 0.0f);
    CHECK(presentation.players()[0].animationUse == AnimationUse::PlayerMove);
    CHECK(near(presentation.players()[0].clipTimeSeconds, 0.1f));

    const RenderFrameData undoFrame = RenderFrameBuilder::buildGameplay({
        .manifest = testManifest(),
        .level = level,
        .state = after,
        .moving = true,
        .projectedState = undo.after,
        .presentation = presentation,
        .settings = PresentationSettings {},
        .animations = &animations,
    });
    const auto revivedPlayer = std::ranges::find_if(
        undoFrame.tiles,
        [](const RenderFrameData::Tile& tile) {
            return tile.model == testManifest().playerModel();
        });
    CHECK(revivedPlayer != undoFrame.tiles.end());
    if (revivedPlayer != undoFrame.tiles.end()) {
        CHECK(revivedPlayer->animation ==
            testManifest().playerMoveAnimation());
        CHECK(!revivedPlayer->animationCrossfades);
        CHECK(near(revivedPlayer->animationTimeSeconds, 0.1f));
    }

    presentation.seekAction(undo, action.presentation.durationSeconds);
    CHECK(!presentation.players()[0].motion.moving);
    CHECK(near(presentation.players()[0].motion.renderPosition.x, 0.0f));
}

} // namespace

int main()
{
    try {
    testPresentationTransactionResolvesActorIndependentDependencies();
    testPresentationTransactionRejectsDependencyCycles();
    testCameraPitchTransition();
    testAnimationPreviewBuildsIsolatedStage();
    testSettingsNormalizeAndConvert();
    testPresentationResetClocksAndFallenTargets();
    testPresentationInterpolatesActionsAndClips();
    testGameplayFrameUsesSettingsAndPresentation();
    testSelectorFlagReflectsTargetCompletion();
    testDecorativeTileRendersWithoutChangingCameraExtent();
    testGameplayCameraExtentComesOnlyFromAuthoredLayout();
    testEditorFrameProvidesInvisibleExpansionBorderAndPreview();
    testEditorSelectorMoveUsesFlagPreviews();
    testMirrorTilesUseTheirModelAndOrientation();
    testMirrorActivationBuildsBeamAndDestinationGhost();
    testPlayerCopiesRenderAndInterpolateTogether();
    testPlayerCopiesShareTheInputFacing();
    testMirrorDuplicationPreviewsEveryDestination();
    testGameplayFrameBuildsProceduralWaterSurface();
    testWaterLayerBuildsUnboundedNonPickableExterior();
    testFilledWaterUpdatesEdgesAndRoundedCornerCaps();
    testDrownedPlayerRemainsVisibleBelowWaterAndPlaysDeathTransition();
    testGameplayFrameBuildsManifestDecorationInstances();
    testEnemyFacingAttackAndAnimationInstances();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

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
