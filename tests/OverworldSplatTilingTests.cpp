#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/OverworldSplatTiling.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace sokoban;

AssetManifest manifestWithFallback()
{
    return AssetManifest::parse(R"json({
      "format": 1,
      "textures": [
        { "name": "GroundGrass", "path": "custom/textures/grass.png" },
        { "name": "GroundRock", "path": "custom/textures/rock.png" },
        { "name": "GroundSplatMap", "path": "custom/textures/fallback.png" }
      ],
      "models": [
        { "name": "Hero", "path": "h.glb", "geometry": "skinned", "role": "player" }
      ],
      "animations": [
        { "name": "Idle", "path": "a.glb", "role": "player-idle" },
        { "name": "Move", "path": "a.glb", "role": "player-move" },
        { "name": "Push", "path": "a.glb", "role": "player-push" },
        { "name": "Death", "path": "a.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" }
      ]
    })json");
}

void addMask(AssetManifest& manifest, std::string name, std::string path = {})
{
    if (path.empty()) path = "custom/textures/" + name + ".png";
    (void)manifest.addTexture({
        .name = std::move(name),
        .path = std::move(path),
        .filter = TextureFilter::Linear,
        .colorSpace = TextureColorSpace::Linear,
    });
}

Level::GroundSplat ground(std::string mask, std::string name = "Ground")
{
    return {
        .name = std::move(name),
        .base = "GroundGrass",
        .detail = "GroundRock",
        .mask = std::move(mask),
    };
}

OverworldSplatScreen screen(OverworldScreenId id, OverworldSlot slot,
    std::vector<Level::GroundSplat> splats = {})
{
    return {
        .id = id,
        .slot = slot,
        .definition = { .layers = {{ "." }}, .groundSplats = std::move(splats) },
    };
}

OverworldSplatTilingPlan plan(const std::vector<OverworldSplatScreen>& screens,
    const AssetManifest& manifest,
    std::optional<Level::GroundSplat> selected = std::nullopt)
{
    return planOverworldSplatTiling({
        .screens = screens,
        .activeScreen = 7,
        .selectedSplat = std::move(selected),
        .screenWidth = 12,
        .screenHeight = 8,
        .manifest = manifest,
    });
}

void testDedicatedMasksUseScreenLayoutWithoutCopies()
{
    TEST("dedicatedMasksUseScreenLayoutWithoutCopies");
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "GroundSplatMapOverworld7");
    addMask(manifest, "GroundSplatMapOverworld8_1");
    addMask(manifest, "GroundSplatMapOverworld9");
    const std::vector screens {
        screen(8, { -2, 3 }, { ground("GroundSplatMapOverworld8_1") }),
        screen(7, { -3, 3 }, { ground("GroundSplatMapOverworld7") }),
        screen(9, { -5, 1 }, { ground("GroundSplatMapOverworld9") }),
    };
    const auto result = plan(screens, manifest, screens[1].definition.groundSplats[0]);
    CHECK(result.valid);
    CHECK(result.maps.size() == 3);
    CHECK(result.copies.empty());
    CHECK(result.maskChanges.empty());
    if (result.maps.size() != 3) return;
    CHECK(result.maps[0].screen == 7);
    CHECK(result.maps[0].originTiles == Vec2({ 0, 0 }));
    CHECK(result.maps[1].screen == 8);
    CHECK(result.maps[1].originTiles == Vec2({ 12, 0 }));
    CHECK(result.maps[2].originTiles == Vec2({ -24, -16 }));
}

void testLegacyFallbackIsCopiedIndependentlyAndInputsStayIntact()
{
    TEST("legacyFallbackIsCopiedIndependentlyAndInputsStayIntact");
    const AssetManifest manifest = manifestWithFallback();
    const AssetManifest before = manifest;
    const std::vector screens { screen(7, { 0, 0 }), screen(8, { 1, 1 }) };
    const auto result = plan(screens, manifest);
    CHECK(result.valid);
    CHECK(result.maps.size() == 2);
    CHECK(result.copies.size() == 2);
    CHECK(result.maskChanges.size() == 2);
    CHECK(manifest == before);
    CHECK(screens[0].definition.groundSplats.empty());
    if (result.copies.size() != 2 || result.maskChanges.size() != 2) return;
    CHECK(result.copies[0].sourceTextureName == "GroundSplatMap");
    CHECK(result.copies[1].sourceTextureName == "GroundSplatMap");
    CHECK(result.copies[0].newTextureName == "GroundSplatMapOverworld7");
    CHECK(result.copies[1].newTextureName == "GroundSplatMapOverworld8");
    CHECK(result.copies[1].relativePath == "custom/textures/ground_splat_overworld_8.png");
    CHECK(!result.maskChanges[0].before);
    CHECK(result.maskChanges[0].after.name == "Ground");
    CHECK(result.maskChanges[0].after.mask == result.maps[0].textureName);
    CHECK(result.maps[1].originTiles == Vec2({ 12, 8 }));
}

