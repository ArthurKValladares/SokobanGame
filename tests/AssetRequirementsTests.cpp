// Headless tests for level/frame render asset planning.

#include "engine/AssetManifest.hpp"
#include "engine/Level.hpp"
#include "engine/render/RenderAssetRequirements.hpp"

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
        { "name": "Smoke01", "path": "smoke01.png" },
        { "name": "Smoke02", "path": "smoke02.png" },
        { "name": "Smoke03", "path": "smoke03.png" },
        { "name": "Smoke04", "path": "smoke04.png" },
        { "name": "Smoke05", "path": "smoke05.png" },
        { "name": "Smoke06", "path": "smoke06.png" },
        { "name": "Smoke07", "path": "smoke07.png" },
        { "name": "Smoke08", "path": "smoke08.png" },
        { "name": "Smoke09", "path": "smoke09.png" },
        { "name": "Smoke10", "path": "smoke10.png" },
        { "name": "GroundGrass", "path": "grass.png" },
        { "name": "GroundRock", "path": "rock.png" },
        { "name": "GroundSplatMap", "path": "splat.png" },
        { "name": "GroundSplatMap0_0", "path": "splat0_0.png" },
        { "name": "GroundSplatMap0_1", "path": "splat0_1.png" },
        { "name": "GroundSplatMap2_0", "path": "splat2_0.png" }
      ],
      "models": [
        { "name": "Stone", "path": "stone.gltf" },
        { "name": "Water", "path": "water.gltf" },
        { "name": "Glass", "path": "glass.gltf" },
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Conveyor", "path": "conveyor.gltf" },
        { "name": "ScreenSelectorUnsolved", "path": "flag-blue.gltf" },
        { "name": "ScreenSelectorSolved", "path": "flag-yellow.gltf" },
        { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" },
        { "name": "Enemy", "path": "enemy.glb", "geometry": "skinned", "role": "enemy" }
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
        { "tile": "Water", "model": "Water" },
        { "tile": "Ice", "model": "Glass" },
        { "tile": "Conveyor Up", "model": "Conveyor" },
        { "tile": "Conveyor Down", "model": "Conveyor" },
        { "tile": "Conveyor Right", "model": "Conveyor" },
        { "tile": "Conveyor Left", "model": "Conveyor" },
        { "tile": "Player", "model": "Hero" },
        { "tile": "Enemy", "model": "Enemy" }
      ]
    })json");
    return manifest;
}

void testSelectorRequirementsIncludeBothFlagStates()
{
    TEST("selectorRequirementsIncludeBothFlagStates");
    const Level level = Level::loadFromLayers(
        { { ".." }, { "C " } },
        "selector requirements",
        std::nullopt,
        {},
        { Level::ScreenSelector {
            .id = 1,
            .cell = { 1, 0, 1 },
            .target = LevelLocation { .level = 0, .screen = 0 },
        } });
    const AssetManifest& manifest = testManifest();
    const RenderAssetRequirements requirements =
        renderAssetRequirementsForLevel(level, manifest);

    CHECK(requirements.contains(
        manifest.modelIdByName("ScreenSelectorUnsolved")));
    CHECK(requirements.contains(
        manifest.modelIdByName("ScreenSelectorSolved")));
}

void testLevelRequirementsIncludeDynamicAndStaticAssets()
{
    TEST("levelRequirementsIncludeDynamicAndStaticAssets");
    const Level level = Level::loadFromLayers({
        { "........" },
        { "C#W>RIN" },
    }, "asset requirements", std::nullopt, {
        Level::Decoration {
            .model = "Water",
            .position = { 2.5f, 0.5f, 2.0f },
        },
    });

    const AssetManifest& manifest = testManifest();
    const RenderAssetRequirements requirements =
        renderAssetRequirementsForLevel(level, manifest);
    CHECK(requirements.contains(manifest.playerModel()));
    CHECK(requirements.contains(manifest.modelIdByName("Bricks")));
    // The water tile is procedural, but a decoration can explicitly use the
    // manifest model and must warm it for screen transitions.
    CHECK(requirements.contains(manifest.modelIdByName("Water")));
    CHECK(requirements.contains(manifest.modelIdByName("Conveyor")));
    CHECK(requirements.contains(manifest.modelIdByName("Stone")));
    CHECK(requirements.contains(manifest.modelIdByName("Glass")));
    CHECK(!requirements.contains(cubeModel));
    CHECK(requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.contains(manifest.playerMoveAnimation()));
    CHECK(requirements.contains(manifest.playerPushAnimation()));
    CHECK(requirements.contains(manifest.playerDeathAnimation()));
    CHECK(requirements.contains(manifest.playerDeadIdleAnimation()));
    CHECK(requirements.contains(manifest.enemyModel()));
    CHECK(requirements.contains(manifest.enemyAttackAnimation()));
    CHECK(requirements.modelCount() == 7);
    CHECK(requirements.animationCount() == 6);
    // The three ground splat textures are always required.
    CHECK(requirements.textureCount() == 3);

    const Level mirrorLevel = Level::loadFromLayers({
        { ".....", ".....", ".....", ".....", "....." },
        { "C    ", "     ", "  3  ", "     ", "     " },
    }, "mirror particle requirements");
    const RenderAssetRequirements mirrorRequirements =
        renderAssetRequirementsForLevel(mirrorLevel, manifest);
    // Ten smoke textures plus the three ground splat textures.
    CHECK(mirrorRequirements.textureCount() == 13);
    CHECK(mirrorRequirements.contains(
        manifest.textureIdByName("Smoke01")));
    CHECK(mirrorRequirements.contains(
        manifest.textureIdByName("Smoke10")));
}

