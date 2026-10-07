#include "TestHarness.hpp"

#include "engine/render/GroundRimSurfaceCache.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace sokoban;

RenderFrameData::Tile rimTile(int x, int y, float elevation = 0)
{
    return {
        .cell = { x, y, 0 },
        .position = { static_cast<float>(x), static_cast<float>(y) },
        .color = { 1, 1, 1, 1 },
        .baseElevation = elevation,
        .height = 1,
        .model = { 1 },
        .effect = RenderSurfaceEffect::GroundSplat,
        .groundTop = true,
        .groundGeometryEligible = true,
        .groundRimSides = groundAllSides,
        .groundRimWidth = 0.12f,
        .groundRimDepth = 0.10f,
    };
}

std::vector<RenderFrameData::Tile> fixture()
{
    std::vector tiles { rimTile(-3, 4), rimTile(2, -1, 2), rimTile(7, 9) };
    tiles[1].groundRimSides = groundNorthSide | groundWestSide;
    tiles[2].groundRimSides = 0;
    tiles[2].groundRimConcaveCorners = groundNorthWestCorner | groundSouthEastCorner;
    return tiles;
}

void checkSurfaceEqual(const GroundRimSurface& actual, const GroundRimSurface& expected)
{
    CHECK(actual.count == expected.count);
    for (std::size_t index = 0; index < std::min(actual.count, expected.count); ++index) {
        CHECK(actual.patches[index].vertices == expected.patches[index].vertices);
        CHECK(actual.patches[index].normal == expected.patches[index].normal);
        CHECK(actual.patches[index].wallCoverage == expected.patches[index].wallCoverage);
    }
}

void checkCacheParity(const GroundRimSurfaceCache& cache,
    std::span<const RenderFrameData::Tile> tiles)
{
    std::size_t expectedCount = 0;
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const GroundRimSurface expected = buildGroundRimSurface(tiles[index]);
        const GroundRimSurface* actual = cache.surfaceForTileIndex(index);
        if (expected.count == 0) {
            CHECK(actual == nullptr);
        } else {
            ++expectedCount;
            CHECK(actual != nullptr);
            if (actual) checkSurfaceEqual(*actual, expected);
        }
    }
    CHECK(cache.surfaceCount() == expectedCount);
    CHECK(cache.surfaceForTileIndex(tiles.size()) == nullptr);
    CHECK(cache.surfaceForTileIndex(std::numeric_limits<std::size_t>::max()) == nullptr);
}

void testCompilationAndExactParity()
{
    TEST("compilationAndExactParity");
    auto tiles = fixture();
    tiles.insert(tiles.begin() + 1, rimTile(20, 5));
    tiles[1].groundRimWidth = 0;
    const auto compiled = compileGroundRimSurfaces(tiles);
    CHECK(compiled.size() == 3);
    constexpr std::array<std::size_t, 3> expectedIndices { 0, 2, 3 };
    for (std::size_t index = 0; index < std::min(compiled.size(), expectedIndices.size()); ++index) {
        CHECK(compiled[index].tileIndex == expectedIndices[index]);
        checkSurfaceEqual(compiled[index].surface,
            buildGroundRimSurface(tiles[expectedIndices[index]]));
    }
    CHECK(compileGroundRimSurfaces(std::span<const RenderFrameData::Tile> {}).empty());
    GroundRimSurfaceCache cache;
    cache.update(tiles);
    checkCacheParity(cache, tiles);
    CHECK(cache.generatedSurfaceCount() == 3);
    CHECK(cache.reusedSurfaceCount() == 0);
}

void testUnchangedAndPresentationEditsReuseGeometry()
{
    TEST("unchangedAndPresentationEditsReuseGeometry");
    auto tiles = fixture();
    GroundRimSurfaceCache cache;
    cache.update(tiles);
    const auto rebuilds = cache.rebuildCount();
    const auto initialHits = cache.hitCount();
    const auto capacity = cache.capacityBytes();
    for (uint32_t frame = 0; frame < 64; ++frame) {
        auto& tile = tiles[1];
        tile.cell = { static_cast<int>(frame), -14, 3 };
        tile.color = { 0.2f, 0.7f, 0.1f, 0.4f };
        tile.effect = RenderSurfaceEffect::Standard;
        tile.pickable = !tile.pickable;
        tile.showGrid = !tile.showGrid;
        tile.affectsCameraFit = !tile.affectsCameraFit;
        tile.groundGeometryEligible = !tile.groundGeometryEligible;
        tile.groundRockVariant = frame;
        tile.groundSplat = GroundSplatTextures {
            .base = { 11 }, .detail = { 12 }, .splatMap = { 13 }, .rimWall = { 14 },
        };
        tile.groundSplatOrigin = { static_cast<int>(frame), 0 };
        cache.update(tiles);
        CHECK(cache.lastUpdateReused());
        CHECK(cache.generatedSurfaceCount() == 0);
        CHECK(cache.reusedSurfaceCount() == tiles.size());
        CHECK(cache.rebuildCount() == rebuilds);
        CHECK(cache.capacityBytes() == capacity);
    }
    CHECK(cache.hitCount() == initialHits + 64);
    checkCacheParity(cache, tiles);
}

