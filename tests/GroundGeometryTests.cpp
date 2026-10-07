#include "TestHarness.hpp"

#include "engine/GroundGeometry.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/Rules.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
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
    CHECK(settings.geometry.smoothGroundRim);
    GroundGeometryCache cache;
    RenderFrameBuilder::GameplayInput input {
        .manifest = manifest(), .level = level, .state = state,
        .projectedState = state, .presentation = presentation, .settings = settings,
        .visibleCell = [](GridPosition3 cell) { return cell.x < 2; },
        .groundGeometryCache = &cache,
    };
    const auto visible = RenderFrameBuilder::buildGameplay(input);
    const auto* edge = groundAt(visible, { 1, 0, 0 });
    CHECK(edge);
    CHECK(edge && edge->groundSideMask == (groundAllSides & ~groundWestSide));
    CHECK(edge && edge->groundGeometryEligible);
    CHECK(edge && edge->groundRimWidth == settings.geometry.groundRimWidth);
    CHECK(edge && edge->groundRimDepth == settings.geometry.groundRimDepth);
    CHECK(cache.rebuildCount() == 1);
    CHECK(!groundAt(visible, { 2, 0, 0 }));
    FrameArena arena("ground gameplay test", renderFrameArenaBytes());
    const auto live = RenderFrameBuilder::buildGameplay(input, arena);
    CHECK(live.tiles.size() == visible.tiles.size());
    CHECK(std::ranges::equal(live.tiles, visible.tiles));
    CHECK(!arena.exhausted());
    CHECK(cache.hitCount() == 1);

    input.visibleCell = {};
    const auto complete = RenderFrameBuilder::buildGameplay(input);
    const auto* interior = groundAt(complete, { 1, 0, 0 });
    CHECK(interior && interior->groundSideMask == (groundNorthSide | groundSouthSide));
    CHECK(cache.rebuildCount() == 2);
    settings.geometry.processGroundGeometry = false;
    const auto unprocessed = RenderFrameBuilder::buildGameplay(input);
    const auto* unprocessedTile = groundAt(unprocessed, { 1, 0, 0 });
    CHECK(unprocessedTile && unprocessedTile->groundSideMask == groundAllSides);
    CHECK(unprocessedTile && unprocessedTile->groundRimWidth == 0);
    CHECK(cache.rebuildCount() == 2);
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
    settings.geometry.smoothGroundRim = true;
    GroundGeometryCache cache;
    const auto frame = RenderFrameBuilder::buildEditor({
        .manifest = manifest(), .editor = editor, .settings = settings,
        .hoverCell = GridPosition3 { -1, 0, 0 }, .overworldNeighbors = neighbors,
        .groundGeometryCache = &cache,
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
    CHECK(preview != frame.tiles.end() && !preview->groundGeometryEligible);
    CHECK(preview != frame.tiles.end() && preview->groundRimWidth == 0);
    CHECK(cache.rebuildCount() == 1);
    CHECK(tileVisual(TileType::Ground, { 0, 0, 0 }, manifest(), settings).groundSideMask == groundAllSides);

    CHECK(editor.setCell({ 1, 0, 0 }, TileType::Air));
    const auto edited = RenderFrameBuilder::buildEditor({
        .manifest = manifest(), .editor = editor, .settings = settings,
        .overworldNeighbors = neighbors,
        .groundGeometryCache = &cache,
    });
    const auto* editedFirst = groundAt(edited, { 0, 0, 0 });
    const auto* editedNeighbor = groundAt(edited, { 2, 0, 0 });
    CHECK(editedFirst && editedFirst->groundSideMask == groundAllSides);
    CHECK(editedNeighbor && editedNeighbor->groundSideMask == groundAllSides);
    CHECK(cache.rebuildCount() == 2);
}

const ProcessedGroundCell* processedAt(const ProcessedGround& processed, GridPosition3 position)
{
    const auto found = std::ranges::find(processed.cells, position, &ProcessedGroundCell::cell);
    return found == processed.cells.end() ? nullptr : &*found;
}

void testBoundaryDescriptions()
{
    TEST("boundaryDescriptions");
    const std::array lone { ground({ 0, 0, 0 }) };
    const auto isolated = compileGroundGeometry(lone, manifest());
    CHECK(isolated.cells.size() == 1);
    CHECK(isolated.exposedSideCount == 4);
    CHECK(isolated.convexCornerCount == 4);
    CHECK(isolated.concaveCornerCount == 0);
    CHECK(isolated.cells[0].convexCorners == 0x0f);
    CHECK(isolated.cells[0].diagonalNeighbors == 0);

    const std::array elbow {
        ground({ 1, 0, 0 }, { 2 }), ground({ 0, 1, 0 }), ground({ 0, 0, 0 }),
    };
    const auto elbowBefore = elbow;
    const auto lShape = compileGroundGeometry(elbow, manifest());
    CHECK(elbow == elbowBefore);
    CHECK(lShape.cells.size() == 3);
    CHECK(lShape.cells.front().cell == GridPosition3({ 0, 0, 0 }));
    CHECK(lShape.cells[1].cell == GridPosition3({ 1, 0, 0 }));
    CHECK(lShape.exposedSideCount == 8);
    CHECK(lShape.convexCornerCount == 5);
    CHECK(lShape.concaveCornerCount == 1);
    const auto* junction = processedAt(lShape, { 0, 0, 0 });
    CHECK(junction && junction->groundSideMask == (groundNorthSide | groundWestSide));
    CHECK(junction && junction->convexCorners == groundNorthWestCorner);
    CHECK(junction && junction->concaveCorners == groundSouthEastCorner);
    CHECK(junction && junction->sideNeighbors[1] == RenderModel { 2 });

    std::vector<RenderFrameData::Tile> ring;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (x != 1 || y != 1) ring.push_back(ground({ x, y, 0 }));
        }
    }
    const auto hollow = compileGroundGeometry(ring, manifest());
    CHECK(hollow.cells.size() == 8);
    CHECK(!processedAt(hollow, { 1, 1, 0 }));
    CHECK(hollow.exposedSideCount == 16);
    CHECK(hollow.convexCornerCount == 4);
    CHECK(hollow.concaveCornerCount == 4);
    const auto* outsideCorner = processedAt(hollow, { 0, 0, 0 });
    CHECK(outsideCorner && outsideCorner->convexCorners == groundNorthWestCorner);
    CHECK(outsideCorner && outsideCorner->concaveCorners == groundSouthEastCorner);
    const auto* top = processedAt(hollow, { 1, 0, 0 });
    CHECK(top && top->diagonalNeighbors == (groundSouthEastCorner | groundSouthWestCorner));
    CHECK(top && top->diagonalNeighborModels[2] == RenderModel { 1 });
    CHECK(top && top->diagonalNeighborModels[3] == RenderModel { 1 });

    std::array diagonal { ground({ 0, 0, 0 }), ground({ 1, 1, 0 }, { 2 }) };
    const auto touching = compileGroundGeometry(diagonal, manifest());
    CHECK(touching.exposedSideCount == 8);
    CHECK(touching.convexCornerCount == 8);
    CHECK(touching.concaveCornerCount == 0);
    CHECK(touching.cells[0].diagonalNeighbors == groundSouthEastCorner);
    CHECK(touching.cells[0].diagonalNeighborModels[2] == RenderModel { 2 });
    CHECK(touching.cells[1].diagonalNeighbors == groundNorthWestCorner);
    diagonal[1].cell.z = 1;
    diagonal[1].baseElevation = 1;
    const auto layered = compileGroundGeometry(diagonal, manifest());
    CHECK(layered.cells[0].diagonalNeighbors == 0);
    CHECK(layered.cells[1].diagonalNeighbors == 0);
}

