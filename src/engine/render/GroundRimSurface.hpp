#pragma once

#include "engine/render/GroundRimGeometry.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace sokoban {

struct GroundRimSurfacePatch {
    std::array<Vec3, 4> vertices {};
    // Triangle-interpolated wall coverage: lowered border=1, flat interior=0.
    // Smooth the interpolated coverage in the material shader, not per vertex.
    std::array<float, 4> wallCoverage {};
    Vec3 normal {};
};

// Broad chipped facets surround a flat painted centre.
// A triangle is encoded as a quad with a degenerate second triangle, retaining
// the existing instanced face and shadow paths without a new mesh resource.
struct GroundRimSurface {
    static constexpr std::size_t capacity = GroundRimGeometry::capacity;
    std::array<GroundRimSurfacePatch, capacity> patches {};
    std::size_t count = 0;
};

[[nodiscard]] inline GroundRimProfile groundRimProfileForSurface(
    const RenderFrameData::Tile& tile) noexcept
{
    return {
        .exposedSides = tile.groundRimSides,
        .concaveCorners = tile.groundRimConcaveCorners,
        .width = tile.groundRimWidth,
        .depth = tile.groundRimDepth,
        .origin = tile.position,
    };
}

[[nodiscard]] inline bool hasGroundRimSurface(
    const RenderFrameData::Tile& tile) noexcept
{
    return tile.groundTop && !tile.model.isCube() && !tile.pickOnly &&
        !tile.isEditorPreview &&
        (tile.groundRimSides != 0 || tile.groundRimConcaveCorners != 0) &&
        groundRimProfileValid(groundRimProfileForSurface(tile));
}

[[nodiscard]] inline GroundRimSurface buildGroundRimSurface(
    const RenderFrameData::Tile& tile) noexcept
{
    GroundRimSurface surface;
    if (!hasGroundRimSurface(tile)) return surface;

    const GroundRimGeometry geometry = buildGroundRimGeometry(groundRimProfileForSurface(tile));
    for (std::size_t index = 0; index < geometry.count; ++index) {
        auto& patch = surface.patches[surface.count++];
        for (std::size_t vertex = 0; vertex < 4; ++vertex) {
            const Vec3 local = geometry.patches[index][vertex];
            patch.vertices[vertex] = {
                tile.position.x + local.x, tile.position.y + local.y,
                tile.baseElevation + tile.height - (1.0f - local.z),
            };
            patch.wallCoverage[vertex] = local.z < 1.0f ? 1.0f : 0.0f;
        }
        patch.normal = normalize(cross(patch.vertices[1] - patch.vertices[0],
            patch.vertices[2] - patch.vertices[0]));
    }
    return surface;
}

} // namespace sokoban
