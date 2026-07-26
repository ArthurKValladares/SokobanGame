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
        { "name": "Smoke10", "path": "smoke10.png" }
      ],
      "models": [
        { "name": "Stone", "path": "stone.gltf" },
        { "name": "Water", "path": "water.gltf" },
        { "name": "Glass", "path": "glass.gltf" },
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Conveyor", "path": "conveyor.gltf" },
        { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }
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

void testLevelRequirementsIncludeDynamicAndStaticAssets()
{
    TEST("levelRequirementsIncludeDynamicAndStaticAssets");
    const Level level = Level::loadFromLayers({
        { "......." },
        { "C#W>RI " },
    }, "asset requirements");

    const AssetManifest& manifest = testManifest();
    const RenderAssetRequirements requirements =
        renderAssetRequirementsForLevel(level, manifest);
    CHECK(requirements.contains(manifest.playerModel()));
    CHECK(requirements.contains(manifest.modelIdByName("Bricks")));
    CHECK(!requirements.contains(manifest.modelIdByName("Water")));
    CHECK(requirements.contains(manifest.modelIdByName("Conveyor")));
    CHECK(requirements.contains(manifest.modelIdByName("Stone")));
    CHECK(requirements.contains(manifest.modelIdByName("Glass")));
    CHECK(!requirements.contains(cubeModel));
    CHECK(requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.contains(manifest.playerMoveAnimation()));
    CHECK(requirements.contains(manifest.playerPushAnimation()));
    CHECK(requirements.contains(manifest.playerDeathAnimation()));
    CHECK(requirements.contains(manifest.playerDeadIdleAnimation()));
    CHECK(requirements.modelCount() == 5);
    CHECK(requirements.animationCount() == 5);
    CHECK(requirements.textureCount() == 0);

    const Level mirrorLevel = Level::loadFromLayers({
        { ".....", ".....", ".....", ".....", "....." },
        { "C    ", "     ", "  3  ", "     ", "     " },
    }, "mirror particle requirements");
    const RenderAssetRequirements mirrorRequirements =
        renderAssetRequirementsForLevel(mirrorLevel, manifest);
    CHECK(mirrorRequirements.textureCount() == 10);
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

int main()
{
    testLevelRequirementsIncludeDynamicAndStaticAssets();
    testFrameRequirementsOnlyContainReferencedAssets();
    testMergeDeduplicatesRequirements();
    testCubeAndNoneAreNeverRequirements();

    if (failures == 0) {
        std::cout << "AssetRequirementsTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AssetRequirementsTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
