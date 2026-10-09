// Headless tests for the tile thumbnail bake's scene and crop.
//
// The bake renders a tile through the real frame path and screenshots it, so
// the part worth pinning down here is the part that decides *what* is on
// screen and *where* the crop lands - the rest is the game's own renderer.

#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/GateEffect.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"
#include "engine/TileThumbnailBake.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

const AssetManifest& testManifest()
{
    static const AssetManifest manifest = AssetManifest::parse(R"json({
      "format": 1,
      "textures": [
        { "name": "ParticleGlow", "path": "glow.png" },
        { "name": "GroundGrass", "path": "grass.png" },
        { "name": "GroundRock", "path": "rock.png" },
        { "name": "GroundRockSide", "path": "rock_side.png" },
        { "name": "GroundSplatMap", "path": "splat.png" }
      ],
      "models": [
        { "name": "Bricks", "path": "bricks.gltf" },
        { "name": "Hero", "path": "h.glb", "geometry": "skinned", "role": "player" },
        { "name": "Lorekeeper", "path": "lorekeeper.glb", "geometry": "skinned" },
        { "name": "Knight", "path": "k.glb", "geometry": "skinned" },
        { "name": "Ladder", "path": "ladder.glb", "preserveSourceScale": true },
        { "name": "PulseButton", "path": "pulse_button.glb", "preserveSourceScale": true },
        { "name": "Druid", "path": "d.glb", "geometry": "skinned" },
        { "name": "Witch", "path": "w.glb", "geometry": "skinned" },
        { "name": "Bard", "path": "b.glb", "geometry": "skinned" },
        { "name": "Wardrobe", "path": "wardrobe.glb", "preserveSourceScale": true },
        { "name": "CliffWallIslandA", "path": "cliff_a.glb", "preserveSourceScale": true },
        { "name": "CliffWallIslandB", "path": "cliff_b.glb", "preserveSourceScale": true }
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
        { "tile": "Cliff Wall", "model": "CliffWallIslandA" },
        { "tile": "Cliff Wall 02", "model": "CliffWallIslandB" },
        { "tile": "Ladder", "model": "Ladder" },
        { "tile": "Button North", "model": "PulseButton" },
        { "tile": "Button East", "model": "PulseButton" },
        { "tile": "Button South", "model": "PulseButton" },
        { "tile": "Button West", "model": "PulseButton" },
        { "tile": "Player", "model": "Hero" },
        { "tile": "Wardrobe Lorekeeper", "model": "Wardrobe" },
        { "tile": "Wardrobe Rogue", "model": "Wardrobe" },
        { "tile": "Wardrobe Knight", "model": "Wardrobe" },
        { "tile": "Wardrobe Druid", "model": "Wardrobe" },
        { "tile": "Wardrobe Witch", "model": "Wardrobe" },
        { "tile": "Wardrobe Bard", "model": "Wardrobe" }
      ]
    })json");
    return manifest;
}

void testAssetPathsAreUniqueAndTidy()
{
    TEST("assetPathsAreUniqueAndTidy");
    std::set<std::string> paths;
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) {
            continue;
        }
        const std::string path = tileThumbnails::assetPathFor(definition.type);
        // Two tiles sharing a path would silently overwrite each other during
        // the bake, and the palette would show one of them twice.
        CHECK(paths.insert(path).second);
        CHECK(path.starts_with("custom/thumbnails/tile_"));
        CHECK(path.ends_with(".png"));
        // Filesystem-safe: no spaces, capitals or punctuation from the display
        // name survive into the file name.
        for (const char character : path) {
            const bool allowed = (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') ||
                character == '_' || character == '/' || character == '.';
            CHECK(allowed);
        }
    }
    CHECK(paths.size() > 10);

    // Names come from the display name, so multi-word tiles read sensibly.
    CHECK(tileThumbnails::assetPathFor(TileType::ConveyorUp) ==
        "custom/thumbnails/tile_conveyor_up.png");
    CHECK(tileThumbnails::assetPathFor(TileType::MirrorNorthWest) ==
        "custom/thumbnails/tile_mirror_north_west.png");
}

