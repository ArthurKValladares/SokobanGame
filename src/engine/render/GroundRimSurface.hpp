#pragma once

#include "engine/render/GroundRimGeometry.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace sokoban {

struct GroundRimSurfacePatch {
    std::array<Vec3, 4> vertices {};
    Vec3 normal {};
};

// Three bands on each axis, with corner cells split along the chamfer kink.
// A triangle is encoded as a quad with a degenerate second triangle, retaining
// the existing instanced face and shadow paths without a new mesh resource.
struct GroundRimSurface {
    static constexpr std::size_t capacity = 18;
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

    const GroundRimProfile profile = groundRimProfileForSurface(tile);
    const std::array<float, 4> knots {
        0.0f, profile.width, 1.0f - profile.width, 1.0f,
    };
    const auto point = [&](float x, float y) {
        return Vec3 {
            tile.position.x + x,
            tile.position.y + y,
            tile.baseElevation + tile.height -
                sampleGroundRim({ x, y }, profile).drop,
        };
    };
    const auto append = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        surface.patches[surface.count++] = {
            .vertices = { a, b, c, d },
            .normal = normalize(cross(b - a, c - a)),
        };
    };
    for (std::size_t y = 0; y < 3; ++y) {
        for (std::size_t x = 0; x < 3; ++x) {
            if (knots[x] == knots[x + 1] || knots[y] == knots[y + 1]) continue;
            const Vec3 a = point(knots[x], knots[y]);
            const Vec3 b = point(knots[x + 1], knots[y]);
            const Vec3 c = point(knots[x + 1], knots[y + 1]);
            const Vec3 d = point(knots[x], knots[y + 1]);
            if (std::abs((a.z + c.z) - (b.z + d.z)) < 0.000001f) {
                append(a, b, c, d);
                continue;
            }
            // max(edge ramps) at a convex corner and min(ramps) at a
            // concave corner use opposite diagonals. Sampling the centre
            // chooses the one whose interpolation reproduces the shared field.
            const float centreHeight = point(
                (knots[x] + knots[x + 1]) * 0.5f,
                (knots[y] + knots[y + 1]) * 0.5f).z;
            if (std::abs(centreHeight - (a.z + c.z) * 0.5f) <=
                std::abs(centreHeight - (b.z + d.z) * 0.5f)) {
                append(a, b, c, c);
                append(a, c, d, d);
            } else {
                append(a, b, d, d);
                append(b, c, d, d);
            }
        }
    }
    return surface;
}

} // namespace sokoban
