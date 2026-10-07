#include "TestHarness.hpp"

#include "engine/GroundGeometry.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

namespace {

using namespace sokoban;

const AssetManifest& manifest()
{
    static const AssetManifest result = AssetManifest::parse(R"json({
        "format": 1,
        "models": [
            { "name": "GroundRock01", "path": "custom/pbr/models/GroundRock01.glb",
              "preserveSourceScale": true },
            { "name": "GroundRock02", "path": "custom/pbr/models/GroundRock02.glb",
              "preserveSourceScale": true },
            { "name": "Custom", "path": "custom/other_ground.glb",
              "preserveSourceScale": true },
            { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }
        ],
        "animations": [
            { "name": "Idle", "path": "idle.glb", "role": "player-idle" },
            { "name": "Move", "path": "move.glb", "role": "player-move" },
            { "name": "Push", "path": "push.glb", "role": "player-push" },
            { "name": "Death", "path": "death.glb", "role": "player-death" },
            { "name": "DeadIdle", "path": "dead_idle.glb", "role": "player-dead-idle" }
        ],
        "tiles": [
            { "tile": "Ground", "model": "GroundRock01" },
            { "tile": "Ground Rock 02", "model": "GroundRock02" }
        ]
    })json");
    return result;
}

RenderFrameData::Tile ground(GridPosition3 cell, RenderModel model = { 1 })
{
    return {
        .cell = cell,
        .position = { static_cast<float>(cell.x), static_cast<float>(cell.y) },
        .color = { 1, 1, 1, 1 },
        .baseElevation = static_cast<float>(cell.z),
        .height = 1,
        .model = model,
        .effect = RenderSurfaceEffect::GroundSplat,
        .groundTop = true,
    };
}

void testAdjacencyAndHoles()
{
    TEST("adjacencyAndHoles");
    std::vector tiles {
        ground({ 0, 0, 0 }),
        ground({ 0, -1, 0 }, { 2 }), ground({ 1, 0, 0 }),
        ground({ 0, 1, 0 }), ground({ -1, 0, 0 }),
        ground({ 4, 4, 0 }),
    };
    processGroundGeometry(tiles, manifest());
    CHECK(tiles[0].groundSideMask == 0);
    CHECK(tiles[1].groundSideMask == (groundAllSides & ~groundSouthSide));
    CHECK(tiles[2].groundSideMask == (groundAllSides & ~groundWestSide));
    CHECK(tiles[3].groundSideMask == (groundAllSides & ~groundNorthSide));
    CHECK(tiles[4].groundSideMask == (groundAllSides & ~groundEastSide));
    CHECK(tiles[5].groundSideMask == groundAllSides);
    CHECK((tiles[0].groundSideNeighbors == std::array<RenderModel, 4> {
        RenderModel { 2 }, RenderModel { 1 }, RenderModel { 1 }, RenderModel { 1 },
    }));
    CHECK(tiles[1].groundSideNeighbors[2] == RenderModel { 1 });
    CHECK(tiles[1].groundSideNeighbors[0].isCube());
    CHECK((tiles[5].groundSideNeighbors == std::array<RenderModel, 4> {}));
    CHECK(tiles[0].groundTop);
    CHECK(tiles[0].cell == GridPosition3({ 0, 0, 0 }));
    CHECK(tiles[0].height == 1);

    tiles.erase(tiles.begin() + 2);
    processGroundGeometry(tiles, manifest());
    CHECK(tiles[0].groundSideMask == groundEastSide);
    CHECK(tiles[0].groundSideNeighbors[1].isCube());
    // A repeated build must restore previously hidden sides when a neighbour
    // is deleted, without retaining state from the previous authored version.
    tiles.erase(tiles.begin() + 1, tiles.end());
    processGroundGeometry(tiles, manifest());
    CHECK(tiles[0].groundSideMask == groundAllSides);
    CHECK((tiles[0].groundSideNeighbors == std::array<RenderModel, 4> {}));
}

void testLayersAndNegativeOrigins()
{
    TEST("layersAndNegativeOrigins");
    std::vector tiles {
        ground({ -2, -5, 1 }), ground({ -1, -5, 1 }),
        ground({ -2, -6, 2 }), ground({ -2, -5, 0 }),
    };
    processGroundGeometry(tiles, manifest());
    CHECK(tiles[0].groundSideMask == (groundAllSides & ~groundEastSide));
    CHECK(tiles[1].groundSideMask == (groundAllSides & ~groundWestSide));
    CHECK(tiles[2].groundSideMask == groundAllSides);
    CHECK(tiles[3].groundSideMask == groundAllSides);
}

