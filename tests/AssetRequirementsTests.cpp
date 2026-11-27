// Headless tests for level/frame render asset planning.

#include "TestHarness.hpp"

#include "engine/ActionFeedbackConfig.hpp"
#include "engine/AnimationCatalog.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/CampaignSession.hpp"
#include "engine/Level.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/render/RenderAssetRequirements.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace sokoban;

[[nodiscard]] std::string_view testManifestJson()
{
    return R"json({
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
        { "name": "GroundRockSide", "path": "rock_side.png" },
        { "name": "GroundSplatMap", "path": "splat.png" },
        { "name": "GroundSplatMap0_0", "path": "splat0_0.png" },
        { "name": "GroundSplatMap0_1", "path": "splat0_1.png" },
        { "name": "GroundSplatMap2_0", "path": "splat2_0.png" },
        { "name": "GroundSplatMapOverworld1", "path": "overworld1.png" },
        { "name": "GroundSplatMapOverworld2", "path": "overworld2.png" },
        { "name": "Muzzle01", "path": "muzzle.png" },
        { "name": "ParticleGlow", "path": "glow.png" },
        { "name": "LaserBeam", "path": "laser.png" }
      ],
      "models": [
        { "name": "Stone", "path": "stone.gltf" },
        { "name": "Water", "path": "water.gltf" },
        { "name": "Glass", "path": "glass.gltf" },
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Conveyor", "path": "conveyor.gltf" },
        { "name": "Turret", "path": "turret.gltf" },
        { "name": "ScreenSelectorAPlayable", "path": "flag-a-blue.gltf" },
        { "name": "ScreenSelectorASolved", "path": "flag-a-green.gltf" },
        { "name": "ScreenSelectorAUnavailable", "path": "flag-a-red.gltf" },
        { "name": "ScreenSelectorBPlayable", "path": "flag-b-blue.gltf" },
        { "name": "ScreenSelectorBSolved", "path": "flag-b-green.gltf" },
        { "name": "ScreenSelectorBUnavailable", "path": "flag-b-red.gltf" },
        { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" },
        { "name": "Knight", "path": "knight.glb", "geometry": "skinned" },
        { "name": "Enemy", "path": "enemy.glb", "geometry": "skinned", "role": "enemy" }
      ],
      "animations": [
        { "name": "Idle", "path": "a.glb", "role": "player-idle" },
        { "name": "Move", "path": "a.glb", "role": "player-move" },
        { "name": "Push", "path": "a.glb", "role": "player-push" },
        { "name": "Pull", "path": "a.glb", "role": "player-pull" },
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
        { "tile": "Turret North", "model": "Turret" },
        { "tile": "Turret East", "model": "Turret" },
        { "tile": "Turret South", "model": "Turret" },
        { "tile": "Turret West", "model": "Turret" },
        { "tile": "Player", "model": "Hero" },
        { "tile": "Enemy", "model": "Enemy" }
      ]
    })json";
}

const AssetManifest& testManifest()
{
    static const AssetManifest manifest =
        AssetManifest::parse(testManifestJson());
    return manifest;
}

// A composed overworld always stars the Lorekeeper, which the shared fixture
// has no model for. Added here rather than there so the model counts the
// other tests assert stay as they are.
const AssetManifest& overworldTestManifest()
{
    static const AssetManifest manifest = [] {
        std::string json(testManifestJson());
        constexpr std::string_view anchor = R"({ "name": "Hero",)";
        const std::size_t position = json.find(anchor);
        if (position == std::string::npos) {
            throw std::logic_error("overworld fixture lost its anchor model");
        }
        json.insert(position,
            R"({ "name": "Lorekeeper", "path": "lorekeeper.glb", "geometry": "skinned" },
        )");
        return AssetManifest::parse(json);
    }();
    return manifest;
}

void testSelectorRequirementsIncludeEveryFlagState()
{
    TEST("selectorRequirementsIncludeEveryFlagState");
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

    for (std::string_view name : {
            "ScreenSelectorAPlayable",
            "ScreenSelectorASolved",
            "ScreenSelectorAUnavailable",
            "ScreenSelectorBPlayable",
            "ScreenSelectorBSolved",
            "ScreenSelectorBUnavailable",
        }) {
        CHECK(requirements.contains(manifest.modelIdByName(name)));
    }
}