void testAirAndWaterAreNotBaked()
{
    TEST("airAndWaterAreNotBaked");
    // Air is the eraser and Water is a layer property, so neither appears in
    // the palette as a drawable brush.
    CHECK(!tileThumbnails::shouldBake(TileType::Air));
    CHECK(!tileThumbnails::shouldBake(TileType::Water));
    CHECK(tileThumbnails::shouldBake(TileType::Wall));
    CHECK(tileThumbnails::shouldBake(TileType::Ground));
    CHECK(tileThumbnails::shouldBake(TileType::Player));
    CHECK(tileThumbnails::shouldBake(TileType::Rogue));
    CHECK(tileThumbnails::shouldBake(TileType::Knight));
    CHECK(tileThumbnails::shouldBake(TileType::Druid));
    CHECK(tileThumbnails::shouldBake(TileType::Witch));
    CHECK(tileThumbnails::shouldBake(TileType::Bard));
}

// The bake takes the game's live settings. Shadows and ambient occlusion are
// off in the RenderFrameData defaults, so these are switched on here to prove
// they reach the frame rather than being silently dropped.
[[nodiscard]] const PresentationSettings& testSettings()
{
    static const PresentationSettings settings = [] {
        PresentationSettings value;
        value.applyTileScales(testManifest());
        value.lighting.shadowsEnabled = true;
        value.lighting.shadowOpacity = 0.6f;
        value.lighting.ambientOcclusionEnabled = true;
        value.lighting.ambientOcclusionStrength = 0.5f;
        value.normalize();
        return value;
    }();
    return settings;
}

constexpr std::array buttonVariants { TileType::ButtonNorth, TileType::ButtonEast,
    TileType::ButtonSouth, TileType::ButtonWest };

void testButtonsRetainAuthoredEdgePlacementAndEnergyMaterials()
{
    TEST("buttonsRetainAuthoredEdgePlacementAndEnergyMaterials");
    const auto& manifest = testManifest();
    const auto model = manifest.modelIdByName("PulseButton");
    CHECK(!model.isCube());
    CHECK(manifest.model(model).preserveSourceScale);
    PresentationSettings settings = testSettings();
    settings.geometry.surfaceEntityWidthDepth = 0.5f;
    settings.geometry.surfaceEntityHeight = 0.08f;
    constexpr GridPosition3 cell { 1, 1, 1 };
    constexpr std::array<Vec2, 4> anchors {
        Vec2 { 1.5f, 1.19f }, Vec2 { 1.81f, 1.5f },
        Vec2 { 1.5f, 1.81f }, Vec2 { 1.19f, 1.5f },
    };
    for (std::size_t index = 0; index < buttonVariants.size(); ++index) {
        const auto button = tileVisual(buttonVariants[index], cell, manifest, settings);
        // Compact dimensions and the pedestal's height live in the model.
        // Scaling it like a pressure plate would move it off its authored edge.
        CHECK(button.model == model);
        CHECK(button.size == Vec2({ 1.0f, 1.0f }));
        CHECK(button.position == Vec2({ 1.0f, 1.0f }));
        CHECK(button.height == 1.0f);
        CHECK(button.modelRotationQuarterTurns == index);
        // PlateEnergy tints the authored emissive face while retaining the
        // housing's neutral materials, including when its pulse is dimmed.
        CHECK(button.effect == RenderSurfaceEffect::PlateEnergy);
        const auto transform = IsoScenePreparer::modelTransformPoints(button);
        const Vec2 xAxis { transform.xPoint.x - transform.origin.x,
            transform.xPoint.y - transform.origin.y };
        const Vec2 yAxis { transform.yPoint.x - transform.origin.x,
            transform.yPoint.y - transform.origin.y };
        const Vec2 anchor { transform.origin.x + xAxis.x * 0.5f + yAxis.x * 0.19f,
            transform.origin.y + xAxis.y * 0.5f + yAxis.y * 0.19f };
        CHECK(std::abs(anchor.x - anchors[index].x) < 0.0001f);
        CHECK(std::abs(anchor.y - anchors[index].y) < 0.0001f);
        // The base model faces along local +y, toward the tile's centre.
        CHECK(yAxis.x * (1.5f - anchor.x) + yAxis.y * (1.5f - anchor.y) > 0.3f);
        const auto baked = tileThumbnails::buildBakeFrame(
            buttonVariants[index], manifest, settings).tiles.back();
        CHECK(baked.model == model);
        CHECK(baked.modelRotationQuarterTurns == index);
        CHECK(baked.size == button.size);
        CHECK(baked.height == button.height);
        CHECK(baked.effect == button.effect);
    }
    CHECK(tileThumbnails::buildBakeFrame(
        TileType::PressurePlate, manifest, settings).tiles.back().effect ==
        RenderSurfaceEffect::PlateEnergy);
    CHECK(tileThumbnails::buildBakeFrame(
        TileType::End, testManifest(), testSettings()).tiles.back().effect ==
        RenderSurfaceEffect::PlateEnergy);
    CHECK(tileThumbnails::assetPathFor(TileType::ButtonNorth) ==
        "custom/thumbnails/tile_button.png");
    CHECK(tileThumbnails::assetPathFor(TileType::ButtonEast) ==
        "custom/thumbnails/tile_button_east.png");
    CHECK(tileThumbnails::assetPathFor(TileType::ButtonSouth) ==
        "custom/thumbnails/tile_button_south.png");
    CHECK(tileThumbnails::assetPathFor(TileType::ButtonWest) ==
        "custom/thumbnails/tile_button_west.png");
}