void testNameThenUniqueMaterialMatchingSkipsAmbiguityAndReversedLayers()
{
    TEST("nameThenUniqueMaterialMatchingSkipsAmbiguityAndReversedLayers");
    AssetManifest manifest = manifestWithFallback();
    for (int id = 7; id <= 11; ++id) {
        addMask(manifest, "GroundSplatMapOverworld" + std::to_string(id));
        addMask(manifest, "GroundSplatMapOverworld" + std::to_string(id) + "_1");
    }
    auto reversed = ground("GroundSplatMapOverworld11");
    std::swap(reversed.base, reversed.detail);
    const std::vector screens {
        screen(7, { 0, 0 }, { ground("GroundSplatMapOverworld7") }),
        screen(8, { 1, 0 }, { ground("GroundSplatMapOverworld8", "Meadow") }),
        screen(9, { 2, 0 }, { ground("GroundSplatMapOverworld9", "Other"),
            ground("GroundSplatMapOverworld9_1") }),
        screen(10, { 3, 0 }, { ground("GroundSplatMapOverworld10", "One"),
            ground("GroundSplatMapOverworld10_1", "Two") }),
        screen(11, { 4, 0 }, { reversed }),
    };
    const auto result = plan(screens, manifest);
    CHECK(result.valid);
    CHECK(result.maps.size() == 3);
    CHECK(result.copies.empty());
    CHECK(result.status.find("Skipped 2") != std::string::npos);
    if (result.maps.size() != 3) return;
    CHECK(result.maps[1].textureName == "GroundSplatMapOverworld8");
    CHECK(result.maps[2].textureName == "GroundSplatMapOverworld9_1");
}

void testPathAliasesAndOtherMaterialUsersCannotShareEditableCanvas()
{
    TEST("pathAliasesAndOtherMaterialUsersCannotShareEditableCanvas");
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "GroundSplatMapOverworld7", "custom/textures/shared.png");
    addMask(manifest, "GroundSplatMapOverworld8", "custom/textures/../textures/shared.png");
    auto wall = ground("GroundSplatMapOverworld7", "Wall");
    wall.base = "WallMoss";
    wall.detail = "WallRock";
    const std::vector screens {
        screen(7, { 0, 0 }, { ground("GroundSplatMapOverworld7"), wall }),
        screen(8, { 1, 0 }, { ground("GroundSplatMapOverworld8") }),
    };
    const auto result = plan(screens, manifest);
    CHECK(result.valid);
    CHECK(result.copies.size() == 2);
    CHECK(result.maskChanges.size() == 2);
    if (result.copies.size() != 2 || result.maskChanges.size() != 2) return;
    CHECK(result.copies[0].newTextureName == "GroundSplatMapOverworld7_Tiled_1");
    CHECK(result.copies[1].newTextureName == "GroundSplatMapOverworld8_Tiled_1");
    CHECK(result.maskChanges[0].before->name == "Ground");
    CHECK(result.maskChanges[0].after.mask != wall.mask);
    CHECK(result.maps[0].textureName != result.maps[1].textureName);
}

void testCustomAndSimilarScreenPrefixesAreDetached()
{
    TEST("customAndSimilarScreenPrefixesAreDetached");
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "GroundSplatMapOverworld70");
    addMask(manifest, "CustomSharedMask");
    const std::vector screens {
        screen(7, { 0, 0 }, { ground("GroundSplatMapOverworld70") }),
        screen(8, { 1, 0 }, { ground("CustomSharedMask") }),
    };
    const auto result = plan(screens, manifest);
    CHECK(result.valid);
    CHECK(result.copies.size() == 2);
    if (result.copies.size() != 2) return;
    CHECK(result.copies[0].sourceTextureName == "GroundSplatMapOverworld70");
    CHECK(result.copies[1].sourceTextureName == "CustomSharedMask");
}

void testExternalDocumentsProtectOwnedNamesAndPathAliases()
{
    TEST("externalDocumentsProtectOwnedNamesAndPathAliases");
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "GroundSplatMapOverworld7", "custom/textures/owned7.png");
    addMask(manifest, "GroundSplatMapOverworld8", "custom/textures/owned8.png");
    addMask(manifest, "PuzzleMaskAlias", "custom/textures/../textures/owned8.png");
    const std::vector screens {
        screen(7, { 0, 0 }, { ground("GroundSplatMapOverworld7") }),
        screen(8, { 1, 0 }, { ground("GroundSplatMapOverworld8") }),
    };
    const std::vector<std::string> externalNames {
        "GroundSplatMapOverworld7", "PuzzleMaskAlias",
    };
    const auto result = planOverworldSplatTiling({
        .screens = screens,
        .activeScreen = 7,
        .screenWidth = 12,
        .screenHeight = 8,
        .manifest = manifest,
        .externalMaskNames = externalNames,
    });
    CHECK(result.valid);
    CHECK(result.copies.size() == 2);
    CHECK(result.maskChanges.size() == 2);
    if (result.copies.size() != 2) return;
    CHECK(result.copies[0].sourceTextureName == "GroundSplatMapOverworld7");
    CHECK(result.copies[1].sourceTextureName == "GroundSplatMapOverworld8");
    CHECK(result.copies[0].newTextureName == "GroundSplatMapOverworld7_Tiled_1");
    CHECK(result.copies[1].newTextureName == "GroundSplatMapOverworld8_Tiled_1");
}