void testLevelRequirementsIncludeDynamicAndStaticAssets()
{
    TEST("levelRequirementsIncludeDynamicAndStaticAssets");
    const Level level = Level::loadFromLayers({
        { "........" },
        { "C#W>RINe" },
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
    CHECK(requirements.contains(manifest.modelIdByName("Turret")));
    CHECK(requirements.contains(manifest.textureIdByName("ParticleGlow")));
    CHECK(!requirements.contains(cubeModel));
    CHECK(requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.contains(manifest.playerMoveAnimation()));
    CHECK(requirements.contains(manifest.playerPushAnimation()));
    CHECK(requirements.contains(manifest.playerDeathAnimation()));
    CHECK(requirements.contains(manifest.playerDeadIdleAnimation()));
    CHECK(requirements.contains(manifest.enemyModel()));
    CHECK(requirements.contains(manifest.enemyAttackAnimation()));
    CHECK(requirements.modelCount() == 8);
    CHECK(requirements.animationCount() == 7);
    // Three splat textures, side sandstone and the flame endpoint glow.
    CHECK(requirements.textureCount() == 5);

    const Level mirrorLevel = Level::loadFromLayers({
        { ".....", ".....", ".....", ".....", "....." },
        { "C    ", "     ", "  3  ", "     ", "     " },
    }, "mirror particle requirements");
    const RenderAssetRequirements mirrorRequirements =
        renderAssetRequirementsForLevel(mirrorLevel, manifest);
    // Ten smoke textures plus the three splat textures and side sandstone.
    CHECK(mirrorRequirements.textureCount() == 14);
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

void testActionRequirementsKeepFirstUseAssetsWithoutTerrainOrModels()
{
    TEST("actionRequirementsKeepFirstUseAssetsWithoutTerrainOrModels");
    const AssetManifest& manifest = testManifest();
    const Level quiet = Level::loadFromLayers(
        { { "..." }, { "C.." } }, "quiet action requirements");
    const auto quietRequirements =
        renderActionAssetRequirementsForLevel(quiet, manifest);
    CHECK(quietRequirements.modelCount() == 0);
    CHECK(quietRequirements.textureCount() == 0);
    CHECK(quietRequirements.animationCount() == 6);
    CHECK(quietRequirements.contains(manifest.playerIdleAnimation()));
    CHECK(quietRequirements.contains(manifest.playerMoveAnimation()));
    CHECK(quietRequirements.contains(manifest.playerPushAnimation()));
    CHECK(quietRequirements.contains(manifest.playerPullAnimation()));
    CHECK(quietRequirements.contains(manifest.playerDeathAnimation()));
    CHECK(quietRequirements.contains(manifest.playerDeadIdleAnimation()));
    CHECK(!quietRequirements.contains(manifest.enemyAttackAnimation()));

    // Dust can rise wherever a hero walks, so every level warms its sprites.
    // The shared fixture declares none of them, which is why the quiet level
    // above requires no texture at all.
    const AssetManifest dusty = [] {
        std::string json(testManifestJson());
        constexpr std::string_view anchor = R"({ "name": "Smoke01",)";
        const std::size_t position = json.find(anchor);
        if (position == std::string::npos) {
            throw std::logic_error("dust fixture lost its anchor texture");
        }
        std::string sprites;
        for (std::string_view name : config::actionFeedbackDustTextureNames) {
            sprites += R"({ "name": ")" + std::string(name) +
                R"(", "path": "puff.png" },
        )";
        }
        json.insert(position, sprites);
        return AssetManifest::parse(json);
    }();
    const auto dustRequirements = renderActionAssetRequirementsForLevel(quiet, dusty);
    CHECK(dustRequirements.modelCount() == 0);
    CHECK(dustRequirements.animationCount() == 6);
    CHECK(dustRequirements.textureCount() ==
        config::actionFeedbackDustTextureNames.size());
    for (std::string_view name : config::actionFeedbackDustTextureNames) {
        CHECK(dustRequirements.contains(dusty.textureIdByName(name)));
    }

    const Level effects = Level::loadFromDefinition({
        .layers = { { "......" }, { "C3NGeP" } },
        .gates = { { .cell = { 3, 0, 1 },
                     .pressurePlates = { { 5, 0, 1 } } } },
    }, "brief action effects");
    const auto effectsRequirements =
        renderActionAssetRequirementsForLevel(effects, manifest);
    CHECK(effectsRequirements.modelCount() == 0);
    CHECK(effectsRequirements.animationCount() == 7);
    CHECK(effectsRequirements.contains(manifest.enemyAttackAnimation()));
    for (std::string_view textureName : config::mirrorSwapSmokeTextureNames) {
        CHECK(effectsRequirements.contains(manifest.textureIdByName(textureName)));
    }
    CHECK(effectsRequirements.contains(
        manifest.textureIdByName(config::gateParticleTextureName)));
    CHECK(effectsRequirements.textureCount() == 11);
    CHECK(!effectsRequirements.contains(manifest.textureIdByName("GroundGrass")));
    CHECK(!effectsRequirements.contains(manifest.textureIdByName("GroundRock")));
    CHECK(!effectsRequirements.contains(manifest.textureIdByName("GroundRockSide")));
    CHECK(!effectsRequirements.contains(manifest.textureIdByName("GroundSplatMap")));

    // Gate and portal glows must also warm when no turret shares that texture.
    const Level gate = Level::loadFromDefinition({
        .layers = { { "..." }, { "CGP" } },
        .gates = { { .cell = { 1, 0, 1 },
                     .pressurePlates = { { 2, 0, 1 } } } },
    }, "gate action effect");
    const auto gateRequirements = renderActionAssetRequirementsForLevel(gate, manifest);
    CHECK(gateRequirements.textureCount() == 1);
    CHECK(gateRequirements.contains(manifest.textureIdByName("ParticleGlow")));
    CHECK(renderAssetRequirementsForLevel(gate, manifest).contains(
        manifest.textureIdByName("ParticleGlow")));

    const Level portal = Level::loadFromDefinition({
        .layers = { { "..." }, { "COO" } },
        .portals = { { .cell = { 1, 0, 1 } }, { .cell = { 2, 0, 1 } } },
    }, "portal action effect");
    const auto portalRequirements = renderActionAssetRequirementsForLevel(portal, manifest);
    CHECK(portalRequirements.modelCount() == 0);
    CHECK(portalRequirements.textureCount() == 1);
    CHECK(portalRequirements.contains(manifest.textureIdByName("ParticleGlow")));
}