void testButtonsMatchAcrossGameplayEditorPreviewAndCoveredCells()
{
    TEST("buttonsMatchAcrossGameplayEditorPreviewAndCoveredCells");
    const auto& manifest = testManifest();
    const auto& settings = testSettings();
    const auto model = manifest.modelIdByName("PulseButton");
    constexpr GridPosition3 cell { 1, 1, 1 };
    constexpr GridPosition3 previewCell { 2, 1, 1 };
    constexpr Vec3 linkColor { 0.2f, 0.7f, 1.0f };
    const auto buttonAt = [&](const RenderFrameData& frame, GridPosition3 position,
                              bool preview = false) -> const RenderFrameData::Tile* {
        const auto found = std::ranges::find_if(frame.tiles, [&](const auto& tile) {
            return tile.cell == position && tile.model == model &&
                tile.isEditorPreview == preview;
        });
        CHECK(found != frame.tiles.end());
        return found == frame.tiles.end() ? nullptr : &*found;
    };
    const auto checkGeometry = [&](const RenderFrameData::Tile* tile, TileType variant,
                                   GridPosition3 position) {
        if (tile == nullptr) {
            return;
        }
        const auto expected = tileVisual(variant, position, manifest, settings);
        CHECK(tile->size == expected.size);
        CHECK(tile->position == expected.position);
        CHECK(tile->height == expected.height);
        // Preview models lift slightly to avoid coincident surfaces; their
        // translucency is rendered through the preview flag's dither path.
        const float previewLift = tile->isEditorPreview ? 0.02f : 0.0f;
        CHECK(std::abs(tile->baseElevation - expected.baseElevation - previewLift) < 0.0001f);
        CHECK(tile->modelRotationQuarterTurns == expected.modelRotationQuarterTurns);
        CHECK(tile->effect == RenderSurfaceEffect::PlateEnergy);
    };
    for (const TileType variant : buttonVariants) {
        LevelEditor editor;
        editor.newDocument(4, 3, false);
        editor.setActiveLinkColor(linkColor);
        CHECK(editor.setCell(cell, variant));
        CHECK(editor.setCell({ 3, 1, 1 }, TileType::Gate));
        const auto edited = RenderFrameBuilder::buildEditor({
            .manifest = manifest, .editor = editor, .settings = settings,
        });
        const auto editorButton = buttonAt(edited, cell);
        checkGeometry(editorButton, variant, cell);
        if (editorButton != nullptr) {
            CHECK(editorButton->color == Vec4({ linkColor.x, linkColor.y, linkColor.z, 1.0f }));
        }
        const auto preview = RenderFrameBuilder::buildEditor({
            .manifest = manifest, .editor = editor, .settings = settings,
            .hoverCell = previewCell, .editorPreviewTile = variant,
        });
        const auto ghost = buttonAt(preview, previewCell, true);
        checkGeometry(ghost, variant, previewCell);
        if (ghost != nullptr) {
            CHECK(ghost->color == tileColor(variant));
            CHECK(ghost->isEditorPreview && !ghost->pickOnly);
        }
        for (const bool covered : { false, true }) {
            if (covered) {
                CHECK(editor.setCell(cell, TileType::Rock));
                CHECK(editor.documentPlateAt(cell) == variant);
                const auto coveredEditor = RenderFrameBuilder::buildEditor({
                    .manifest = manifest, .editor = editor, .settings = settings,
                });
                const auto coveredButton = buttonAt(coveredEditor, cell);
                checkGeometry(coveredButton, variant, cell);
                CHECK(coveredButton != nullptr && !coveredButton->pickable);
            }
            const auto level = editor.documentToLevel();
            auto state = rules::initialState(level);
            GameplayPresentation presentation;
            presentation.resetEntities(state);
            for (const bool pulsing : { false, true }) {
                state.activeButtons = pulsing ? std::vector<GridPosition3> { cell }
                                              : std::vector<GridPosition3> {};
                const auto gameplay = RenderFrameBuilder::buildGameplay({
                    .manifest = manifest, .level = level, .state = state,
                    .projectedState = state, .presentation = presentation,
                    .settings = settings,
                });
                const auto gameplayButton = buttonAt(gameplay, cell);
                checkGeometry(gameplayButton, variant, cell);
                if (gameplayButton != nullptr) {
                    const float strength = pulsing ? 1.0f : 0.42f;
                    CHECK(std::abs(gameplayButton->color.x - linkColor.x * strength) < 0.001f);
                    CHECK(std::abs(gameplayButton->color.y - linkColor.y * strength) < 0.001f);
                    CHECK(std::abs(gameplayButton->color.z - linkColor.z * strength) < 0.001f);
                }
            }
        }
    }
}