void testSelectiveGeometryInvalidation()
{
    TEST("selectiveGeometryInvalidation");
    const auto original = fixture();
    GroundRimSurfaceCache cache;
    const auto verifyEdit = [&](auto edit) {
        cache.update(original);
        const auto rebuilds = cache.rebuildCount();
        auto edited = original;
        edit(edited[1]);
        cache.update(edited);
        CHECK(!cache.lastUpdateReused());
        CHECK(cache.generatedSurfaceCount() == 1);
        CHECK(cache.reusedSurfaceCount() == 2);
        CHECK(cache.rebuildCount() == rebuilds + 1);
        checkCacheParity(cache, edited);
    };
    verifyEdit([](auto& tile) { tile.model = { 2 }; });
    verifyEdit([](auto& tile) { tile.position.x += 0.5f; });
    verifyEdit([](auto& tile) { tile.position.y -= 0.75f; });
    verifyEdit([](auto& tile) { tile.baseElevation += 0.25f; });
    verifyEdit([](auto& tile) { tile.height += 0.25f; });
    verifyEdit([](auto& tile) { tile.groundRimSides ^= groundNorthSide; });
    verifyEdit([](auto& tile) { tile.groundRimConcaveCorners = groundSouthEastCorner; });
    verifyEdit([](auto& tile) { tile.groundRimWidth *= 0.8f; });
    verifyEdit([](auto& tile) { tile.groundRimDepth *= 0.8f; });
    // Equal summed top elevation still has distinct authored inputs.
    verifyEdit([](auto& tile) { tile.baseElevation -= 0.25f; tile.height += 0.25f; });

    auto signedZero = fixture();
    cache.update(signedZero);
    signedZero[0].baseElevation = -0.0f;
    cache.update(signedZero);
    CHECK(cache.generatedSurfaceCount() == 1);
    CHECK(cache.reusedSurfaceCount() == 2);
    checkCacheParity(cache, signedZero);
}

void testEligibilityAndInvalidProfilesRemoveSurfaces()
{
    TEST("eligibilityAndInvalidProfilesRemoveSurfaces");
    const auto original = fixture();
    GroundRimSurfaceCache cache;
    const auto verifyAbsent = [&](auto edit) {
        cache.update(original);
        auto edited = original;
        edit(edited[1]);
        cache.update(edited);
        CHECK(cache.surfaceForTileIndex(1) == nullptr);
        CHECK(cache.generatedSurfaceCount() == 0);
        CHECK(cache.reusedSurfaceCount() == 2);
        checkCacheParity(cache, edited);
    };
    verifyAbsent([](auto& tile) { tile.groundTop = false; });
    verifyAbsent([](auto& tile) { tile.model = cubeModel; });
    verifyAbsent([](auto& tile) { tile.pickOnly = true; });
    verifyAbsent([](auto& tile) { tile.isEditorPreview = true; });
    verifyAbsent([](auto& tile) { tile.groundRimSides = 0; tile.groundRimConcaveCorners = 0; });
    verifyAbsent([](auto& tile) { tile.groundRimWidth = 0; });
    verifyAbsent([](auto& tile) { tile.groundRimDepth = 0; });
    verifyAbsent([](auto& tile) { tile.groundRimWidth = 0.5001f; });
    verifyAbsent([](auto& tile) { tile.groundRimDepth = 0.30f; });
    verifyAbsent([](auto& tile) { tile.groundRimWidth = std::numeric_limits<float>::quiet_NaN(); });
    verifyAbsent([](auto& tile) { tile.position.x = std::numeric_limits<float>::infinity(); });
}