void testActionRequirementsHonorCatalogAnimationBindings()
{
    TEST("actionRequirementsHonorCatalogAnimationBindings");
    const AssetManifest& manifest = testManifest();
    std::ostringstream serialized;
    serialized << R"({"format":2,"clips":[)";
    bool first = true;
    for (const auto& animation : manifest.animations()) {
        if (!first) serialized << ',';
        first = false;
        serialized << R"({"animation":")" << animation.name
                   << R"(","duration":1})";
    }
    serialized << R"(],"uses":[)";
    first = true;
    for (const auto& use : animationUseDefinitions()) {
        if (!first) serialized << ',';
        first = false;
        serialized << R"({"id":")" << use.id
                   << R"(","animation":"EnemyAttack"})";
    }
    serialized << "]}";
    const AnimationCatalog catalog = AnimationCatalog::parse(serialized.str(), manifest);
    const Level level = Level::loadFromLayers(
        { { "." }, { "C" } }, "catalog action requirements");
    const auto requirements =
        renderActionAssetRequirementsForLevel(level, manifest, &catalog);
    CHECK(requirements.animationCount() == 1);
    CHECK(requirements.contains(manifest.enemyAttackAnimation()));
    CHECK(!requirements.contains(manifest.playerIdleAnimation()));
    CHECK(requirements.modelCount() == 0);
    CHECK(requirements.textureCount() == 0);
}