void testBakeFrameStandsTheTileOnAGroundBed()
{
    TEST("bakeFrameStandsTheTileOnAGroundBed");
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) {
            continue;
        }
        const RenderFrameData frame = tileThumbnails::buildBakeFrame(
            definition.type, testManifest(), testSettings());
        CHECK(frame.viewMode == RenderViewMode::Isometric3D);

        // A bed of neutral ground plus the subject. Ground replaces its centre
        // cell rather than stacking on it, so it is one fewer.
        const std::size_t bedCells =
            tileThumbnails::bedSize * tileThumbnails::bedSize;
        const std::size_t expected = tileTypeIsGround(definition.type)
            ? bedCells
            : bedCells + (definition.type == TileType::Gate
                          ? config::gateEnergyTileCount
                          : tileTypeIsPortal(definition.type) ? 5
                          : definition.type == TileType::MinecartGate ? 4
                          : tileTypeIsWardrobe(definition.type) ? 2 : 1);
        CHECK(frame.tiles.size() == expected);

        // Every tile counts toward the camera fit. This is what makes the
        // camera identical for every thumbnail: fitting to the subject alone
        // framed a flat tile and a tall one at completely different scales.
        std::size_t nonFittingTiles = 0;
        for (const RenderFrameData::Tile& tile : frame.tiles) {
            nonFittingTiles += tile.affectsCameraFit ? 0U : 1U;
            CHECK(!tile.showGrid);
            CHECK(!tile.isEditorPreview);
        }
        CHECK(nonFittingTiles ==
            (tileTypeIsWardrobe(definition.type) ? 1U : 0U));

        // Lighting is carried through, or the capture would have no shadows
        // and no ambient occlusion - the whole reason for the bed.
        CHECK(frame.lighting.shadows.enabled);
        CHECK(frame.lighting.ambientOcclusion.enabled);

        if (definition.type == TileType::Gate) {
            // A gate is a composite effect, not one model at the cell centre.
            continue;
        }
        if (definition.type == TileType::MinecartGate) {
            CHECK(frame.isoFaces.size() == 36);
            for (const auto& face : frame.isoFaces) {
                for (Vec3 point : face.vertices) {
                    CHECK(point.z >= 0.0f && point.z < 1.0f);
                }
            }
            continue;
        }
        // The subject is the last tile and is centred on the centre cell. Its
        // footprint is checked by its midpoint rather than its corner because
        // a manifest tile scale above 1 legitimately overhangs the cell.
        const RenderFrameData::Tile& subject = frame.tiles.back();
        const auto centre = static_cast<float>(tileThumbnails::bedCentre);
        const float midX = subject.position.x + subject.size.x * 0.5f;
        const float midY = subject.position.y + subject.size.y * 0.5f;
        const auto edge = portalEdgeOffset(definition.type);
        CHECK(std::abs(midX - (centre + 0.5f + edge.x * 0.5f)) < 0.001f);
        const float ladderOffset = definition.type == TileType::Ladder ? 0.38f : 0.0f;
        CHECK(std::abs(midY - (centre + 0.5f + edge.y * 0.5f + ladderOffset)) < 0.001f);
        // Standing on the bed's surface, not sunk into or floating above it.
        CHECK(
            std::abs(
                subject.baseElevation -
                (tileTypeIsPortal(definition.type) ? 0.03f : 0.0f)) < 0.001f);
    }
}