void testUnsupportedTilesNeverHideGround()
{
    TEST("unsupportedTilesNeverHideGround");
    const auto checkExcluded = [](RenderFrameData::Tile excluded) {
        std::array tiles { ground({ 0, 0, 0 }), excluded };
        processGroundGeometry(tiles, manifest());
        CHECK(tiles[0].groundSideMask == groundAllSides);
        CHECK(tiles[1].groundSideMask == groundAllSides);
        CHECK((tiles[0].groundSideNeighbors == std::array<RenderModel, 4> {}));
        CHECK((tiles[1].groundSideNeighbors == std::array<RenderModel, 4> {}));
    };
    auto excluded = ground({ 1, 0, 0 });
    excluded.groundTop = false; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.isEditorPreview = true; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.pickOnly = true; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.color.w = 0.99f; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.size = { 0.9f, 0.9f }; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.height = 0.9f; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.position.x += 0.1f; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.baseElevation += 0.1f; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.modelRotationQuarterTurns = 1; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.modelRotationOffsetRadians = 0.1f; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.modelTransform = RenderFrameData::ModelTransform {}; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.animation = { 1 }; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.renderableId = 42; checkExcluded(excluded);
    excluded = ground({ 1, 0, 0 });
    excluded.effect = RenderSurfaceEffect::MirrorEnergy; checkExcluded(excluded);
    checkExcluded(ground({ 1, 0, 0 }, cubeModel));
    checkExcluded(ground({ 1, 0, 0 }, { 3 }));
    checkExcluded(ground({ 1, 0, 0 }, { 9999 }));

    // The editor's material assignment visualization replaces the splat
    // effect with an opaque color, while preserving the ground identity.
    std::array colored { ground({ 0, 0, 0 }), ground({ 1, 0, 0 }) };
    colored[1].effect = RenderSurfaceEffect::Standard;
    processGroundGeometry(colored, manifest());
    CHECK(colored[0].groundSideMask == (groundAllSides & ~groundEastSide));
}

void testArenaAndOwningPathsAgree()
{
    TEST("arenaAndOwningPathsAgree");
    std::vector<RenderFrameData::Tile> tiles;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) tiles.push_back(ground({ x, y, 0 }));
    }
    auto arenaTiles = tiles;
    processGroundGeometry(tiles, manifest());
    FrameArena arena("ground geometry test", 16384);
    processGroundGeometry(arenaTiles, manifest(), &arena);
    CHECK(arenaTiles == tiles);
    CHECK(arena.bytesUsed() == 8192);
    CHECK(!arena.exhausted());
    arena.reset();
    processGroundGeometry(arenaTiles, manifest(), &arena);
    CHECK(arenaTiles == tiles);
    CHECK(arena.bytesUsed() == 8192);

    FrameArena tinyArena("ground geometry fail-open test", 1);
    processGroundGeometry(arenaTiles, manifest(), &tinyArena);
    CHECK(tinyArena.exhausted());
    CHECK(std::ranges::all_of(arenaTiles, [](const auto& tile) {
        return tile.groundSideMask == groundAllSides &&
            std::ranges::all_of(tile.groundSideNeighbors, [](RenderModel model) {
                return model.isCube();
            });
    }));
}

const RenderFrameData::Tile* groundAt(const RenderFrameData& frame, GridPosition3 cell)
{
    const auto found = std::ranges::find_if(frame.tiles, [&](const auto& tile) {
        return tile.cell == cell && tile.groundTop && !tile.isEditorPreview && !tile.pickOnly;
    });
    return found == frame.tiles.end() ? nullptr : &*found;
}

