#include "engine/GroundGeometry.hpp"
#include "engine/render/GroundMeshGeometry.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <memory>
#include <vector>

namespace sokoban {
namespace {

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

} // namespace

void processGroundGeometry(
    std::span<RenderFrameData::Tile> tiles,
    const AssetManifest& manifest,
    FrameArena* arena)
{
    std::size_t eligibleCount = 0;
    for (auto& tile : tiles) {
        tile.groundSideMask = groundAllSides;
        tile.groundSideNeighbors = {};
        eligibleCount += eligibleGround(tile, manifest) ? 1U : 0U;
    }
    if (eligibleCount < 2 || tiles.size() >= std::numeric_limits<uint32_t>::max()) {
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
            table[slotFor(tile.cell)] = static_cast<uint32_t>(index + 1);
        }
    }

    constexpr std::array<GridPosition, 4> offsets {{
        { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
    }};
    // Only table members participate; excluded tiles keep all four sides.
    for (std::size_t slot = 0; slot < capacity; ++slot) {
        if (!table[slot]) continue;
        auto& tile = tiles[table[slot] - 1];
        for (std::size_t side = 0; side < offsets.size(); ++side) {
            const auto offset = offsets[side];
            // Protect the neighbour addition even for malicious authored cells.
            if ((offset.x < 0 && tile.cell.x == std::numeric_limits<int>::min()) ||
                (offset.x > 0 && tile.cell.x == std::numeric_limits<int>::max()) ||
                (offset.y < 0 && tile.cell.y == std::numeric_limits<int>::min()) ||
                (offset.y > 0 && tile.cell.y == std::numeric_limits<int>::max())) {
                continue;
            }
            const GridPosition3 neighbor {
                tile.cell.x + offset.x, tile.cell.y + offset.y, tile.cell.z,
            };
            const uint32_t neighborIndex = table[slotFor(neighbor)];
            if (neighborIndex != 0) {
                tile.groundSideMask &= static_cast<uint8_t>(~(1U << side));
                tile.groundSideNeighbors[side] = tiles[neighborIndex - 1].model;
            }
        }
    }
}

} // namespace sokoban