void testBedIsNeutralAndFlat()
{
    TEST("bedIsNeutralAndFlat");
    const RenderFrameData frame = tileThumbnails::buildBakeFrame(
        TileType::Wall, testManifest(), testSettings());
    const std::size_t bedCells =
        tileThumbnails::bedSize * tileThumbnails::bedSize;
    for (std::size_t i = 0; i < bedCells; ++i) {
        const RenderFrameData::Tile& cell = frame.tiles[i];
        CHECK(cell.height == 0.0f);
        // Plain cubes, not Ground: a screen's splat map must not be able to
        // change what the thumbnails look like.
        CHECK(cell.effect == RenderSurfaceEffect::Standard);
        CHECK(cell.model.isCube());
        CHECK(cell.color.x == tileThumbnails::bedColor.x);
    }
}

void testGateBakesTheClosedEnergyEffect()
{
    TEST("gateBakesTheClosedEnergyEffect");
    const auto frame = tileThumbnails::buildBakeFrame(
        TileType::Gate, testManifest(), testSettings());
    CHECK(frame.particles.size() == config::gateCornerParticleCount);
    const auto center = static_cast<int>(tileThumbnails::bedCentre);
    RenderFrameData closedGate;
    appendGateEffect(closedGate, Level::Gate { .cell = { center, center, 0 } },
        testManifest(), 1.0f, 0.0f);
    const std::size_t bedCells = tileThumbnails::bedSize * tileThumbnails::bedSize;
    CHECK(frame.tiles.size() == bedCells + closedGate.tiles.size());
    for (std::size_t index = 0; index < closedGate.tiles.size(); ++index) {
        const auto& part = frame.tiles[bedCells + index];
        const auto& expected = closedGate.tiles[index];
        CHECK(part.effect == RenderSurfaceEffect::GateEnergy);
        CHECK(!part.pickOnly);
        CHECK(part.height == expected.height);
        CHECK(part.baseElevation == expected.baseElevation);
        CHECK(part.color.w == expected.color.w);
        CHECK(part.color.w > 0.0f);
        CHECK(part.baseElevation >= 0.0f);
        CHECK(part.baseElevation + part.height <= 1.0f);
    }
    for (const auto& particle : frame.particles) {
        CHECK(particle.texture == testManifest().findTextureIdByName(
            config::gateParticleTextureName));
        CHECK(particle.color.w > 0.0f);
        CHECK(particle.position.z > 0.0f && particle.position.z < 1.0f);
    }
    PreparedRenderScene scene;
    IsoScenePreparer {}.prepare(frame, { 800, 600 }, scene);
    CHECK(!scene.particles.empty());
    CHECK(std::ranges::any_of(scene.isoFaces, [](const auto& face) {
        return face.material == PreparedSurfaceMaterial::GateEnergy;
    }));
}

