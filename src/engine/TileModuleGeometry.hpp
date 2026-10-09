#pragma once

#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <tuple>

namespace sokoban {

struct TileModule {
    std::size_t shapeIndex;
    uint32_t quarterTurns;
};

inline constexpr std::array<uint8_t, 6> tileModuleCanonicalMasks { 15, 11, 5, 3, 1, 0 };

[[nodiscard]] constexpr uint8_t rotateTileModuleMask(uint8_t mask, uint32_t turns)
{
    mask &= groundAllSides;
    turns &= 3;
    return static_cast<uint8_t>(((mask << turns) | (mask >> (4 - turns))) & groundAllSides);
}

// Six authored shapes cover every N/E/S/W exposure through cell-center rotation.
[[nodiscard]] constexpr TileModule tileModuleForMask(uint8_t mask)
{
    mask &= groundAllSides;
    for (std::size_t shape = 0; shape < tileModuleCanonicalMasks.size(); ++shape) {
        for (uint32_t turn = 0; turn < 4; ++turn) {
            const uint8_t rotated = rotateTileModuleMask(tileModuleCanonicalMasks[shape], turn);
            if (rotated == mask) return { shape, turn };
        }
    }
    return { 0, 0 };
}

[[nodiscard]] inline bool tileModuleCellLess(GridPosition3 left, GridPosition3 right)
{
    return std::tie(left.z, left.y, left.x) < std::tie(right.z, right.y, right.x);
}

[[nodiscard]] inline bool tileModuleHasNeighbor(GridPosition3 cell, GridPosition offset,
    std::span<const GridPosition3> sortedOccupied)
{
    if ((offset.x < 0 && cell.x == std::numeric_limits<int>::min()) ||
        (offset.x > 0 && cell.x == std::numeric_limits<int>::max()) ||
        (offset.y < 0 && cell.y == std::numeric_limits<int>::min()) ||
        (offset.y > 0 && cell.y == std::numeric_limits<int>::max())) return false;
    const GridPosition3 neighbor { cell.x + offset.x, cell.y + offset.y, cell.z };
    return std::binary_search(sortedOccupied.begin(), sortedOccupied.end(), neighbor, tileModuleCellLess);
}

// Occupancy comes from emitted cells, so filtered layers/screens leave their
// exposed boundary intact. A hover cell queries this list without joining it.
[[nodiscard]] inline uint8_t tileModuleExposedSides(
    GridPosition3 cell, std::span<const GridPosition3> sortedOccupied)
{
    constexpr std::array<GridPosition, 4> offsets {{ { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }};
    uint8_t exposed = groundAllSides;
    for (std::size_t side = 0; side < offsets.size(); ++side) {
        if (tileModuleHasNeighbor(cell, offsets[side], sortedOccupied)) {
            exposed &= static_cast<uint8_t>(~(1U << side));
        }
    }
    return exposed;
}

// NW/NE/SE/SW corner bits identify holes whose two incident cardinal cells
// exist but whose diagonal does not. Only ground caps use these corner wedges.
[[nodiscard]] inline uint8_t tileModuleConcaveCorners(GridPosition3 cell, uint8_t exposedSides,
    std::span<const GridPosition3> sortedOccupied)
{
    constexpr std::array<GridPosition, 4> offsets {{ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }};
    constexpr std::array<uint8_t, 4> incidentSides { 9, 3, 6, 12 };
    uint8_t corners = 0;
    for (std::size_t corner = 0; corner < offsets.size(); ++corner) {
        if ((exposedSides & incidentSides[corner]) == 0 &&
            !tileModuleHasNeighbor(cell, offsets[corner], sortedOccupied)) {
            corners |= static_cast<uint8_t>(1U << corner);
        }
    }
    return corners;
}

} // namespace sokoban