void testFrameRequirementsOnlyContainReferencedAssets()
{
    TEST("frameRequirementsOnlyContainReferencedAssets");
    const AssetManifest& manifest = testManifest();
    RenderFrameData frame;
    frame.tiles = {
        RenderFrameData::Tile { .model = cubeModel },
        RenderFrameData::Tile { .model = manifest.modelIdByName("Stone") },
        RenderFrameData::Tile {
            .model = manifest.playerModel(),
            .animation = manifest.playerMoveAnimation(),
            .animationFallback = manifest.playerDeadIdleAnimation(),
        },
    };
    frame.particles.push_back({
        .texture = manifest.textureIdByName("Smoke03"),
    });

    const RenderAssetRequirements requirements =
        renderAssetRequirementsForFrame(frame);
    CHECK(requirements.contains(manifest.modelIdByName("Stone")));
    CHECK(requirements.contains(manifest.playerModel()));
    CHECK(!requirements.contains(manifest.modelIdByName("Water")));
    CHECK(requirements.contains(manifest.playerMoveAnimation()));
    CHECK(requirements.contains(manifest.playerDeadIdleAnimation()));
    CHECK(!requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.modelCount() == 2);
    CHECK(requirements.animationCount() == 2);
    CHECK(requirements.textureCount() == 1);
    CHECK(requirements.contains(manifest.textureIdByName("Smoke03")));
}

void testMergeDeduplicatesRequirements()
{
    TEST("mergeDeduplicatesRequirements");
    const AssetManifest& manifest = testManifest();
    RenderAssetRequirements first;
    first.requireModel(manifest.modelIdByName("Stone"));
    first.requireAnimation(manifest.playerIdleAnimation());
    first.requireTexture(manifest.textureIdByName("Smoke01"));

    RenderAssetRequirements second;
    second.requireModel(manifest.modelIdByName("Stone"));
    second.requireModel(manifest.modelIdByName("Water"));
    second.requireAnimation(manifest.playerPushAnimation());
    second.requireTexture(manifest.textureIdByName("Smoke01"));
    second.requireTexture(manifest.textureIdByName("Smoke02"));
    first.merge(second);

    CHECK(first.modelCount() == 2);
    CHECK(first.animationCount() == 2);
    CHECK(first.textureCount() == 2);
    CHECK(first.contains(manifest.modelIdByName("Water")));
    CHECK(first.contains(manifest.playerPushAnimation()));
    CHECK(first.contains(manifest.textureIdByName("Smoke02")));
}

void testCubeAndNoneAreNeverRequirements()
{
    TEST("cubeAndNoneAreNeverRequirements");
    RenderAssetRequirements requirements;
    requirements.requireModel(cubeModel);
    requirements.requireAnimation(noAnimation);
    requirements.requireTexture(noTexture);
    CHECK(requirements.empty());
    CHECK(!requirements.contains(cubeModel));
    CHECK(!requirements.contains(noAnimation));
    CHECK(!requirements.contains(noTexture));

    // Ids beyond anything required are absent, not out-of-bounds errors.
    CHECK(!requirements.contains(RenderModel { 99 }));
    CHECK(!requirements.contains(RenderAnimation { 99 }));
    CHECK(!requirements.contains(RenderTexture { 99 }));
}

} // namespace

