#include "engine/render/GroundChunkGeometry.hpp"

#include "engine/render/IndexedMeshOptimization.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace sokoban {
namespace {

struct Candidate {
    GroundChunkTile tile {};
    std::size_t sourceIndex = 0;
};

int chunkCoordinate(int cell) noexcept
{
    // Division truncates toward zero; the remainder correction implements
    // mathematical floor without overflowing at INT_MIN.
    const int quotient = cell / groundChunkCellWidth;
    return quotient - (cell % groundChunkCellWidth < 0 ? 1 : 0);
}

auto chunkKey(GridPosition3 cell) noexcept
{
    return std::tuple { cell.z, chunkCoordinate(cell.y), chunkCoordinate(cell.x) };
}

auto cellKey(GridPosition3 cell) noexcept
{
    return std::tuple { cell.z, chunkCoordinate(cell.y), chunkCoordinate(cell.x), cell.y, cell.x };
}

template <typename Signature>
void collectSignatures(std::span<const RenderFrameData::Tile> tiles,
    std::vector<Signature>& signatures)
{
    signatures.clear();
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const auto& tile = tiles[index];
        if (!isGroundChunkTileEligible(tile)) continue;
        if (signatures.size() == RenderFrameData::tileCapacity) {
            throw std::length_error("ground chunks exceed render tile capacity");
        }
        signatures.push_back({ { groundRimSurfaceKey(tile), tile.cell }, index });
    }
    std::ranges::sort(signatures, [](const auto& left, const auto& right) {
        const auto leftCell = cellKey(left.tile.cell);
        const auto rightCell = cellKey(right.tile.cell);
        return leftCell != rightCell ? leftCell < rightCell : left.tile.key < right.tile.key;
    });
    // Even identical duplicate keys are ambiguous cap owners. Omitting all
    // occurrences lets the caller retain their individual rendering path.
    std::size_t output = 0;
    for (std::size_t first = 0; first < signatures.size();) {
        std::size_t last = first + 1;
        while (last < signatures.size() && signatures[last].tile.cell == signatures[first].tile.cell) ++last;
        if (last == first + 1) signatures[output++] = signatures[first];
        first = last;
    }
    signatures.resize(output);
}

void appendPatch(GroundChunk& chunk, const GroundRimSurfacePatch& patch, uint32_t tileSlot)
{
    constexpr std::array<Vec2, 4> faceCoords {{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }};
    constexpr std::array<uint32_t, 6> quadIndices { 0, 1, 2, 0, 2, 3 };
    const uint32_t firstVertex = static_cast<uint32_t>(chunk.vertices.size());
    const Vec4 coverage {
        patch.wallCoverage[0], patch.wallCoverage[1], patch.wallCoverage[2], patch.wallCoverage[3],
    };
    for (std::size_t index = 0; index < patch.vertices.size(); ++index) {
        chunk.vertices.push_back({ patch.vertices[index], patch.normal, faceCoords[index], coverage, tileSlot });
        chunk.bounds = expand(chunk.bounds, patch.vertices[index]);
    }
    for (uint32_t index : quadIndices) chunk.indices.push_back(firstVertex + index);
}

GroundRimSurfacePatch flatPatch(const RenderFrameData::Tile& tile) noexcept
{
    const float top = tile.baseElevation + tile.height;
    return {
        .vertices = {{
            { tile.position.x, tile.position.y, top },
            { tile.position.x + 1.0f, tile.position.y, top },
            { tile.position.x + 1.0f, tile.position.y + 1.0f, top },
            { tile.position.x, tile.position.y + 1.0f, top },
        }},
        .normal = { 0, 0, 1 },
    };
}

template <typename Signature>
GroundChunkGeometry compileSignatures(std::span<const RenderFrameData::Tile> tiles,
    std::span<const Signature> signatures, const ProcessedGroundArtifact* artifact, bool optimize)
{
    GroundChunkGeometry result { .optimized = optimize };
    std::size_t first = 0;
    while (first < signatures.size()) {
        std::size_t last = first + 1;
        while (last < signatures.size() && chunkKey(signatures[last].tile.cell) == chunkKey(signatures[first].tile.cell)) ++last;
        auto& chunk = result.chunks.emplace_back();
        chunk.tiles.reserve(last - first);
        for (std::size_t index = first; index < last; ++index) {
            const auto& source = signatures[index];
            const auto& tile = tiles[source.sourceIndex];
            const uint32_t tileSlot = static_cast<uint32_t>(chunk.tiles.size());
            chunk.tiles.push_back(source.tile);
            if (hasGroundRimSurface(tile)) {
                const auto* baked = artifact ? artifact->find(source.tile.key) : nullptr;
                if (baked && baked->count > 0 && baked->count <= GroundRimSurface::capacity) {
                    for (std::size_t patch = 0; patch < baked->count; ++patch) appendPatch(chunk, baked->patches[patch], tileSlot);
                } else {
                    const auto surface = buildGroundRimSurface(tile);
                    for (std::size_t patch = 0; patch < surface.count; ++patch) appendPatch(chunk, surface.patches[patch], tileSlot);
                }
            } else {
                appendPatch(chunk, flatPatch(tile), tileSlot);
            }
        }
        chunk.inputVertexCount = static_cast<uint32_t>(chunk.vertices.size());
        result.originalBytes += chunk.vertices.size() * sizeof(GroundChunkVertex) + chunk.indices.size() * sizeof(uint32_t);
        const auto optimized = optimizeIndexedMesh(std::as_bytes(std::span(chunk.vertices)),
            sizeof(GroundChunkVertex), chunk.indices, optimize);
        if (optimized.vertices.size() % sizeof(GroundChunkVertex) != 0) {
            throw std::runtime_error("ground chunk optimizer returned an invalid vertex payload");
        }
        static_assert(std::is_trivially_copyable_v<GroundChunkVertex>);
        chunk.vertices.resize(optimized.vertices.size() / sizeof(GroundChunkVertex));
        if (!optimized.vertices.empty()) std::memcpy(chunk.vertices.data(), optimized.vertices.data(), optimized.vertices.size());
        chunk.indices = optimized.indices;
        chunk.inputAcmr = optimized.inputAcmr;
        chunk.outputAcmr = optimized.outputAcmr;
        result.geometryBytes += chunk.vertices.size() * sizeof(GroundChunkVertex) + chunk.indices.size() * sizeof(uint32_t);
        first = last;
    }
    return result;
}

} // namespace