void testReorderingPruningAndDuplicates()
{
    TEST("reorderingPruningAndDuplicates");
    auto tiles = fixture();
    GroundRimSurfaceCache cache;
    cache.update(tiles);
    std::reverse(tiles.begin(), tiles.end());
    cache.update(tiles);
    CHECK(cache.generatedSurfaceCount() == 0);
    CHECK(cache.reusedSurfaceCount() == 3);
    checkCacheParity(cache, tiles);
    tiles.erase(tiles.begin());
    cache.update(tiles);
    CHECK(cache.generatedSurfaceCount() == 0);
    CHECK(cache.reusedSurfaceCount() == 2);
    checkCacheParity(cache, tiles);
    tiles.push_back(rimTile(-12, 7));
    cache.update(tiles);
    CHECK(cache.generatedSurfaceCount() == 1);
    CHECK(cache.reusedSurfaceCount() == 2);
    checkCacheParity(cache, tiles);
    tiles.push_back(tiles[1]);
    cache.update(tiles);
    checkCacheParity(cache, tiles);
    const auto capacity = cache.capacityBytes();
    const auto rebuilds = cache.rebuildCount();
    for (uint32_t iteration = 0; iteration < 64; ++iteration) {
        std::rotate(tiles.begin(), tiles.begin() + 1, tiles.end());
        cache.update(tiles);
        CHECK(cache.generatedSurfaceCount() == 0);
        CHECK(cache.rebuildCount() == rebuilds);
        CHECK(cache.capacityBytes() == capacity);
        checkCacheParity(cache, tiles);
    }
    tiles.clear();
    cache.update(tiles);
    CHECK(cache.surfaceCount() == 0);
    CHECK(cache.surfaceForTileIndex(0) == nullptr);
    CHECK(cache.capacityBytes() == capacity);
}

void testResolvedReadinessAndToggleTransitions()
{
    TEST("resolvedReadinessAndToggleTransitions");
    const auto rimmed = fixture();
    auto flat = rimmed;
    for (auto& tile : flat) {
        tile.groundRimWidth = 0;
        tile.groundRimDepth = 0;
    }
    GroundRimSurfaceCache cache;
    // An unpublished participant resolves the entire cohort to flat input;
    // the cache never queries GPU state or keeps a partially rimmed scene.
    cache.update(flat);
    CHECK(cache.surfaceCount() == 0);
    CHECK(cache.generatedSurfaceCount() == 0);
    CHECK(cache.capacityBytes() == 0);
    checkCacheParity(cache, flat);
    cache.update(rimmed);
    CHECK(cache.surfaceCount() == rimmed.size());
    CHECK(cache.generatedSurfaceCount() == rimmed.size());
    checkCacheParity(cache, rimmed);
    const auto capacity = cache.capacityBytes();
    cache.update(flat);
    CHECK(cache.surfaceCount() == 0);
    CHECK(cache.capacityBytes() == capacity);
    checkCacheParity(cache, flat);
    cache.update(rimmed);
    checkCacheParity(cache, rimmed);
    cache.invalidate();
    CHECK(cache.capacityBytes() == capacity);
    cache.update(rimmed);
    CHECK(!cache.lastUpdateReused());
    CHECK(cache.generatedSurfaceCount() == rimmed.size());
    checkCacheParity(cache, rimmed);
}

void testOwningSourceAndCopiedCacheLifetime()
{
    TEST("owningSourceAndCopiedCacheLifetime");
    GroundRimSurfaceCache cache;
    std::vector<CompiledGroundRimSurface> compiled;
    {
        auto ephemeral = fixture();
        cache.update(ephemeral);
        compiled = compileGroundRimSurfaces(ephemeral);
        ephemeral[0].position = { 100, 100 };
        ephemeral.clear();
        ephemeral.shrink_to_fit();
    }
    CHECK(cache.surfaceCount() == compiled.size());
    for (const auto& entry : compiled) {
        const auto* surface = cache.surfaceForTileIndex(entry.tileIndex);
        CHECK(surface != nullptr);
        if (surface) checkSurfaceEqual(*surface, entry.surface);
    }
    GroundRimSurfaceCache copied = cache;
    GroundRimSurfaceCache assigned;
    assigned = copied;
    auto edited = fixture();
    edited[0].position = { -100, 100 };
    cache.update(edited);
    cache.invalidate();
    for (const auto& entry : compiled) {
        const auto* copiedSurface = copied.surfaceForTileIndex(entry.tileIndex);
        const auto* assignedSurface = assigned.surfaceForTileIndex(entry.tileIndex);
        CHECK(copiedSurface != nullptr && assignedSurface != nullptr);
        if (copiedSurface) checkSurfaceEqual(*copiedSurface, entry.surface);
        if (assignedSurface) checkSurfaceEqual(*assignedSurface, entry.surface);
    }
}

} // namespace

int main()
{
    testCompilationAndExactParity();
    testUnchangedAndPresentationEditsReuseGeometry();
    testSelectiveGeometryInvalidation();
    testEligibilityAndInvalidProfilesRemoveSurfaces();
    testReorderingPruningAndDuplicates();
    testResolvedReadinessAndToggleTransitions();
    testOwningSourceAndCopiedCacheLifetime();
    if (failures != 0) {
        std::cerr << "GroundRimSurfaceCacheTests: " << failures << " checks failed\n";
        return 1;
    }
    std::cout << "GroundRimSurfaceCacheTests passed (" << checks << " checks)\n";
    return 0;
}