void testKnightLevelRequiresKnightInsteadOfRogueModel()
{
    TEST("knightLevelRequiresKnightInsteadOfRogueModel");
    const Level level = Level::loadFromDefinition({
        .layers = {
            { "." },
            { "C" },
        },
        .character = CharacterType::Knight,
    }, "knight requirements");
    const AssetManifest& manifest = testManifest();
    const RenderAssetRequirements requirements =
        renderAssetRequirementsForLevel(level, manifest);
    CHECK(requirements.contains(manifest.modelIdByName("Knight")));
    CHECK(!requirements.contains(manifest.playerModel()));
    CHECK(requirements.contains(manifest.playerIdleAnimation()));
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
    CHECK(levelRequirements.contains(manifest.textureIdByName(groundRockSideTextureName)));

    RenderFrameData frame;
    frame.groundSplat = {
        .base = manifest.textureIdByName(groundSplatBaseTextureName),
        .detail = manifest.textureIdByName(groundSplatDetailTextureName),
        .splatMap = manifest.textureIdByName(groundSplatMapTextureName),
    };
    frame.groundRockSideTexture = manifest.textureIdByName(groundRockSideTextureName);
    const RenderAssetRequirements frameRequirements =
        renderAssetRequirementsForFrame(frame);
    CHECK(frameRequirements.contains(frame.groundSplat.base));
    CHECK(frameRequirements.contains(frame.groundSplat.detail));
    CHECK(frameRequirements.contains(frame.groundSplat.splatMap));
    CHECK(frameRequirements.contains(frame.groundRockSideTexture));

    frame.groundSplatRegionCount = 1;
    frame.groundSplatRegions[0] = {
        .origin = { 9, 0 },
        .width = 9,
        .height = 7,
        .textures = groundSplatTexturesForOverworldScreen(
            [&manifest](std::string_view name) {
                return manifest.findTextureIdByName(name);
            },
            1),
    };
    const RenderAssetRequirements regionRequirements =
        renderAssetRequirementsForFrame(frame);
    CHECK(regionRequirements.contains(
        manifest.textureIdByName("GroundSplatMapOverworld1")));
    CHECK(frame.groundSplatRegionAt({ 10, 2, 1 }) ==
        &frame.groundSplatRegions[0]);
    CHECK(frame.groundSplatRegionAt({ 8, 2, 1 }) == nullptr);

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

void testTerrainPbrCompanionsAreRequired()
{
    TEST("terrainPbrCompanionsAreRequired");
    AssetManifest manifest = testManifest();
    for (const char* name : { "GroundGrassNormal", "GroundGrassOrm", "GroundRockNormal", "GroundRockOrm",
             "GroundRockSideNormal", "GroundRockSideOrm", "GroundAutumnGrass", "GroundAutumnRock",
             "GroundAutumnGrassNormal", "GroundAutumnGrassOrm", "GroundAutumnRockNormal", "GroundAutumnRockOrm" }) {
        CHECK(!manifest.addTexture({ .name = name, .path = std::string(name) + ".png",
            .tiling = true, .filter = TextureFilter::Linear,
            .colorSpace = TextureColorSpace::Linear }).isNone());
    }
    const auto find = [&manifest](std::string_view name) { return manifest.findTextureIdByName(name); };
    const GroundSplatTextures defaults = groundSplatTexturesForScreen(find, std::nullopt);
    CHECK(defaults.baseNormal == manifest.textureIdByName("GroundGrassNormal"));
    CHECK(defaults.detailNormal == manifest.textureIdByName("GroundRockNormal"));
    CHECK(defaults.baseOrm == manifest.textureIdByName("GroundGrassOrm"));
    CHECK(defaults.detailOrm == manifest.textureIdByName("GroundRockOrm"));
    CHECK(defaults.texture2 == manifest.textureIdByName("GroundAutumnGrass"));
    CHECK(defaults.texture3 == manifest.textureIdByName("GroundAutumnRock"));
    CHECK(defaults.texture2Normal == manifest.textureIdByName("GroundAutumnGrassNormal"));
    CHECK(defaults.texture3Orm == manifest.textureIdByName("GroundAutumnRockOrm"));
    CHECK(defaults.rimWall == manifest.textureIdByName("GroundRockSide"));
    CHECK(defaults.rimWallNormal == manifest.textureIdByName("GroundRockSideNormal"));
    CHECK(defaults.rimWallOrm == manifest.textureIdByName("GroundRockSideOrm"));
    CHECK(defaults.sampledTextures().size() == 16);
    const Level level = Level::loadFromLayers({ { "..." }, { "C.." } }, "terrain PBR residency");
    const auto levelRequirements = renderAssetRequirementsForLevel(level, manifest);
    for (RenderTexture texture : defaults.sampledTextures()) {
        if (!texture.isNone()) CHECK(levelRequirements.contains(texture));
    }

    // Editor-assigned layers may reverse base and detail. Resolve maps by the
    // selected albedo, rather than reusing the default grass/rock ordering.
    const GroundSplatTextures reversed = groundSplatTexturesForMaterials(find,
        "GroundRock", "GroundGrass", defaults.splatMap);
    CHECK(reversed.baseNormal == defaults.detailNormal);
    CHECK(reversed.detailOrm == defaults.baseOrm);
    CHECK(reversed.rimWall == defaults.rimWall);
    CHECK(reversed.rimWallNormal == defaults.rimWallNormal);
    CHECK(reversed.rimWallOrm == defaults.rimWallOrm);
    RenderFrameData frame;
    frame.tiles.push_back({ .groundSplat = reversed });
    const auto tileRequirements = renderAssetRequirementsForFrame(frame);
    for (RenderTexture texture : reversed.sampledTextures()) {
        if (!texture.isNone()) CHECK(tileRequirements.contains(texture));
    }
    frame.tiles.clear();
    frame.groundSplatRegionCount = 1;
    frame.groundSplatRegions[0].textures = groundSplatTexturesForOverworldScreen(find, 1);
    const auto regionRequirements = renderAssetRequirementsForFrame(frame);
    for (RenderTexture texture : frame.groundSplatRegions[0].textures.sampledTextures()) {
        if (!texture.isNone()) CHECK(regionRequirements.contains(texture));
    }

    const GroundSplatTextures legacy = groundSplatTexturesForMaterials(find,
        "GroundGrass", "Smoke01", defaults.splatMap);
    CHECK(legacy.valid());
    CHECK(legacy.detailNormal.isNone());
    CHECK(legacy.detailOrm.isNone());

    const GroundSplatTextures swappedExtras = groundSplatTexturesForMaterials(find,
        "GroundGrass", "GroundRock", defaults.splatMap, "GroundAutumnRock", "GroundAutumnGrass");
    CHECK(swappedExtras.texture2 == defaults.texture3);
    CHECK(swappedExtras.texture3Normal == defaults.texture2Normal);
    CHECK(swappedExtras.texture2Orm == defaults.texture3Orm);
}

void testRimWallTextureFallbacksAndPreviewRequirements()
{
    TEST("rimWallTextureFallbacksAndPreviewRequirements");
    AssetManifest manifest = testManifest();
    const auto find = [&manifest](std::string_view name) {
        return manifest.findTextureIdByName(name);
    };
    const GroundSplatTextures albedoOnly = groundSplatTexturesForScreen(find, std::nullopt);
    CHECK(albedoOnly.valid());
    CHECK(albedoOnly.rimWall == manifest.textureIdByName(groundRockSideTextureName));
    CHECK(albedoOnly.rimWallNormal.isNone());
    CHECK(albedoOnly.rimWallOrm.isNone());

    const GroundSplatTextures missingWall = groundSplatTexturesForScreen(
        [&](std::string_view name) {
            return name.starts_with(groundRockSideTextureName) ? noTexture : find(name);
        }, std::nullopt);
    CHECK(missingWall.valid());
    CHECK(missingWall.base == albedoOnly.base);
    CHECK(missingWall.detail == albedoOnly.detail);
    CHECK(missingWall.splatMap == albedoOnly.splatMap);
    CHECK(missingWall.rimWall.isNone());
    CHECK(missingWall.rimWallNormal.isNone());
    CHECK(missingWall.rimWallOrm.isNone());

    const RenderTexture normal = manifest.addTexture({
        .name = "GroundRockSideNormal", .path = "side_normal.png",
        .tiling = true, .filter = TextureFilter::Linear,
        .colorSpace = TextureColorSpace::Linear });
    const RenderTexture orm = manifest.addTexture({
        .name = "GroundRockSideOrm", .path = "side_orm.png",
        .tiling = true, .filter = TextureFilter::Linear,
        .colorSpace = TextureColorSpace::Linear });
    const GroundSplatTextures complete = groundSplatTexturesForOverworldScreen(find, 1);
    CHECK(complete.rimWallNormal == normal);
    CHECK(complete.rimWallOrm == orm);
    const GroundSplatTextures missingAlbedo = groundSplatTexturesForMaterials(
        [&](std::string_view name) {
            return name == groundRockSideTextureName ? noTexture : find(name);
        }, "GroundGrass", "GroundRock", complete.splatMap);
    CHECK(missingAlbedo.valid());
    CHECK(missingAlbedo.rimWall.isNone());
    CHECK(missingAlbedo.rimWallNormal == normal);
    CHECK(missingAlbedo.rimWallOrm == orm);

    // Preview frames contribute their own ground requirements to the same
    // visible asset set, even when the main frame has no wall material.
    RenderFrameData mainFrame;
    mainFrame.groundSplat = missingWall;
    RenderFrameData previewFrame;
    previewFrame.groundSplat = complete;
    RenderAssetRequirements requirements = renderAssetRequirementsForFrame(mainFrame);
    CHECK(!requirements.contains(complete.rimWall));
    CHECK(!requirements.contains(normal));
    CHECK(!requirements.contains(orm));
    requirements.merge(renderAssetRequirementsForFrame(previewFrame));
    CHECK(requirements.contains(complete.rimWall));
    CHECK(requirements.contains(normal));
    CHECK(requirements.contains(orm));
    CHECK(!requirements.contains(noTexture));
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

    // One splat map either way, plus base, detail, and the side sandstone.
    CHECK(first.textureCount() == 4);
    CHECK(missing.textureCount() == 4);

    // The name helper is what ties manifest entries to screens; if it drifts,
    // every screen silently falls back to the shared map.
    CHECK(groundSplatMapTextureNameForScreen({ 0, 0 }) == "GroundSplatMap0_0");
    CHECK(groundSplatMapTextureNameForScreen({ 12, 3 }) == "GroundSplatMap12_3");
    CHECK(groundSplatMapTextureNameForOverworldScreen(42) ==
        "GroundSplatMapOverworld42");
    CHECK(groundSplatMapAssetPathForOverworldScreen(42) ==
        "custom/textures/ground_splat_overworld_42.png");
    // Level/screen must not be ambiguous once concatenated: 1:23 and 12:3 are
    // different screens and must not resolve to the same texture name.
    CHECK(groundSplatMapTextureNameForScreen({ 1, 23 }) !=
        groundSplatMapTextureNameForScreen({ 12, 3 }));
}

void testPuzzlePrefetchOnlyPlansImmediateSuccessor()
{
    TEST("puzzlePrefetchOnlyPlansImmediateSuccessor");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 3, 2 });
    PlayerProfile profile;

    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    CHECK(profile.selectorStatus({ 0, 2 }) ==
        ScreenSelectorStatus::Unavailable);
    CHECK(puzzleAssetPrefetchTargets(campaign, profile) ==
        (std::vector<LevelLocation> { { 0, 2 } }));

    // Completing a level returns to the overworld, so its last screen must
    // not speculate into every other level or even the next numbered level.
    CHECK(campaign.startPuzzle(profile, { 0, 2 }));
    CHECK(puzzleAssetPrefetchTargets(campaign, profile).empty());
    CHECK(campaign.startPuzzle(profile, { 1, 0 }));
    CHECK(puzzleAssetPrefetchTargets(campaign, profile) ==
        (std::vector<LevelLocation> { { 1, 1 } }));
}