void testGroundIsBakedThroughTheSplatPath()
{
    TEST("groundIsBakedThroughTheSplatPath");
    // Ground's look comes from the splat shader rather than a model, so the
    // bake has to request that path and supply the textures - otherwise the
    // thumbnail would be a flat untextured square.
    const RenderFrameData ground =
        tileThumbnails::buildBakeFrame(
            TileType::Ground, testManifest(), testSettings());
    CHECK(ground.tiles.back().effect == RenderSurfaceEffect::GroundSplat);
    CHECK(ground.groundSplat.valid());
    CHECK(ground.groundRockSideTexture == testManifest().textureIdByName(groundRockSideTextureName));

    const RenderFrameData wall =
        tileThumbnails::buildBakeFrame(
            TileType::Wall, testManifest(), testSettings());
    CHECK(wall.tiles.back().effect == RenderSurfaceEffect::Standard);
    CHECK(!wall.tiles.back().model.isCube());
    for (const auto type : { TileType::CliffWall, TileType::CliffWall02 }) {
        const auto cliff = tileThumbnails::buildBakeFrame(type, testManifest(), testSettings());
        CHECK(cliff.tiles.back().model == testManifest().modelForTile(type));
        CHECK(cliff.tiles.back().effect == RenderSurfaceEffect::GroundSplat);
        CHECK(cliff.tiles.back().groundTop);
        CHECK(cliff.groundSplat.valid());
        CHECK(cliff.groundRockSideTexture == testManifest().textureIdByName(groundRockSideTextureName));
        CHECK(cliff.tiles.size() == tileThumbnails::bedSize * tileThumbnails::bedSize + 1);
    }
}

void testLecternsBakeAllCardinalDirections()
{
    TEST("lecternsBakeAllCardinalDirections");
    constexpr std::array variants { TileType::LecternNorth, TileType::LecternEast,
        TileType::LecternSouth, TileType::LecternWest };
    constexpr std::array<uint32_t, 4> turns { 2, 3, 0, 1 };
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto frame = tileThumbnails::buildBakeFrame(
            variants[i], testManifest(), testSettings());
        CHECK(frame.tiles.back().modelRotationQuarterTurns == turns[i]);
        CHECK(frame.tiles.back().height > 0.0f);
    }
    CHECK(tileThumbnails::assetPathFor(TileType::LecternSouth) ==
        "custom/thumbnails/tile_lectern.png");
}

void testMirrorsBakeAtTheirOwnOrientation()
{
    TEST("mirrorsBakeAtTheirOwnOrientation");
    // The four mirrors share one model and differ only by rotation, so
    // baking them all unrotated would produce four identical pictures.
    std::set<uint32_t> turns;
    for (const TileType mirror : {
             TileType::MirrorNorthWest, TileType::MirrorNorthEast,
             TileType::MirrorSouthWest, TileType::MirrorSouthEast }) {
        const RenderFrameData frame =
            tileThumbnails::buildBakeFrame(
                mirror, testManifest(), testSettings());
        turns.insert(frame.tiles.back().modelRotationQuarterTurns);
        CHECK(frame.tiles.back().modelRotationOffsetRadians != 0.0f);
    }
    CHECK(turns.size() == 4);
}

void testConveyorsBakeRotatedAndAtBeltHeight()
{
    TEST("conveyorsBakeRotatedAndAtBeltHeight");
    // Regression: the bake used to re-derive the tile's height and rotation
    // and handled only mirrors, so all four conveyors baked identically - and
    // flat, because a conveyor is neither a surface entity nor a solid block.
    std::set<uint32_t> turns;
    for (const TileType conveyor : {
             TileType::ConveyorUp, TileType::ConveyorDown,
             TileType::ConveyorLeft, TileType::ConveyorRight }) {
        const RenderFrameData frame = tileThumbnails::buildBakeFrame(
            conveyor, testManifest(), testSettings());
        const RenderFrameData::Tile& subject = frame.tiles.back();
        turns.insert(subject.modelRotationQuarterTurns);
        // A belt, not a floor decal and not a full cube.
        CHECK(subject.height > 0.0f);
        CHECK(subject.height < 1.0f);
    }
    CHECK(turns.size() == 4);
}