void testCacheReuseAndInvalidation()
{
    TEST("cacheReuseAndInvalidation");
    GroundGeometryCache cache;
    std::vector tiles {
        ground({ 1, 0, 0 }, { 2 }), ground({ 0, 1, 0 }), ground({ 0, 0, 0 }),
    };
    auto uncached = tiles;
    processGroundGeometry(uncached, manifest());
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(tiles == uncached);
    CHECK(cache.rebuildCount() == 1);
    CHECK(cache.hitCount() == 0);
    CHECK(!cache.lastResultReused());
    CHECK(cache.processed() == compileGroundGeometry(tiles, manifest()));
    const auto description = cache.processed();
    const auto capacity = cache.capacityBytes();
    CHECK(tiles[2].groundGeometryEligible);
    CHECK(tiles[2].groundRimSides == (groundNorthSide | groundWestSide));
    CHECK(tiles[2].groundRimConcaveCorners == groundSouthEastCorner);
    CHECK(tiles[0].groundDiagonalNeighbors[3] == RenderModel { 1 });

    // Color/material edits and unrelated actors leave occupancy unchanged.
    // Tile order is irrelevant, and applying cached masks restores corrupted
    // transient render values without depending on their previous contents.
    std::ranges::reverse(tiles);
    tiles[0].color.x = 0.25f;
    tiles[0].groundSideMask = 0;
    tiles[0].groundSideNeighbors = {};
    tiles[0].groundRimConcaveCorners = 0;
    auto preview = ground({ -1, 0, 0 });
    preview.isEditorPreview = true;
    tiles.push_back(preview);
    FrameArena untouchedArena("cached ground scratch test", 1);
    processGroundGeometry(tiles, manifest(), &untouchedArena, &cache);
    CHECK(cache.lastResultReused());
    CHECK(cache.hitCount() == 1);
    CHECK(cache.rebuildCount() == 1);
    CHECK(cache.capacityBytes() == capacity);
    CHECK(cache.processed() == description);
    CHECK(untouchedArena.bytesUsed() == 0);
    CHECK(!untouchedArena.exhausted());
    CHECK(!tiles.back().groundGeometryEligible);
    CHECK(tiles.back().groundSideMask == groundAllSides);
    CHECK(tiles[0].groundRimConcaveCorners == groundSouthEastCorner);
    for (int frame = 0; frame < 64; ++frame) {
        std::ranges::reverse(tiles);
        processGroundGeometry(tiles, manifest(), nullptr, &cache);
        CHECK(cache.capacityBytes() == capacity);
        CHECK(cache.lastResultReused());
    }

    // An adjacent ground variant changes the readiness identity even when
    // its occupancy and exposed sides are the same.
    auto east = std::ranges::find(tiles, GridPosition3 { 1, 0, 0 }, &RenderFrameData::Tile::cell);
    CHECK(east != tiles.end());
    east->model = { 1 };
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(!cache.lastResultReused());
    CHECK(cache.rebuildCount() == 2);
    CHECK(processedAt(cache.processed(), { 0, 0, 0 })->sideNeighbors[1] == RenderModel { 1 });
    east->pickOnly = true; // move/delete source preview no longer occludes
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.rebuildCount() == 3);
    CHECK(cache.processed().cells.size() == 2);
    CHECK(!east->groundGeometryEligible);
    CHECK(processedAt(cache.processed(), { 0, 0, 0 })->groundSideMask ==
        (groundAllSides & ~groundSouthSide));
    east->pickOnly = false;
    east->cell.z = 1;
    east->baseElevation = 1;
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.rebuildCount() == 4);
    CHECK(processedAt(cache.processed(), { 1, 0, 1 }));
    CHECK(east->groundSideMask == groundAllSides);
    east->size.x = 0.9f;
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.rebuildCount() == 5);
    CHECK(cache.processed().cells.size() == 2);
    tiles.erase(east);
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.lastResultReused()); // removing an already excluded tile

    // Explicit invalidation retains capacity and forces one fresh compile.
    cache.invalidate();
    CHECK(cache.processed().cells.empty());
    CHECK(cache.capacityBytes() == capacity);
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.rebuildCount() == 6);
    CHECK(!cache.lastResultReused());
    CHECK(cache.capacityBytes() == capacity);
    tiles.clear(); // screen/layer visibility can remove every eligible tile
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.rebuildCount() == 7);
    CHECK(cache.processed().cells.empty());
    CHECK(cache.processed().exposedSideCount == 0);
    processGroundGeometry(tiles, manifest(), nullptr, &cache);
    CHECK(cache.lastResultReused());
}