bool isGroundChunkTileEligible(const RenderFrameData::Tile& tile) noexcept
{
    if (!tile.groundGeometryEligible || tile.groundModule || tile.cliffWall || !tile.groundTop || tile.model.isCube() ||
        tile.effect != RenderSurfaceEffect::GroundSplat || tile.pickOnly || tile.isEditorPreview ||
        tile.blurBehind || tile.color.w != 1.0f || tile.size != Vec2 { 1, 1 } || tile.height != 1.0f ||
        tile.modelTransform || tile.modelRotationQuarterTurns != 0 || tile.modelRotationOffsetRadians != 0 ||
        !tile.animation.isNone() || !tile.animationFallback.isNone() || tile.animationInstanceId != 0 ||
        tile.renderableId != 0 || tile.beltScrollOffset != 0 ||
        !std::isfinite(tile.position.x) || !std::isfinite(tile.position.y) ||
        std::floor(tile.position.x) != tile.position.x || std::floor(tile.position.y) != tile.position.y ||
        !std::isfinite(tile.baseElevation) || !std::isfinite(tile.baseElevation + tile.height) ||
        tile.groundRimSides > groundAllSides || tile.groundRimConcaveCorners > groundAllSides ||
        !std::isfinite(tile.groundRimWidth) || !std::isfinite(tile.groundRimDepth) ||
        tile.groundRimWidth < 0 || tile.groundRimDepth < 0) {
        return false;
    }
    return tile.groundRimWidth == 0 || tile.groundRimDepth == 0 ||
        groundRimProfileValid(groundRimProfileForSurface(tile));
}

GroundChunkGeometry compileGroundChunkGeometry(std::span<const RenderFrameData::Tile> tiles,
    const ProcessedGroundArtifact* artifact, bool optimize)
{
    std::vector<Candidate> signatures;
    collectSignatures(tiles, signatures);
    return compileSignatures(tiles, std::span<const Candidate>(signatures), artifact, optimize);
}

std::shared_ptr<const GroundChunkGeometry> GroundChunkGeometryCache::update(
    std::span<const RenderFrameData::Tile> tiles, const ProcessedGroundArtifact* artifact, bool optimize)
{
    collectSignatures(tiles, workingSignatures_);
    if (geometry_ && optimized_ == optimize && std::ranges::equal(signatures_, workingSignatures_,
        {}, &Signature::tile, &Signature::tile)) {
        lastUpdateReused_ = true;
        ++hitCount_;
        return geometry_;
    }
    lastUpdateReused_ = false;
    // Construct before publishing so any old returned geometry remains valid
    // if allocation or compilation fails, and all outputs have owning data.
    auto next = std::make_shared<const GroundChunkGeometry>(compileSignatures(
        tiles, std::span<const Signature>(workingSignatures_), artifact, optimize));
    signatures_.reserve(workingSignatures_.size());
    signatures_.swap(workingSignatures_);
    geometry_ = std::move(next);
    optimized_ = optimize;
    ++rebuildCount_;
    return geometry_;
}

std::size_t GroundChunkGeometryCache::capacityBytes() const noexcept
{
    std::size_t bytes = (signatures_.capacity() + workingSignatures_.capacity()) * sizeof(Signature);
    if (!geometry_) return bytes;
    bytes += sizeof(GroundChunkGeometry) + geometry_->chunks.capacity() * sizeof(GroundChunk);
    for (const auto& chunk : geometry_->chunks) {
        bytes += chunk.tiles.capacity() * sizeof(GroundChunkTile) +
            chunk.vertices.capacity() * sizeof(GroundChunkVertex) + chunk.indices.capacity() * sizeof(uint32_t);
    }
    return bytes;
}

void GroundChunkGeometryCache::invalidate() noexcept
{
    signatures_.clear();
    workingSignatures_.clear();
    geometry_.reset();
    lastUpdateReused_ = false;
}

} // namespace sokoban