void testSubjectMatchesTheTileTheEditorDraws()
{
    TEST("subjectMatchesTheTileTheEditorDraws");
    // The bake and the editor must agree on what a tile looks like, since the
    // palette icon is meant to be a picture of the tile the editor will place.
    // Both go through tileVisual, so this pins the bake to it and would catch
    // the bake growing its own copy of the rules again.
    for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
        if (!tileThumbnails::shouldBake(definition.type)) {
            continue;
        }
        const RenderFrameData frame = tileThumbnails::buildBakeFrame(
            definition.type, testManifest(), testSettings());
        const RenderFrameData::Tile& subject = frame.tiles.back();
        const RenderFrameData::Tile expected = tileVisual(
            definition.type,
            {
                static_cast<int>(tileThumbnails::bedCentre),
                static_cast<int>(tileThumbnails::bedCentre),
                tileTypeIsGround(definition.type) ? 0 : 1,
            },
            testManifest(),
            testSettings());
        if (tileTypeIsPortal(definition.type)) {
            // Composite edge entrances have an upright frame and a dark back.
            CHECK(frame.tiles.size() >= 5);
            CHECK(subject.height > 0.8f);
            CHECK(frame.particles.size() == 112);
            CHECK(subject.color.x < expected.color.x);
            const GridPosition edge = portalEdgeOffset(definition.type);
            const float edgeX = subject.cell.x + 0.5f + edge.x * 0.5f;
            const float edgeY = subject.cell.y + 0.5f + edge.y * 0.5f;
            for (const auto& particle : frame.particles) {
                CHECK((particle.position.x - edgeX) * edge.x +
                    (particle.position.y - edgeY) * edge.y < 0.0f);
            }
            continue;
        }
        if (definition.type == TileType::Gate) {
            CHECK(subject.effect == RenderSurfaceEffect::GateEnergy);
            continue;
        }
        if (definition.type == TileType::MinecartGate) {
            CHECK(frame.isoFaces.size() == 36);
            CHECK(frame.tiles.size() == tileThumbnails::bedSize * tileThumbnails::bedSize + 4);
            continue;
        }
        CHECK(subject.height == expected.height);
        CHECK(subject.size.x == expected.size.x);
        CHECK(subject.size.y == expected.size.y);
        CHECK(subject.position.x == expected.position.x);
        CHECK(std::abs(subject.position.y - expected.position.y -
            (definition.type == TileType::Ladder ? 0.38f : 0.0f)) < 0.0001f);
        CHECK(subject.modelRotationQuarterTurns ==
            expected.modelRotationQuarterTurns);
        CHECK(subject.modelRotationOffsetRadians ==
            expected.modelRotationOffsetRadians);
        CHECK(subject.effect == expected.effect);
        CHECK(subject.color.x == expected.color.x);
        CHECK(subject.color.w == expected.color.w);
    }
}

void testPerspectiveIsNoStrongerThanOnARealBoard()
{
    TEST("perspectiveIsNoStrongerThanOnARealBoard");
    // Camera distance follows the size of the framed area, so fitting to a 3x3
    // bed put the camera very close and a tile's vertical edges visibly
    // splayed. This measures that splay directly rather than asserting the
    // multiplier's value: a vertical world edge is exactly vertical on screen
    // under an orthographic iso view, so any horizontal drift between a
    // corner's base and its top is the perspective divergence.
    constexpr uint32_t width = 1280;
    constexpr uint32_t height = 720;

    const auto leanFraction = [](const RenderFrameData& frame) {
        PreparedRenderScene scene;
        const IsoScenePreparer preparer;
        preparer.prepare(frame,
            { static_cast<float>(width), static_cast<float>(height) }, scene);
        const auto pixelX = [&](Vec3 point) {
            return (IsoScenePreparer::projectIsoPoint(
                        scene.isoLayout, scene.renderExtent, point)
                           .x +
                       1.0f) *
                0.5f * static_cast<float>(width);
        };
        const auto centre = static_cast<float>(tileThumbnails::bedCentre);
        float lean = 0.0f;
        for (const float x : { centre, centre + 1.0f }) {
            for (const float y : { centre, centre + 1.0f }) {
                lean = std::max(lean,
                    std::abs(pixelX({ x, y, 1.0f }) - pixelX({ x, y, 0.0f })));
            }
        }
        const tileThumbnails::CropRect crop =
            tileThumbnails::cropFor(frame, width, height);
        return lean / static_cast<float>(std::max(crop.width, 1u));
    };

    const RenderFrameData frame = tileThumbnails::buildBakeFrame(
        TileType::Wall, testManifest(), testSettings());
    // A 9-wide board - a small level - produces about 1.2%, so this is the
    // loosest bound that still means "no worse than the game".
    CHECK(leanFraction(frame) < 0.012f);

    // The bed alone, with the ordinary fitted distance, is well past that.
    // Without this the bound above could be met by accident.
    RenderFrameData close = frame;
    close.cameraDistanceMultiplier.reset();
    CHECK(leanFraction(close) > 0.03f);

    // Pulling the camera back must not shrink the subject: the fit rescales to
    // compensate, which is what makes this a lens choice and not a zoom.
    const tileThumbnails::CropRect crop =
        tileThumbnails::cropFor(frame, width, height);
    const tileThumbnails::CropRect closeCrop =
        tileThumbnails::cropFor(close, width, height);
    CHECK(crop.width >= closeCrop.width);
}