void testAllocationAvoidsUnregisteredFilesInBothAssetTrees()
{
    TEST("allocationAvoidsUnregisteredFilesInBothAssetTrees");
    const auto root = std::filesystem::temp_directory_path() /
        ("sokoban_splat_tiling_tests_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "source";
    const auto runtime = root / "runtime";
    std::filesystem::create_directories(source / "custom/textures");
    std::filesystem::create_directories(runtime / "custom/textures");
    const auto sourceLeftover = source / "custom/textures/ground_splat_overworld_7.png";
    const auto runtimeLeftover = runtime / "custom/textures/GroundSplatMapOverworld7_Tiled_1.png";
    { std::ofstream file(sourceLeftover); file << "source paint"; }
    { std::ofstream file(runtimeLeftover); file << "runtime paint"; }
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "ReservedAlias", "custom/textures/GroundSplatMapOverworld7_Tiled_2.png");
    const std::vector screens { screen(7, { 0, 0 }) };
    const auto result = planOverworldSplatTiling({
        .screens = screens,
        .activeScreen = 7,
        .screenWidth = 12,
        .screenHeight = 8,
        .manifest = manifest,
        .sourceAssetRoot = source,
        .runtimeAssetRoot = runtime,
    });
    CHECK(result.valid);
    CHECK(result.copies.size() == 1);
    if (result.copies.size() == 1) {
        CHECK(result.copies[0].newTextureName == "GroundSplatMapOverworld7_Tiled_3");
        CHECK(!std::filesystem::exists(source / result.copies[0].relativePath));
    }
    CHECK(std::filesystem::file_size(sourceLeftover) == 12);
    CHECK(std::filesystem::file_size(runtimeLeftover) == 13);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void testInvalidActiveSelectionFailsWithoutPartialPlan()
{
    TEST("invalidActiveSelectionFailsWithoutPartialPlan");
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "GroundSplatMapOverworld7");
    const std::vector screens {
        screen(7, { 0, 0 }, { ground("GroundSplatMapOverworld7") }),
        screen(8, { 1, 0 }),
    };
    const auto stale = plan(screens, manifest, ground("MissingMask"));
    CHECK(!stale.valid);
    CHECK(stale.maps.empty());
    CHECK(stale.copies.empty());
    CHECK(!stale.status.empty());
    const std::vector missingMask {
        screen(7, { 0, 0 }, { ground("MissingMask") }),
    };
    CHECK(!plan(missingMask, manifest).valid);
    const std::vector absentActive { screen(8, { 0, 0 }) };
    CHECK(!plan(absentActive, manifest).valid);
    const std::vector duplicateSlot { screen(7, { 0, 0 }), screen(8, { 0, 0 }) };
    CHECK(!plan(duplicateSlot, manifest).valid);
}

void testLegacyDedicatedMaskIsReusedAndBoundExplicitly()
{
    TEST("legacyDedicatedMaskIsReusedAndBoundExplicitly");
    AssetManifest manifest = manifestWithFallback();
    addMask(manifest, "GroundSplatMapOverworld7");
    const std::vector screens { screen(7, { 0, 0 }) };
    const auto result = plan(screens, manifest);
    CHECK(result.valid);
    CHECK(result.copies.empty());
    CHECK(result.maskChanges.size() == 1);
    if (result.maskChanges.size() != 1) return;
    CHECK(!result.maskChanges[0].before);
    CHECK(result.maskChanges[0].after.mask == "GroundSplatMapOverworld7");
}

} // namespace

int main()
{
    testDedicatedMasksUseScreenLayoutWithoutCopies();
    testLegacyFallbackIsCopiedIndependentlyAndInputsStayIntact();
    testNameThenUniqueMaterialMatchingSkipsAmbiguityAndReversedLayers();
    testPathAliasesAndOtherMaterialUsersCannotShareEditableCanvas();
    testCustomAndSimilarScreenPrefixesAreDetached();
    testExternalDocumentsProtectOwnedNamesAndPathAliases();
    testAllocationAvoidsUnregisteredFilesInBothAssetTrees();
    testInvalidActiveSelectionFailsWithoutPartialPlan();
    testLegacyDedicatedMaskIsReusedAndBoundExplicitly();
    if (failures != 0) {
        std::cerr << "OverworldSplatTilingTests: " << failures << " failure(s) of "
                  << checks << " checks\n";
        return 1;
    }
    std::cout << "OverworldSplatTilingTests: " << checks << " checks passed\n";
    return 0;
}