void testCacheOwnershipDuplicatesAndExtremeCells()
{
    TEST("cacheOwnershipDuplicatesAndExtremeCells");
    GroundGeometryCache cache;
    std::vector duplicates {
        ground({ 0, 0, 0 }, { 2 }), ground({ 1, 0, 0 }), ground({ 0, 0, 0 }),
    };
    auto noCache = duplicates;
    processGroundGeometry(noCache, manifest());
    processGroundGeometry(duplicates, manifest(), nullptr, &cache);
    CHECK(duplicates == noCache);
    CHECK(cache.processed().cells.size() == 2);
    CHECK(cache.processed().cells[0].model == RenderModel { 1 });
    CHECK(duplicates[1].groundSideNeighbors[3] == RenderModel { 1 });
    const auto description = cache.processed();
    std::ranges::reverse(duplicates);
    processGroundGeometry(duplicates, manifest(), nullptr, &cache);
    CHECK(cache.lastResultReused());
    CHECK(cache.processed() == description);

    // A copied cache owns its table/description; it never references the
    // original frame or another cache's vector storage.
    auto copiedCache = cache;
    cache.invalidate();
    processGroundGeometry(duplicates, manifest(), nullptr, &copiedCache);
    CHECK(copiedCache.lastResultReused());
    CHECK(copiedCache.processed() == description);
    duplicates.clear();
    CHECK(copiedCache.processed() == description);
    CHECK(description.cells[0].cell == GridPosition3({ 0, 0, 0 }));

    std::array extremes {
        ground({ std::numeric_limits<int>::min(), 0, 0 }),
        ground({ std::numeric_limits<int>::max(), 0, 0 }),
    };
    const auto isolated = compileGroundGeometry(extremes, manifest());
    CHECK(isolated.cells.size() == 2);
    CHECK(isolated.exposedSideCount == 8);
    CHECK(isolated.convexCornerCount == 8);
    CHECK(isolated.concaveCornerCount == 0);
    processGroundGeometry(extremes, manifest(), nullptr, &copiedCache);
    CHECK(extremes[0].groundGeometryEligible);
    CHECK(extremes[1].groundGeometryEligible);
    CHECK(extremes[0].groundSideMask == groundAllSides);
    CHECK(extremes[1].groundSideMask == groundAllSides);
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
        testBoundaryDescriptions();
        testCacheReuseAndInvalidation();
        testCacheOwnershipDuplicatesAndExtremeCells();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }
    std::cout << "GroundGeometryTests: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