void testGroundSplatTexturesAreRequired()
{
    TEST("groundSplatTexturesAreRequired");
    const AssetManifest& manifest = testManifest();
    const Level level = Level::loadFromLayers({
        { "..." },
        { "C.." },
    }, "ground splat requirements");

    // Regression: splat textures are sampled straight from the descriptor
    // array, so failing to require them leaves those slots holding the 1x1
    // fallback and the ground renders flat.
    const RenderAssetRequirements levelRequirements =
        renderAssetRequirementsForLevel(level, manifest);
    CHECK(levelRequirements.contains(
        manifest.textureIdByName(groundSplatBaseTextureName)));
    CHECK(levelRequirements.contains(
        manifest.textureIdByName(groundSplatDetailTextureName)));
    CHECK(levelRequirements.contains(
        manifest.textureIdByName(groundSplatMapTextureName)));

    RenderFrameData frame;
    frame.groundSplat = {
        .base = manifest.textureIdByName(groundSplatBaseTextureName),
        .detail = manifest.textureIdByName(groundSplatDetailTextureName),
        .splatMap = manifest.textureIdByName(groundSplatMapTextureName),
    };
    const RenderAssetRequirements frameRequirements =
        renderAssetRequirementsForFrame(frame);
    CHECK(frameRequirements.contains(frame.groundSplat.base));
    CHECK(frameRequirements.contains(frame.groundSplat.detail));
    CHECK(frameRequirements.contains(frame.groundSplat.splatMap));

    // Unset ids stay absent, so a manifest without the textures is fine.
    const RenderAssetRequirements empty =
        renderAssetRequirementsForFrame(RenderFrameData {});
    CHECK(!empty.contains(frame.groundSplat.base));

    // The draw path reuses one requirement set. Filling it from an empty
    // frame must clear the prior frame's bits without releasing its storage.
    RenderAssetRequirements reused;
    renderAssetRequirementsForFrame(frame, reused);
    CHECK(reused.contains(frame.groundSplat.base));
    renderAssetRequirementsForFrame(RenderFrameData {}, reused);
    CHECK(reused.empty());
}

void testPerScreenSplatMapsAreSelectedAndFallBack()
{
    TEST("perScreenSplatMapsAreSelectedAndFallBack");
    const AssetManifest& manifest = testManifest();
    const Level level = Level::loadFromLayers({
        { "..." },
        { "C.." },
    }, "per-screen splat requirements");

    // The fixture declares maps for screens 0:0, 0:1 and 2:0 but deliberately
    // not 1:0, so both the per-screen path and the shared fallback are covered.
    const RenderTexture shared =
        manifest.textureIdByName(groundSplatMapTextureName);
    const RenderTexture screen00 =
        manifest.textureIdByName("GroundSplatMap0_0");
    const RenderTexture screen01 =
        manifest.textureIdByName("GroundSplatMap0_1");
    const RenderTexture screen20 =
        manifest.textureIdByName("GroundSplatMap2_0");
    CHECK(screen00 != shared);

    // Screens of the same level get different maps - the whole point of
    // keying on the screen rather than the level.
    CHECK(screen00 != screen01);

    // A screen preloads its own map and only its own map: pulling in every
    // screen's map would defeat the point of requirement-driven residency.
    const RenderAssetRequirements first =
        renderAssetRequirementsForLevel(level, manifest, LevelLocation { 0, 0 });
    CHECK(first.contains(screen00));
    CHECK(!first.contains(screen01));
    CHECK(!first.contains(shared));
    CHECK(!first.contains(screen20));

    const RenderAssetRequirements second =
        renderAssetRequirementsForLevel(level, manifest, LevelLocation { 0, 1 });
    CHECK(second.contains(screen01));
    CHECK(!second.contains(screen00));

    // Screen 1:0 has no map of its own, so it shares the fallback rather than
    // rendering without one. Note this is not level 1 falling back as a whole:
    // the fallback is per screen.
    const RenderAssetRequirements missing =
        renderAssetRequirementsForLevel(level, manifest, LevelLocation { 1, 0 });
    CHECK(missing.contains(shared));
    CHECK(!missing.contains(screen00));

    // An unset location (the editor, and any caller that predates per-screen
    // maps) also lands on the shared map.
    const RenderAssetRequirements unlocated =
        renderAssetRequirementsForLevel(level, manifest);
    CHECK(unlocated.contains(shared));

    // Exactly one splat map either way: base + detail + one map.
    CHECK(first.textureCount() == 3);
    CHECK(missing.textureCount() == 3);

    // The name helper is what ties manifest entries to screens; if it drifts,
    // every screen silently falls back to the shared map.
    CHECK(groundSplatMapTextureNameForScreen({ 0, 0 }) == "GroundSplatMap0_0");
    CHECK(groundSplatMapTextureNameForScreen({ 12, 3 }) == "GroundSplatMap12_3");
    // Level/screen must not be ambiguous once concatenated: 1:23 and 12:3 are
    // different screens and must not resolve to the same texture name.
    CHECK(groundSplatMapTextureNameForScreen({ 1, 23 }) !=
        groundSplatMapTextureNameForScreen({ 12, 3 }));
}

int main()
{
    testLevelRequirementsIncludeDynamicAndStaticAssets();
    testSelectorRequirementsIncludeBothFlagStates();
    testFrameRequirementsOnlyContainReferencedAssets();
    testMergeDeduplicatesRequirements();
    testCubeAndNoneAreNeverRequirements();
    testGroundSplatTexturesAreRequired();
    testPerScreenSplatMapsAreSelectedAndFallBack();

    if (failures == 0) {
        std::cout << "AssetRequirementsTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AssetRequirementsTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
