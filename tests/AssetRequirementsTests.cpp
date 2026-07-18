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
    CHECK(requirements.contains(manifest.modelIdByName("Water")));
    CHECK(requirements.contains(manifest.modelIdByName("Conveyor")));
    CHECK(requirements.contains(manifest.modelIdByName("Stone")));
    CHECK(requirements.contains(manifest.modelIdByName("Glass")));
    CHECK(!requirements.contains(cubeModel));
    CHECK(requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.contains(manifest.playerMoveAnimation()));
    CHECK(requirements.contains(manifest.playerPushAnimation()));
    CHECK(requirements.modelCount() == 6);
    CHECK(requirements.animationCount() == 3);
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
        },
    };

    const RenderAssetRequirements requirements =
        renderAssetRequirementsForFrame(frame);
    CHECK(requirements.contains(manifest.modelIdByName("Stone")));
    CHECK(requirements.contains(manifest.playerModel()));
    CHECK(!requirements.contains(manifest.modelIdByName("Water")));
    CHECK(requirements.contains(manifest.playerMoveAnimation()));
    CHECK(!requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.modelCount() == 2);
    CHECK(requirements.animationCount() == 1);
}

void testMergeDeduplicatesRequirements()
{
    TEST("mergeDeduplicatesRequirements");
    const AssetManifest& manifest = testManifest();
    RenderAssetRequirements first;
    first.requireModel(manifest.modelIdByName("Stone"));
    first.requireAnimation(manifest.playerIdleAnimation());

    RenderAssetRequirements second;
    second.requireModel(manifest.modelIdByName("Stone"));
    second.requireModel(manifest.modelIdByName("Water"));
    second.requireAnimation(manifest.playerPushAnimation());
    first.merge(second);

    CHECK(first.modelCount() == 2);
    CHECK(first.animationCount() == 2);
    CHECK(first.contains(manifest.modelIdByName("Water")));
    CHECK(first.contains(manifest.playerPushAnimation()));
}

void testCubeAndNoneAreNeverRequirements()
{
    TEST("cubeAndNoneAreNeverRequirements");
    RenderAssetRequirements requirements;
    requirements.requireModel(cubeModel);
    requirements.requireAnimation(noAnimation);
    CHECK(requirements.empty());
    CHECK(!requirements.contains(cubeModel));
    CHECK(!requirements.contains(noAnimation));

    // Ids beyond anything required are absent, not out-of-bounds errors.
    CHECK(!requirements.contains(RenderModel { 99 }));
    CHECK(!requirements.contains(RenderAnimation { 99 }));
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
