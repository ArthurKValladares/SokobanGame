#include "TestHarness.hpp"

#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace sokoban;

RenderFrameData::Tile tile(int x, int y, int layer = 0)
{
    return {
        .cell = { x, y, layer }, .position = { static_cast<float>(x), static_cast<float>(y) },
        .color = { 1, 1, 1, 1 }, .baseElevation = static_cast<float>(layer), .height = 1,
        .model = { 1 }, .effect = RenderSurfaceEffect::GroundSplat, .groundTop = true,
        .groundGeometryEligible = true, .groundRimSides = groundAllSides,
        .groundRimWidth = 0.12f, .groundRimDepth = 0.10f,
    };
}

std::vector<RenderFrameData::Tile> fixture()
{
    std::vector tiles { tile(0, 0), tile(1, 0), tile(2, 0), tile(-1, -1, 2) };
    tiles[1].groundRimSides = groundNorthSide | groundEastSide;
    tiles[2].groundRimSides = 0;
    tiles[3].groundRimSides = 0;
    tiles[3].groundRimConcaveCorners = groundNorthWestCorner | groundSouthEastCorner;
    return tiles;
}

GroundRimSurface expectedSurface(const RenderFrameData::Tile& source)
{
    if (hasGroundRimSurface(source)) return buildGroundRimSurface(source);
    GroundRimSurface result;
    result.count = 1;
    const float top = source.baseElevation + source.height;
    result.patches[0] = {
        .vertices = {{
            { source.position.x, source.position.y, top },
            { source.position.x + 1, source.position.y, top },
            { source.position.x + 1, source.position.y + 1, top },
            { source.position.x, source.position.y + 1, top },
        }},
        .normal = { 0, 0, 1 },
    };
    return result;
}

const RenderFrameData::Tile* sourceFor(const GroundChunkTile& compiled,
    std::span<const RenderFrameData::Tile> sources)
{
    const auto found = std::ranges::find_if(sources, [&](const auto& source) {
        return source.cell == compiled.cell && groundRimSurfaceKey(source) == compiled.key;
    });
    return found == sources.end() ? nullptr : &*found;
}

void checkUnoptimizedParity(const GroundChunkGeometry& geometry,
    std::span<const RenderFrameData::Tile> sources)
{
    constexpr std::array<Vec2, 4> coords {{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }};
    constexpr std::array<uint32_t, 6> indices { 0, 1, 2, 0, 2, 3 };
    uint64_t bytes = 0;
    for (const auto& chunk : geometry.chunks) {
        std::size_t firstVertex = 0;
        std::size_t firstIndex = 0;
        Aabb expectedBounds;
        for (std::size_t slot = 0; slot < chunk.tiles.size(); ++slot) {
            const auto* source = sourceFor(chunk.tiles[slot], sources);
            CHECK(source != nullptr);
            if (!source) continue;
            const auto surface = expectedSurface(*source);
            for (std::size_t patchIndex = 0; patchIndex < surface.count; ++patchIndex) {
                const auto& patch = surface.patches[patchIndex];
                const Vec4 coverage { patch.wallCoverage[0], patch.wallCoverage[1], patch.wallCoverage[2], patch.wallCoverage[3] };
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    CHECK(firstVertex + corner < chunk.vertices.size());
                    if (firstVertex + corner >= chunk.vertices.size()) continue;
                    const auto& vertex = chunk.vertices[firstVertex + corner];
                    CHECK(vertex.position == patch.vertices[corner]);
                    CHECK(vertex.normal == patch.normal);
                    CHECK(vertex.faceCoord == coords[corner]);
                    CHECK(vertex.wallCoverage == coverage);
                    CHECK(vertex.tileSlot == slot);
                    expectedBounds = expand(expectedBounds, vertex.position);
                }
                for (std::size_t corner = 0; corner < indices.size(); ++corner) {
                    CHECK(firstIndex + corner < chunk.indices.size());
                    if (firstIndex + corner < chunk.indices.size()) {
                        CHECK(chunk.indices[firstIndex + corner] == firstVertex + indices[corner]);
                    }
                }
                firstVertex += 4;
                firstIndex += 6;
            }
        }
        CHECK(firstVertex == chunk.vertices.size() && firstIndex == chunk.indices.size());
        CHECK(chunk.inputVertexCount == chunk.vertices.size());
        CHECK(chunk.bounds == expectedBounds && chunk.bounds.valid());
        bytes += chunk.vertices.size() * sizeof(GroundChunkVertex) + chunk.indices.size() * sizeof(uint32_t);
    }
    CHECK(geometry.geometryBytes == bytes && geometry.originalBytes == bytes);
    CHECK(!geometry.optimized);
}