void testGameplayVisibilityAndToggle()
{
    TEST("gameplayVisibilityAndToggle");
    const Level level = Level::loadFromLayers({ { "..." }, { "C  " } }, "ground experiment");
    const GameState state = rules::initialState(level);
    GameplayPresentation presentation;
    presentation.resetEntities(state);
    PresentationSettings settings;
    RenderFrameBuilder::GameplayInput input {
        .manifest = manifest(), .level = level, .state = state,
        .projectedState = state, .presentation = presentation, .settings = settings,
        .visibleCell = [](GridPosition3 cell) { return cell.x < 2; },
    };
    const auto visible = RenderFrameBuilder::buildGameplay(input);
    const auto* edge = groundAt(visible, { 1, 0, 0 });
    CHECK(edge);
    CHECK(edge && edge->groundSideMask == (groundAllSides & ~groundWestSide));
    CHECK(!groundAt(visible, { 2, 0, 0 }));
    FrameArena arena("ground gameplay test", renderFrameArenaBytes());
    const auto live = RenderFrameBuilder::buildGameplay(input, arena);
    CHECK(live.tiles.size() == visible.tiles.size());
    CHECK(std::ranges::equal(live.tiles, visible.tiles));
    CHECK(!arena.exhausted());

    input.visibleCell = {};
    const auto complete = RenderFrameBuilder::buildGameplay(input);
    const auto* interior = groundAt(complete, { 1, 0, 0 });
    CHECK(interior && interior->groundSideMask == (groundNorthSide | groundSouthSide));
    settings.geometry.processGroundGeometry = false;
    const auto unprocessed = RenderFrameBuilder::buildGameplay(input);
    const auto* unprocessedTile = groundAt(unprocessed, { 1, 0, 0 });
    CHECK(unprocessedTile && unprocessedTile->groundSideMask == groundAllSides);
    CHECK(level.tileAt(1, 0, 0) == TileType::Ground);
}

void testEditorNeighborsAndPreviews()
{
    TEST("editorNeighborsAndPreviews");
    LevelEditor editor;
    editor.newDocument(2, 1, false);
    editor.setActiveLayer(0);
    editor.setSelectedTile(TileType::Ground);
    const Level::Definition neighborDefinition { .layers = { { "." }, { " " } } };
    const std::array neighbors {
        RenderFrameBuilder::EditorInput::OverworldNeighbor {
            .screen = 9, .origin = { 2, 0 }, .width = 1, .height = 1,
            .definition = &neighborDefinition,
        },
    };
    PresentationSettings settings;
    const auto frame = RenderFrameBuilder::buildEditor({
        .manifest = manifest(), .editor = editor, .settings = settings,
        .hoverCell = GridPosition3 { -1, 0, 0 }, .overworldNeighbors = neighbors,
    });
    const auto* interior = groundAt(frame, { 1, 0, 0 });
    const auto* neighbor = groundAt(frame, { 2, 0, 0 });
    CHECK(interior && interior->groundSideMask == (groundNorthSide | groundSouthSide));
    CHECK(neighbor && neighbor->groundSideMask == (groundAllSides & ~groundWestSide));
    const auto* first = groundAt(frame, { 0, 0, 0 });
    CHECK(first && first->groundSideMask == (groundAllSides & ~groundEastSide));
    const auto preview = std::ranges::find_if(frame.tiles, [](const auto& tile) {
        return tile.isEditorPreview && tile.groundTop;
    });
    CHECK(preview != frame.tiles.end());
    CHECK(preview != frame.tiles.end() && preview->groundSideMask == groundAllSides);
    CHECK(tileVisual(TileType::Ground, { 0, 0, 0 }, manifest(), settings).groundSideMask == groundAllSides);

    CHECK(editor.setCell({ 1, 0, 0 }, TileType::Air));
    const auto edited = RenderFrameBuilder::buildEditor({
        .manifest = manifest(), .editor = editor, .settings = settings,
        .overworldNeighbors = neighbors,
    });
    const auto* editedFirst = groundAt(edited, { 0, 0, 0 });
    const auto* editedNeighbor = groundAt(edited, { 2, 0, 0 });
    CHECK(editedFirst && editedFirst->groundSideMask == groundAllSides);
    CHECK(editedNeighbor && editedNeighbor->groundSideMask == groundAllSides);
}

} // namespace

int main()
{
    try {
        testAdjacencyAndHoles();
        testLayersAndNegativeOrigins();
        testUnsupportedTilesNeverHideGround();
        testArenaAndOwningPathsAgree();
        testGameplayVisibilityAndToggle();
        testEditorNeighborsAndPreviews();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }
    std::cout << "GroundGeometryTests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