void testCropFramesTheSubjectCell()
{
    TEST("cropFramesTheSubjectCell");
    const std::pair<uint32_t, uint32_t> extents[] = {
        { 1280, 720 }, { 720, 1280 }, { 800, 800 }, { 1920, 1080 }, { 64, 64 },
    };
    const RenderFrameData frame = tileThumbnails::buildBakeFrame(
        TileType::Wall, testManifest(), testSettings());
    for (const auto& [width, height] : extents) {
        const tileThumbnails::CropRect crop =
            tileThumbnails::cropFor(frame, width, height);
        CHECK(crop.width > 0);
        CHECK(crop.height > 0);
        // Square, so the saved thumbnail is not stretched by the window's
        // aspect ratio.
        CHECK(crop.width == crop.height);
        // Entirely inside the render extent, or the capture would read
        // outside the image.
        CHECK(crop.x >= 0);
        CHECK(crop.y >= 0);
        CHECK(static_cast<uint32_t>(crop.x) + crop.width <= width);
        CHECK(static_cast<uint32_t>(crop.y) + crop.height <= height);
        // The crop must actually contain the subject cell's projection, which
        // is the point of deriving it rather than guessing a fraction.
        PreparedRenderScene scene;
        const IsoScenePreparer preparer;
        preparer.prepare(
            frame,
            { static_cast<float>(width), static_cast<float>(height) },
            scene);
        const auto centre = static_cast<float>(tileThumbnails::bedCentre);
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(
            scene.isoLayout, scene.renderExtent,
            { centre + 0.5f, centre + 0.5f, 0.0f });
        const float pixelX = (clip.x + 1.0f) * 0.5f * static_cast<float>(width);
        const float pixelY =
            (1.0f - clip.y) * 0.5f * static_cast<float>(height);
        CHECK(pixelX >= static_cast<float>(crop.x));
        CHECK(pixelX <= static_cast<float>(crop.x + static_cast<int32_t>(crop.width)));
        CHECK(pixelY >= static_cast<float>(crop.y));
        CHECK(pixelY <= static_cast<float>(crop.y + static_cast<int32_t>(crop.height)));

        // Smaller than the whole extent, or the bed's outer cells would fill
        // the picture and the subject would be a speck.
        CHECK(crop.width < std::min(width, height) ||
            std::min(width, height) <= 64);
    }

    // Degenerate extents must not produce a zero or negative rectangle.
    const tileThumbnails::CropRect tiny = tileThumbnails::cropFor(frame, 1, 1);
    CHECK(tiny.width >= 1);
    CHECK(tiny.height >= 1);
    CHECK(tiny.x == 0);
    CHECK(tiny.y == 0);
}

} // namespace

int main()
{
    testAssetPathsAreUniqueAndTidy();
    testAirAndWaterAreNotBaked();
    testButtonsRetainAuthoredEdgePlacementAndEnergyMaterials();
    testButtonsMatchAcrossGameplayEditorPreviewAndCoveredCells();
    testBakeFrameStandsTheTileOnAGroundBed();
    testBedIsNeutralAndFlat();
    testGateBakesTheClosedEnergyEffect();
    testGroundIsBakedThroughTheSplatPath();
    testMirrorsBakeAtTheirOwnOrientation();
    testLecternsBakeAllCardinalDirections();
    testConveyorsBakeRotatedAndAtBeltHeight();
    testSubjectMatchesTheTileTheEditorDraws();
    testPerspectiveIsNoStrongerThanOnARealBoard();
    testCropFramesTheSubjectCell();

    if (failures == 0) {
        std::cout << "TileThumbnailBakeTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "TileThumbnailBakeTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
