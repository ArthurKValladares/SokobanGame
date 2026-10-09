#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sokoban {

inline constexpr std::size_t groundTileTopPatchCapacity = 38;

// Vertices, facet normals, and material weights are serialized from the
// authored Blender cap. A triangle repeats its final vertex and weight.
struct GroundTileSurfacePatch {
    std::array<Vec3, 4> vertices {};
    std::array<float, 4> wallCoverage {};
    Vec3 normal {};
};

struct GroundTileCapSurface {
    std::array<GroundTileSurfacePatch, groundTileTopPatchCapacity> patches {};
    std::size_t count = 0;

    [[nodiscard]] std::span<const GroundTileSurfacePatch> faces() const noexcept
    {
        return { patches.data(), count };
    }
};

struct GroundTileNativeCapRecord {
    uint8_t variant = 0;
    uint8_t shapeIndex = 0;
    uint8_t exposedSideMask = 0;
    uint8_t concaveCorners = 0;
    GroundTileCapSurface surface {};
};

} // namespace sokoban