void testOverworldPrefetchUsesOnlySelectableActiveScreenTargets()
{
    TEST("overworldPrefetchUsesOnlySelectableActiveScreenTargets");
    const Level::Definition active {
        .layers = {
            { "....", "...." },
            { "    ", "C   " },
        },
        .selectors = {
            { .id = 1, .cell = { 0, 0, 1 }, .target = LevelLocation { 0, 0 } },
            { .id = 2, .cell = { 1, 0, 1 }, .target = LevelLocation { 0, 0 } },
            { .id = 3, .cell = { 2, 0, 1 }, .target = LevelLocation { 0, 1 } },
            { .id = 4, .cell = { 3, 0, 1 } },
            { .id = 5, .cell = { 1, 1, 1 }, .target = LevelLocation { 99, 0 } },
        },
    };
    const auto otherScreen = [](int targetLevel) {
        return Level::Definition {
            .layers = {
                { "....", "...." },
                { "    ", "    " },
            },
            .selectors = {
                { .id = 1, .cell = { 0, 0, 1 },
                  .target = LevelLocation { targetLevel, 0 } },
            },
        };
    };
    // Full in-memory overrides make this fixture independent of disk and
    // prove that selecting a preload plan needs no puzzle-file reads.
    const OverworldMap map = OverworldMap::load({}, OverworldDraftOverride {
        .layout = OverworldLayout {
            .format = 3,
            .screenWidth = 4,
            .screenHeight = 2,
            .screens = {
                { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
                { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
                { .id = 3, .file = "screen3.scr", .slot = { 2, 0 } },
            },
        },
        .definitions = {
            { .screen = 1, .definition = active },
            { .screen = 2, .definition = otherScreen(1) },
            { .screen = 3, .definition = otherScreen(2) },
        },
    });
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1, 1 });
    campaign.setOverworldTopology(map.fingerprint(), { 1, 2, 3 }, 1);
    PlayerProfile profile;

    // A duplicate flag, a locked successor, an unassigned flag, a missing
    // target and both other authored screens must not broaden disk work.
    CHECK(puzzleAssetPrefetchTargets(campaign, profile, &map) ==
        (std::vector<LevelLocation> { { 0, 0 } }));
    profile.recordScreenCompletion({ 0, 0 }, 3, 1.0);
    CHECK(puzzleAssetPrefetchTargets(campaign, profile, &map) ==
        (std::vector<LevelLocation> { { 0, 0 }, { 0, 1 } }));

    CHECK(campaign.transitionOverworldScreen(profile, 2));
    CHECK(puzzleAssetPrefetchTargets(campaign, profile, &map) ==
        (std::vector<LevelLocation> { { 1, 0 } }));
    CHECK(campaign.transitionOverworldScreen(profile, 3));
    CHECK(puzzleAssetPrefetchTargets(campaign, profile, &map) ==
        (std::vector<LevelLocation> { { 2, 0 } }));
}

