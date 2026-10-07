#include "engine/GroundGeometry.hpp"
#include "engine/render/GroundMeshGeometry.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace sokoban {
namespace {

constexpr std::array<GridPosition, 4> sideOffsets {{
    { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
}};
constexpr std::array<GridPosition, 4> cornerOffsets {{
    { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 },
}};
constexpr std::array<uint8_t, 4> cornerIncidentSides {{
    groundNorthSide | groundWestSide, groundNorthSide | groundEastSide,
    groundSouthSide | groundEastSide, groundSouthSide | groundWestSide,
}};

[[nodiscard]] bool eligibleGround(
    const RenderFrameData::Tile& tile,
    const AssetManifest& manifest) noexcept
{
    if (!tile.groundTop || tile.isEditorPreview || tile.pickOnly ||
        tile.blurBehind || tile.color.w != 1.0f ||
        (tile.effect != RenderSurfaceEffect::GroundSplat &&
            tile.effect != RenderSurfaceEffect::Standard) ||
        tile.modelTransform || tile.modelRotationQuarterTurns != 0 ||
        tile.modelRotationOffsetRadians != 0.0f ||
        !tile.animation.isNone() || !tile.animationFallback.isNone() ||
        tile.animationInstanceId != 0 || tile.renderableId != 0 ||
        tile.size != Vec2 { 1.0f, 1.0f } || tile.height != 1.0f ||
        tile.position != Vec2 {
            static_cast<float>(tile.cell.x),
            static_cast<float>(tile.cell.y),
        } || tile.baseElevation != static_cast<float>(tile.cell.z) ||
        tile.model.isCube() || tile.model.index() >= manifest.models().size()) {
        return false;
    }
    return isProcessableGroundRockModel(manifest.models()[tile.model.index()]);
}

[[nodiscard]] std::size_t cellHash(GridPosition3 cell) noexcept
{
    // Unsigned arithmetic makes negative overworld origins as ordinary as
    // positive cells, and incorporates the layer before the final avalanche.
    uint64_t hash = static_cast<uint32_t>(cell.x) * 0x9e3779b185ebca87ULL;
    hash ^= static_cast<uint32_t>(cell.y) * 0xc2b2ae3d27d4eb4fULL;
    hash ^= static_cast<uint32_t>(cell.z) * 0x165667b19e3779f9ULL;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    return static_cast<std::size_t>(hash);
}

[[nodiscard]] std::optional<GridPosition3> offsetCell(
    GridPosition3 cell, GridPosition offset) noexcept
{
    if ((offset.x < 0 && cell.x == std::numeric_limits<int>::min()) ||
        (offset.x > 0 && cell.x == std::numeric_limits<int>::max()) ||
        (offset.y < 0 && cell.y == std::numeric_limits<int>::min()) ||
        (offset.y > 0 && cell.y == std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return GridPosition3 { cell.x + offset.x, cell.y + offset.y, cell.z };
}

} // namespace

std::size_t GroundGeometryCache::capacityBytes() const noexcept
{
    return (signatures_.capacity() + workingSignatures_.capacity()) * sizeof(Signature) +
        adjacency_.capacity() * sizeof(uint32_t) +
        processed_.cells.capacity() * sizeof(ProcessedGroundCell);
}

void GroundGeometryCache::invalidate() noexcept
{
    valid_ = false;
    lastResultReused_ = false;
    signatures_.clear();
    workingSignatures_.clear();
    adjacency_.clear();
    processed_.cells.clear();
    processed_.exposedSideCount = 0;
    processed_.convexCornerCount = 0;
    processed_.concaveCornerCount = 0;
}

std::size_t GroundGeometryCache::slotFor(GridPosition3 cell) const noexcept
{
    std::size_t slot = cellHash(cell) & (adjacency_.size() - 1);
    while (adjacency_[slot] != 0 && processed_.cells[adjacency_[slot] - 1].cell != cell) {
        slot = (slot + 1) & (adjacency_.size() - 1);
    }
    return slot;
}

const ProcessedGroundCell* GroundGeometryCache::cellAt(GridPosition3 cell) const noexcept
{
    if (adjacency_.empty()) return nullptr;
    const uint32_t index = adjacency_[slotFor(cell)];
    return index == 0 ? nullptr : &processed_.cells[index - 1];
}

void GroundGeometryCache::update(
    std::span<const RenderFrameData::Tile> tiles,
    const AssetManifest& manifest)
{
    if (tiles.size() >= std::numeric_limits<uint32_t>::max()) {
        invalidate();
        return;
    }
    workingSignatures_.clear();
    for (const auto& tile : tiles) {
        if (eligibleGround(tile, manifest)) {
            workingSignatures_.push_back({ tile.cell, tile.model });
        }
    }
    std::ranges::sort(workingSignatures_, [](const Signature& left, const Signature& right) {
        return std::tie(left.cell.z, left.cell.y, left.cell.x, left.model.value) <
            std::tie(right.cell.z, right.cell.y, right.cell.x, right.model.value);
    });
    lastResultReused_ = valid_ && workingSignatures_ == signatures_;
    if (lastResultReused_) {
        ++hitCount_;
        return;
    }
    // Never accept partially rebuilt storage if an allocation throws. The
    // next request must complete a fresh compile before the cache can hit.
    valid_ = false;
    signatures_ = workingSignatures_;
    processed_.cells.clear();
    processed_.cells.reserve(signatures_.size());
    processed_.exposedSideCount = 0;
    processed_.convexCornerCount = 0;
    processed_.concaveCornerCount = 0;
    for (const auto& signature : signatures_) {
        // A duplicate render placement must not create duplicate boundary
        // descriptions. The lowest model identity deterministically supplies
        // the neighbour readiness contract when models overlap at one cell.
        if (!processed_.cells.empty() && processed_.cells.back().cell == signature.cell) {
            continue;
        }
        processed_.cells.push_back({ .cell = signature.cell, .model = signature.model });
    }
    if (processed_.cells.empty()) {
        adjacency_.clear();
        valid_ = true;
        ++rebuildCount_;
        return;
    }
    adjacency_.resize(std::bit_ceil(processed_.cells.size() * 2));
    std::ranges::fill(adjacency_, uint32_t { 0 });
    for (std::size_t index = 0; index < processed_.cells.size(); ++index) {
        adjacency_[slotFor(processed_.cells[index].cell)] = static_cast<uint32_t>(index + 1);
    }
    for (auto& cell : processed_.cells) {
        for (std::size_t side = 0; side < sideOffsets.size(); ++side) {
            const auto neighborCell = offsetCell(cell.cell, sideOffsets[side]);
            const auto* neighbor = neighborCell ? cellAt(*neighborCell) : nullptr;
            if (neighbor) {
                cell.groundSideMask &= static_cast<uint8_t>(~(1U << side));
                cell.sideNeighbors[side] = neighbor->model;
            }
        }
        for (std::size_t corner = 0; corner < cornerOffsets.size(); ++corner) {
            const uint8_t bit = static_cast<uint8_t>(1U << corner);
            const auto neighborCell = offsetCell(cell.cell, cornerOffsets[corner]);
            const auto* diagonal = neighborCell ? cellAt(*neighborCell) : nullptr;
            if (diagonal) {
                cell.diagonalNeighbors |= bit;
                cell.diagonalNeighborModels[corner] = diagonal->model;
            }
            const uint8_t exposedIncidentSides = static_cast<uint8_t>(
                cell.groundSideMask & cornerIncidentSides[corner]);
            if (exposedIncidentSides == cornerIncidentSides[corner]) {
                cell.convexCorners |= bit;
            } else if (exposedIncidentSides == 0 && !diagonal) {
                cell.concaveCorners |= bit;
            }
        }
        processed_.exposedSideCount += static_cast<uint32_t>(std::popcount(cell.groundSideMask));
        processed_.convexCornerCount += static_cast<uint32_t>(std::popcount(cell.convexCorners));
        processed_.concaveCornerCount += static_cast<uint32_t>(std::popcount(cell.concaveCorners));
    }
    valid_ = true;
    ++rebuildCount_;
}

ProcessedGround compileGroundGeometry(
    std::span<const RenderFrameData::Tile> tiles,
    const AssetManifest& manifest)
{
    GroundGeometryCache cache;
    cache.update(tiles, manifest);
    return std::move(cache.processed_);
}

void processGroundGeometry(
    std::span<RenderFrameData::Tile> tiles,
    const AssetManifest& manifest,
    FrameArena* arena,
    GroundGeometryCache* cache)
{
    std::size_t eligibleCount = 0;
    for (auto& tile : tiles) {
        tile.groundSideMask = groundAllSides;
        tile.groundSideNeighbors = {};
        tile.groundDiagonalNeighbors = {};
        tile.groundRimSides = groundAllSides;
        tile.groundRimConcaveCorners = 0;
        tile.groundGeometryEligible = false;
        eligibleCount += eligibleGround(tile, manifest) ? 1U : 0U;
    }
    if (cache) {
        cache->update(tiles, manifest);
        for (auto& tile : tiles) {
            if (!eligibleGround(tile, manifest)) continue;
            if (const auto* cell = cache->cellAt(tile.cell)) {
                tile.groundSideMask = cell->groundSideMask;
                tile.groundSideNeighbors = cell->sideNeighbors;
                tile.groundDiagonalNeighbors = cell->diagonalNeighborModels;
                tile.groundRimSides = cell->groundSideMask;
                tile.groundRimConcaveCorners = cell->concaveCorners;
                tile.groundGeometryEligible = true;
            }
        }
        return;
    }
    if (tiles.size() >= std::numeric_limits<uint32_t>::max()) {
        return;
    }
    if (eligibleCount < 2) {
        for (auto& tile : tiles) tile.groundGeometryEligible = eligibleGround(tile, manifest);
        return;
    }

    // Half-full open-addressing table, storing tile index+1 (zero is empty).
    // At frame capacity this is 128 KiB, inside the frame's existing 2 MiB
    // scratch allowance. Insertion and four neighbour lookups are expected O(n).
    const std::size_t capacity = std::bit_ceil(eligibleCount * 2);
    std::vector<uint32_t> owningTable;
    uint32_t* table = nullptr;
    if (arena) {
        table = arena->allocateUninitialized<uint32_t>(capacity);
        if (!table) return;
        std::uninitialized_fill_n(table, capacity, uint32_t { 0 });
    } else {
        owningTable.resize(capacity);
        table = owningTable.data();
    }
    const auto slotFor = [&](GridPosition3 cell) {
        std::size_t slot = cellHash(cell) & (capacity - 1);
        while (table[slot] != 0 && tiles[table[slot] - 1].cell != cell) {
            slot = (slot + 1) & (capacity - 1);
        }
        return slot;
    };
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const auto& tile = tiles[index];
        if (eligibleGround(tile, manifest)) {
            const std::size_t slot = slotFor(tile.cell);
            if (table[slot] == 0 || tile.model.value < tiles[table[slot] - 1].model.value) {
                table[slot] = static_cast<uint32_t>(index + 1);
            }
        }
    }

    for (auto& tile : tiles) {
        if (!eligibleGround(tile, manifest)) continue;
        tile.groundGeometryEligible = true;
        for (std::size_t side = 0; side < sideOffsets.size(); ++side) {
            const auto neighbor = offsetCell(tile.cell, sideOffsets[side]);
            if (!neighbor) continue;
            const uint32_t neighborIndex = table[slotFor(*neighbor)];
            if (neighborIndex != 0) {
                tile.groundSideMask &= static_cast<uint8_t>(~(1U << side));
                tile.groundSideNeighbors[side] = tiles[neighborIndex - 1].model;
            }
        }
        tile.groundRimSides = tile.groundSideMask;
        for (std::size_t corner = 0; corner < cornerOffsets.size(); ++corner) {
            const auto neighborCell = offsetCell(tile.cell, cornerOffsets[corner]);
            const uint32_t diagonalIndex = neighborCell ? table[slotFor(*neighborCell)] : 0;
            if (diagonalIndex != 0) {
                tile.groundDiagonalNeighbors[corner] = tiles[diagonalIndex - 1].model;
            } else if ((tile.groundSideMask & cornerIncidentSides[corner]) == 0) {
                tile.groundRimConcaveCorners |= static_cast<uint8_t>(1U << corner);
            }
        }
    }
}

} // namespace sokoban