void checkGeometryEqual(const GroundChunkGeometry& left, const GroundChunkGeometry& right)
{
    CHECK(left.chunks.size() == right.chunks.size());
    CHECK(left.optimized == right.optimized);
    CHECK(left.geometryBytes == right.geometryBytes && left.originalBytes == right.originalBytes);
    for (std::size_t chunk = 0; chunk < std::min(left.chunks.size(), right.chunks.size()); ++chunk) {
        CHECK(left.chunks[chunk].tiles == right.chunks[chunk].tiles);
        CHECK(left.chunks[chunk].vertices == right.chunks[chunk].vertices);
        CHECK(left.chunks[chunk].indices == right.chunks[chunk].indices);
        CHECK(left.chunks[chunk].bounds == right.chunks[chunk].bounds);
        CHECK(left.chunks[chunk].inputVertexCount == right.chunks[chunk].inputVertexCount);
        CHECK(left.chunks[chunk].inputAcmr == right.chunks[chunk].inputAcmr);
        CHECK(left.chunks[chunk].outputAcmr == right.chunks[chunk].outputAcmr);
    }
}

using Triangle = std::array<uint32_t, 39>;

std::vector<Triangle> triangles(const GroundChunk& chunk)
{
    std::vector<Triangle> result;
    CHECK(chunk.indices.size() % 3 == 0);
    for (std::size_t index = 0; index + 2 < chunk.indices.size(); index += 3) {
        Triangle triangle {};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const uint32_t vertex = chunk.indices[index + corner];
            CHECK(vertex < chunk.vertices.size());
            if (vertex >= chunk.vertices.size()) continue;
            const auto bits = std::bit_cast<std::array<uint32_t, 13>>(chunk.vertices[vertex]);
            std::copy(bits.begin(), bits.end(), triangle.begin() + static_cast<std::ptrdiff_t>(corner * bits.size()));
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

void testExactCapAndFlatTopology()
{
    TEST("exactCapAndFlatTopology");
    const auto tiles = fixture();
    const auto geometry = compileGroundChunkGeometry(tiles, nullptr, false);
    CHECK(geometry.chunks.size() == 2);
    checkUnoptimizedParity(geometry, tiles);
    bool foundFlat = false;
    bool foundDegenerate = false;
    for (const auto& chunk : geometry.chunks) {
        for (const auto& compiled : chunk.tiles) {
            if (compiled.cell == tiles[2].cell) foundFlat = true;
        }
        for (std::size_t index = 0; index + 5 < chunk.indices.size(); index += 6) {
            const auto& c = chunk.vertices[chunk.indices[index + 2]];
            const auto& d = chunk.vertices[chunk.indices[index + 5]];
            foundDegenerate = foundDegenerate || c.position == d.position;
        }
    }
    CHECK(foundFlat && foundDegenerate);
    auto translated = tiles;
    for (auto& source : translated) {
        source.position = source.position + Vec2 { 40, -17 };
        source.baseElevation += 0.5f;
    }
    checkUnoptimizedParity(compileGroundChunkGeometry(translated, nullptr, false), translated);
}

void testOptimizedTriangleAttributeParity()
{
    TEST("optimizedTriangleAttributeParity");
    const auto tiles = fixture();
    const auto original = compileGroundChunkGeometry(tiles, nullptr, false);
    const auto optimized = compileGroundChunkGeometry(tiles, nullptr, true);
    CHECK(optimized.optimized && optimized.chunks.size() == original.chunks.size());
    CHECK(optimized.originalBytes == original.geometryBytes);
    CHECK(optimized.geometryBytes <= optimized.originalBytes);
    for (std::size_t index = 0; index < std::min(original.chunks.size(), optimized.chunks.size()); ++index) {
        const auto& input = original.chunks[index];
        const auto& output = optimized.chunks[index];
        CHECK(input.tiles == output.tiles && input.bounds == output.bounds);
        CHECK(input.indices.size() == output.indices.size());
        CHECK(output.vertices.size() <= input.vertices.size());
        CHECK(triangles(input) == triangles(output));
        CHECK(output.inputVertexCount == input.vertices.size());
        CHECK(std::isfinite(output.inputAcmr) && std::isfinite(output.outputAcmr));
        CHECK(output.inputAcmr > 0 && output.outputAcmr > 0);
    }
}

void testSpatialLayerGroupingAndAmbiguousCells()
{
    TEST("spatialLayerGroupingAndAmbiguousCells");
    std::vector tiles {
        tile(-9, -9), tile(-8, -8), tile(-1, -1), tile(0, 0), tile(7, 7),
        tile(8, 8), tile(15, 15), tile(16, 0), tile(0, 0, 1), tile(7, 7, 1),
    };
    const auto original = compileGroundChunkGeometry(tiles, nullptr, false);
    CHECK(original.chunks.size() == 6);
    std::size_t count = 0;
    for (const auto& chunk : original.chunks) count += chunk.tiles.size();
    CHECK(count == tiles.size());
    checkUnoptimizedParity(original, tiles);
    std::reverse(tiles.begin(), tiles.end());
    checkGeometryEqual(original, compileGroundChunkGeometry(tiles, nullptr, false));
    auto duplicate = tile(0, 0);
    duplicate.model = { 2 };
    tiles.push_back(duplicate);
    const auto ambiguous = compileGroundChunkGeometry(tiles, nullptr, false);
    count = 0;
    for (const auto& chunk : ambiguous.chunks) {
        count += chunk.tiles.size();
        for (const auto& compiled : chunk.tiles) CHECK(compiled.cell != duplicate.cell);
    }
    CHECK(count == 9);
    const std::array identical { tile(3, 3), tile(3, 3) };
    CHECK(compileGroundChunkGeometry(identical).chunks.empty());
    auto actor = tile(3, 3);
    actor.groundTop = false;
    const std::array layeredActor { tile(3, 3), actor };
    CHECK(compileGroundChunkGeometry(layeredActor).chunks.size() == 1);
}

void testConservativeEligibility()
{
    TEST("conservativeEligibility");
    const auto source = tile(0, 0);
    CHECK(isGroundChunkTileEligible(source));
    const auto reject = [&](auto mutate) {
        auto changed = source;
        mutate(changed);
        CHECK(!isGroundChunkTileEligible(changed));
        CHECK(compileGroundChunkGeometry(std::span(&changed, 1)).chunks.empty());
    };
    reject([](auto& changed) { changed.groundGeometryEligible = false; });
    reject([](auto& changed) { changed.groundTop = false; });
    reject([](auto& changed) { changed.model = cubeModel; });
    reject([](auto& changed) { changed.effect = RenderSurfaceEffect::Standard; });
    reject([](auto& changed) { changed.pickOnly = true; });
    reject([](auto& changed) { changed.isEditorPreview = true; });
    reject([](auto& changed) { changed.blurBehind = true; });
    reject([](auto& changed) { changed.color.w = 0.5f; });
    reject([](auto& changed) { changed.size.x = 1.1f; });
    reject([](auto& changed) { changed.height = 2; });
    reject([](auto& changed) { changed.modelTransform = RenderFrameData::ModelTransform {}; });
    reject([](auto& changed) { changed.modelRotationQuarterTurns = 1; });
    reject([](auto& changed) { changed.modelRotationOffsetRadians = 0.1f; });
    reject([](auto& changed) { changed.animation = { 1 }; });
    reject([](auto& changed) { changed.animationFallback = { 1 }; });
    reject([](auto& changed) { changed.animationInstanceId = 1; });
    reject([](auto& changed) { changed.renderableId = 1; });
    reject([](auto& changed) { changed.beltScrollOffset = 0.1f; });
    reject([](auto& changed) { changed.position.x = 0.5f; });
    reject([](auto& changed) { changed.position.x = std::numeric_limits<float>::infinity(); });
    reject([](auto& changed) { changed.baseElevation = std::numeric_limits<float>::quiet_NaN(); });
    reject([](auto& changed) { changed.groundRimSides = 16; });
    reject([](auto& changed) { changed.groundRimConcaveCorners = 16; });
    reject([](auto& changed) { changed.groundRimWidth = -0.1f; });
    reject([](auto& changed) { changed.groundRimWidth = 0.6f; });
    reject([](auto& changed) { changed.groundRimDepth = std::numeric_limits<float>::quiet_NaN(); });
}

void testCacheReuseChangesAndOwningLifetime()
{
    TEST("cacheReuseChangesAndOwningLifetime");
    auto tiles = fixture();
    GroundChunkGeometryCache cache;
    const auto initial = cache.update(tiles, nullptr, false);
    CHECK(!cache.lastUpdateReused() && cache.rebuildCount() == 1);
    const auto capacity = cache.capacityBytes();
    for (uint32_t frame = 0; frame < 64; ++frame) {
        std::rotate(tiles.begin(), tiles.begin() + 1, tiles.end());
        tiles[0].color = { 0.2f, 0.7f, 0.1f, 1 };
        tiles[0].pickable = !tiles[0].pickable;
        tiles[0].showGrid = !tiles[0].showGrid;
        tiles[0].groundSplat = GroundSplatTextures { .base = { frame + 1 } };
        tiles[0].groundSplatOrigin = { static_cast<int>(frame), -7 };
        CHECK(cache.update(tiles, nullptr, false) == initial);
        CHECK(cache.lastUpdateReused() && cache.capacityBytes() == capacity);
    }
    CHECK(cache.hitCount() == 64 && cache.rebuildCount() == 1);
    const auto changed = [&](auto mutate) {
        auto nextTiles = tiles;
        mutate(nextTiles[0]);
        const auto old = cache.update(tiles, nullptr, false);
        const auto next = cache.update(nextTiles, nullptr, false);
        CHECK(next != old && !cache.lastUpdateReused());
        checkUnoptimizedParity(*next, nextTiles);
        checkUnoptimizedParity(*old, tiles);
    };
    changed([](auto& source) { source.model.value += 1; });
    changed([](auto& source) { source.position.x += 1; });
    changed([](auto& source) { source.baseElevation += 0.5f; });
    changed([](auto& source) { source.groundRimSides ^= groundSouthSide; });
    changed([](auto& source) { source.groundRimConcaveCorners ^= groundNorthEastCorner; });
    changed([](auto& source) { source.groundRimWidth *= 0.8f; });
    changed([](auto& source) { source.groundRimDepth *= 0.8f; });
    changed([](auto& source) { source.cell.z += 1; });
    const auto beforeRemoval = cache.update(tiles, nullptr, false);
    auto removedTiles = tiles;
    removedTiles.pop_back();
    CHECK(cache.update(removedTiles, nullptr, false) != beforeRemoval);
    tiles[0].groundGeometryEligible = false;
    const auto excluded = cache.update(tiles, nullptr, false);
    CHECK(excluded != beforeRemoval);
    tiles[0].groundGeometryEligible = true;
    const auto restored = cache.update(tiles, nullptr, false);
    CHECK(restored != excluded);
    const auto optimized = cache.update(tiles, nullptr, true);
    CHECK(optimized != restored && optimized->optimized);
    cache.invalidate();
    CHECK(!cache.lastUpdateReused());
    checkUnoptimizedParity(*initial, fixture());
    CHECK(cache.update(tiles, nullptr, true) != optimized);
    GroundChunkGeometryCache empty;
    const auto firstEmpty = empty.update({});
    CHECK(firstEmpty->chunks.empty());
    CHECK(empty.update({}) == firstEmpty && empty.lastUpdateReused());
}

void testExactArtifactImportsAndResolvedReadiness()
{
    TEST("exactArtifactImportsAndResolvedReadiness");
    auto tiles = fixture();
    GroundChunkGeometryCache cache;
    std::shared_ptr<const GroundChunkGeometry> imported;
    {
        const auto artifact = buildProcessedGroundArtifact(tiles, 101);
        imported = cache.update(tiles, &artifact, false);
        checkUnoptimizedParity(*imported, tiles);
        checkGeometryEqual(*imported, compileGroundChunkGeometry(tiles, nullptr, false));
    }
    CHECK(cache.update(tiles, nullptr, false) == imported);
    const auto stale = buildProcessedGroundArtifact(tiles, 102);
    tiles[0].groundRimWidth *= 0.8f;
    const auto liveFallback = cache.update(tiles, &stale, false);
    CHECK(liveFallback != imported);
    checkGeometryEqual(*liveFallback, compileGroundChunkGeometry(tiles, nullptr, false));
    auto resolvedFlat = fixture();
    for (auto& source : resolvedFlat) {
        source.groundRimWidth = 0;
        source.groundRimDepth = 0;
    }
    const auto flat = cache.update(resolvedFlat, &stale, false);
    checkUnoptimizedParity(*flat, resolvedFlat);
    std::size_t vertices = 0;
    for (const auto& chunk : flat->chunks) vertices += chunk.vertices.size();
    CHECK(vertices == resolvedFlat.size() * 4);
    const auto ready = cache.update(fixture(), &stale, false);
    CHECK(ready != flat);
    checkUnoptimizedParity(*ready, fixture());
    checkUnoptimizedParity(*imported, fixture());
}

} // namespace

int main()
{
    testExactCapAndFlatTopology();
    testOptimizedTriangleAttributeParity();
    testSpatialLayerGroupingAndAmbiguousCells();
    testConservativeEligibility();
    testCacheReuseChangesAndOwningLifetime();
    testExactArtifactImportsAndResolvedReadiness();
    if (failures != 0) {
        std::cerr << "GroundChunkGeometryTests: " << failures << " checks failed\n";
        return 1;
    }
    std::cout << "GroundChunkGeometryTests passed (" << checks << " checks)\n";
    return 0;
}