void testOverworldNeighborhoodRequirementsStopAtTheVisibleScreens()
{
    TEST("overworldNeighborhoodRequirementsStopAtTheVisibleScreens");
    const auto screen = [](const char* objects) {
        return Level::Definition {
            .layers = {
                { "....", "...." },
                { objects, "    " },
            },
        };
    };
    // Four screens in a row. Only the first has a hero start, which is why
    // the planner filters the composed level instead of loading each screen.
    const OverworldMap map = OverworldMap::load({}, OverworldDraftOverride {
        .layout = OverworldLayout {
            .format = 3,
            .screenWidth = 4,
            .screenHeight = 2,
            .screens = {
                { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
                { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
                { .id = 3, .file = "screen3.scr", .slot = { 2, 0 } },
                { .id = 4, .file = "screen4.scr", .slot = { 3, 0 } },
            },
        },
        .definitions = {
            { .screen = 1, .definition = screen("C   ") },
            { .screen = 2, .definition = screen(" #  ") },
            { .screen = 3, .definition = screen("  R ") },
            { .screen = 4, .definition = screen("   I") },
        },
    });
    const AssetManifest& manifest = overworldTestManifest();
    const RenderModel hero = manifest.modelIdByName("Lorekeeper");
    const RenderModel bricks = manifest.modelIdByName("Bricks");
    const RenderModel stone = manifest.modelIdByName("Stone");
    const RenderModel glass = manifest.modelIdByName("Glass");

    // The whole composed level names all of them.
    const RenderAssetRequirements everything =
        renderAssetRequirementsForLevel(map.level(), manifest);
    CHECK(everything.contains(hero));
    CHECK(everything.contains(bricks));
    CHECK(everything.contains(stone));
    CHECK(everything.contains(glass));

    const RenderAssetRequirements start =
        renderAssetRequirementsForOverworldNeighborhood(map, 1, manifest);
    CHECK(start.contains(hero));
    CHECK(start.contains(bricks));
    CHECK(!start.contains(stone));
    CHECK(!start.contains(glass));
    // Each visible screen samples its own map when it has one.
    CHECK(start.contains(
        manifest.textureIdByName("GroundSplatMapOverworld1")));
    CHECK(start.contains(
        manifest.textureIdByName("GroundSplatMapOverworld2")));
    CHECK(start.contains(manifest.playerIdleAnimation()));

    // Actors are level-wide, so the hero stays; the board does not.
    const RenderAssetRequirements distant =
        renderAssetRequirementsForOverworldNeighborhood(map, 4, manifest);
    CHECK(distant.contains(hero));
    CHECK(!distant.contains(bricks));
    CHECK(distant.contains(stone));
    CHECK(distant.contains(glass));
    CHECK(distant.contains(manifest.textureIdByName("GroundSplatMap")));

    const RenderAssetRequirements middle =
        renderAssetRequirementsForOverworldNeighborhood(map, 2, manifest);
    CHECK(middle.contains(bricks));
    CHECK(middle.contains(stone));
    CHECK(!middle.contains(glass));

    CHECK(renderAssetRequirementsForOverworldNeighborhood(
        map, 99, manifest).empty());
}

void testOverworldTransitionRequirementsReachOneCrossingAhead()
{
    TEST("overworldTransitionRequirementsReachOneCrossingAhead");
    const auto screen = [](const char* objects) {
        return Level::Definition {
            .layers = {
                { "....", "...." },
                { objects, "    " },
            },
        };
    };
    // Five screens in a row, so that from either end one screen is two
    // crossings away and one is three.
    const OverworldMap map = OverworldMap::load({}, OverworldDraftOverride {
        .layout = OverworldLayout {
            .format = 3,
            .screenWidth = 4,
            .screenHeight = 2,
            .screens = {
                { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
                { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
                { .id = 3, .file = "screen3.scr", .slot = { 2, 0 } },
                { .id = 4, .file = "screen4.scr", .slot = { 3, 0 } },
                { .id = 5, .file = "screen5.scr", .slot = { 4, 0 } },
            },
        },
        .definitions = {
            { .screen = 1, .definition = screen("C   ") },
            { .screen = 2, .definition = screen(" #  ") },
            { .screen = 3, .definition = screen("  R ") },
            { .screen = 4, .definition = screen("   I") },
            { .screen = 5, .definition = screen(" n  ") },
        },
    });
    const AssetManifest& manifest = overworldTestManifest();
    const RenderModel bricks = manifest.modelIdByName("Bricks");
    const RenderModel stone = manifest.modelIdByName("Stone");
    const RenderModel glass = manifest.modelIdByName("Glass");
    const RenderModel turret = manifest.modelIdByName("Turret");

    // The frame on the first screen draws the first two. Crossing to the
    // second brings the third into view, and nothing beyond it.
    const RenderAssetRequirements fromStart =
        renderAssetRequirementsForOverworldTransitions(map, 1, manifest);
    CHECK(fromStart.contains(manifest.modelIdByName("Lorekeeper")));
    CHECK(fromStart.contains(bricks));
    CHECK(fromStart.contains(stone));
    CHECK(!fromStart.contains(glass));
    CHECK(!fromStart.contains(turret));
    // The third screen has no splat map of its own and samples the shared one.
    CHECK(fromStart.contains(
        manifest.textureIdByName("GroundSplatMapOverworld2")));
    CHECK(fromStart.contains(manifest.textureIdByName("GroundSplatMap")));

    // From the middle, a crossing either way reaches both ends.
    const RenderAssetRequirements fromMiddle =
        renderAssetRequirementsForOverworldTransitions(map, 3, manifest);
    CHECK(fromMiddle.contains(bricks));
    CHECK(fromMiddle.contains(stone));
    CHECK(fromMiddle.contains(glass));
    CHECK(fromMiddle.contains(turret));
    CHECK(fromMiddle.contains(
        manifest.textureIdByName("GroundSplatMapOverworld1")));

    const RenderAssetRequirements fromEnd =
        renderAssetRequirementsForOverworldTransitions(map, 5, manifest);
    CHECK(!fromEnd.contains(bricks));
    CHECK(fromEnd.contains(stone));
    CHECK(fromEnd.contains(glass));
    CHECK(fromEnd.contains(turret));

    // It is a superset of what the settled frame draws.
    const RenderAssetRequirements settled =
        renderAssetRequirementsForOverworldNeighborhood(map, 1, manifest);
    RenderAssetRequirements merged = fromStart;
    merged.merge(settled);
    CHECK(merged.modelCount() == fromStart.modelCount());
    CHECK(merged.textureCount() == fromStart.textureCount());

    CHECK(renderAssetRequirementsForOverworldTransitions(
        map, 99, manifest).empty());
}

void testOverworldTransitionRequirementsCrossOneSeam()
{
    TEST("overworldTransitionRequirementsCrossOneSeam");
    const auto screen = [](const char* objects) {
        return Level::Definition {
            .layers = {
                { "....", "...." },
                { objects, "    " },
            },
        };
    };
    // A step crosses one seam, so only a screen sharing an edge can become
    // the active one. From the corner of this block that is its two edge
    // neighbours; the diagonal one is drawn but cannot be stepped onto.
    //
    //   1 2 .
    //   3 4 5
    //   . . 6
    const OverworldMap map = OverworldMap::load({}, OverworldDraftOverride {
        .layout = OverworldLayout {
            .format = 3,
            .screenWidth = 4,
            .screenHeight = 2,
            .screens = {
                { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
                { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
                { .id = 3, .file = "screen3.scr", .slot = { 0, 1 } },
                { .id = 4, .file = "screen4.scr", .slot = { 1, 1 } },
                { .id = 5, .file = "screen5.scr", .slot = { 2, 1 } },
                { .id = 6, .file = "screen6.scr", .slot = { 2, 2 } },
            },
        },
        .definitions = {
            { .screen = 1, .definition = screen("C   ") },
            { .screen = 2, .definition = screen("    ") },
            { .screen = 3, .definition = screen("    ") },
            { .screen = 4, .definition = screen(" #  ") },
            { .screen = 5, .definition = screen("  R ") },
            { .screen = 6, .definition = screen("   I") },
        },
    });
    const AssetManifest& manifest = overworldTestManifest();
    const RenderAssetRequirements ahead =
        renderAssetRequirementsForOverworldTransitions(map, 1, manifest);
    // The fourth is drawn from the start; the fifth comes into view from
    // the second, which is one step away.
    CHECK(ahead.contains(manifest.modelIdByName("Bricks")));
    CHECK(ahead.contains(manifest.modelIdByName("Stone")));
    // The sixth is two slots away on both axes. Only the fourth draws it,
    // and reaching the fourth takes two crossings.
    CHECK(!ahead.contains(manifest.modelIdByName("Glass")));
    CHECK(renderAssetRequirementsForOverworldTransitions(map, 4, manifest)
            .contains(manifest.modelIdByName("Glass")));
}

void testLegacyOverworldPrefetchFiltersLockedTargets()
{
    TEST("legacyOverworldPrefetchFiltersLockedTargets");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1 });
    campaign.setOverworldTargets({ { 0, 0 }, { 0, 1 }, { 1, 0 } });
    PlayerProfile profile;
    CHECK(puzzleAssetPrefetchTargets(campaign, profile) ==
        (std::vector<LevelLocation> { { 0, 0 }, { 1, 0 } }));
    profile.recordScreenCompletion({ 0, 0 }, 3, 1.0);
    CHECK(puzzleAssetPrefetchTargets(campaign, profile) ==
        (std::vector<LevelLocation> { { 0, 0 }, { 0, 1 }, { 1, 0 } }));
}

int main()
{
    testLevelRequirementsIncludeDynamicAndStaticAssets();
    testSelectorRequirementsIncludeEveryFlagState();
    testFrameRequirementsOnlyContainReferencedAssets();
    testActionRequirementsKeepFirstUseAssetsWithoutTerrainOrModels();
    testActionRequirementsHonorCatalogAnimationBindings();
    testKnightLevelRequiresKnightInsteadOfRogueModel();
    testMergeDeduplicatesRequirements();
    testCubeAndNoneAreNeverRequirements();
    testGroundSplatTexturesAreRequired();
    testTerrainPbrCompanionsAreRequired();
    testRimWallTextureFallbacksAndPreviewRequirements();
    testPerScreenSplatMapsAreSelectedAndFallBack();
    testPuzzlePrefetchOnlyPlansImmediateSuccessor();
    testOverworldPrefetchUsesOnlySelectableActiveScreenTargets();
    testOverworldNeighborhoodRequirementsStopAtTheVisibleScreens();
    testOverworldTransitionRequirementsReachOneCrossingAhead();
    testOverworldTransitionRequirementsCrossOneSeam();
    testLegacyOverworldPrefetchFiltersLockedTargets();

    if (failures == 0) {
        std::cout << "AssetRequirementsTests: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "AssetRequirementsTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
