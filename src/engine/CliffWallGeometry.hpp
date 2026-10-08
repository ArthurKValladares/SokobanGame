#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace sokoban {

inline constexpr std::array<std::array<std::string_view, 2>, 6> cliffWallModelNames {{
    { "CliffWallIslandA", "CliffWallIslandB" },
    { "CliffWallEndA", "CliffWallEndB" },
    { "CliffWallStripA", "CliffWallStripB" },
    { "CliffWallCornerA", "CliffWallCornerB" },
    { "CliffWallEdgeA", "CliffWallEdgeB" },
    { "CliffWallInteriorA", "CliffWallInteriorB" },
}};

inline constexpr std::array<std::array<float, 13>, 2> cliffWallTopInsetProfiles {{
    { 0, 0, .0429f, .066f, .05808f, .07788f, .06864f, .0594f, .07392f, .05478f, .03762f, 0, 0 },
    { 0, 0, .05412f, .04026f, .07128f, .05808f, .07986f, .06468f, .05544f, .06864f, .04884f, 0, 0 },
}};
inline constexpr std::size_t cliffWallTopBoundaryCount = 48;
inline constexpr std::size_t cliffWallTopPatchCount = 24;
inline constexpr float cliffWallCornerTaperRadius = 1.0f / 6.0f;
inline constexpr std::array<float, 2> cliffWallCornerTaperAmounts { .0605f, .066f };

// Match the native convex-corner taper. Shared or concave corners stay fixed:
// only a corner with both incident sides exposed can move inward. Weights use
// the original point, and the disjoint supports leave neighboring joins intact.
[[nodiscard]] constexpr Vec2 cliffWallConvexCornerTaper(
    Vec2 point, uint8_t exposedSideMask, uint32_t variant, float height = 1.0f)
{
    constexpr std::array<Vec2, 4> corners {{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }};
    constexpr std::array<uint8_t, 4> cornerMasks { 9, 3, 6, 12 };
    const float amount = cliffWallCornerTaperAmounts[variant % cliffWallCornerTaperAmounts.size()] *
        std::clamp(height, 0.0f, 1.0f);
    Vec2 tapered = point;
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        if ((exposedSideMask & cornerMasks[corner]) != cornerMasks[corner]) continue;
        const Vec2 origin = corners[corner];
        const float dx = std::max(0.0f, origin.x == 0 ? point.x : 1.0f - point.x);
        const float dy = std::max(0.0f, origin.y == 0 ? point.y : 1.0f - point.y);
        const float shift = amount * std::max(0.0f, 1.0f - dx / cliffWallCornerTaperRadius) *
            std::max(0.0f, 1.0f - dy / cliffWallCornerTaperRadius);
        tapered.x += origin.x == 0 ? shift : -shift;
        tapered.y += origin.y == 0 ? shift : -shift;
    }
    return tapered;
}

// The native upper boundary, in local unit-cell XY. Side samples run
// North/East/South/West with no duplicated corner; hidden sides stay straight.
[[nodiscard]] constexpr std::array<Vec2, cliffWallTopBoundaryCount> cliffWallTopBoundary(
    uint8_t exposedSideMask, uint32_t variant)
{
    std::array<Vec2, cliffWallTopBoundaryCount> points {};
    const auto& profile = cliffWallTopInsetProfiles[variant % cliffWallTopInsetProfiles.size()];
    for (std::size_t side = 0; side < 4; ++side) {
        for (std::size_t sample = 0; sample < 12; ++sample) {
            const float u = static_cast<float>(sample) / 12.0f;
            const float depth = (exposedSideMask & (1U << side)) != 0 ? profile[sample] : 0.0f;
            const Vec2 point = side == 0 ? Vec2 { u, depth }
                : side == 1 ? Vec2 { 1.0f - depth, u }
                : side == 2 ? Vec2 { 1.0f - u, 1.0f - depth }
                : Vec2 { depth, 1.0f - u };
            points[side * 12 + sample] = cliffWallConvexCornerTaper(point, exposedSideMask, variant);
        }
    }
    return points;
}

// The same planar fan is used by drawing, continuous paint picking and
// shadows. Visual scale/elevation affect these points, never the logical cell.
[[nodiscard]] std::array<std::array<Vec3, 4>, cliffWallTopPatchCount> cliffWallTopPatches(
    const RenderFrameData::Tile& tile);

struct CliffWallModule {
    std::size_t shapeIndex;
    uint32_t quarterTurns;
};

// Native unit-cell modules rotate about the cell center, North toward East.
[[nodiscard]] constexpr CliffWallModule cliffWallModuleForMask(uint8_t mask)
{
    constexpr std::array<uint8_t, 6> canonicalMasks { 15, 11, 5, 3, 1, 0 };
    mask &= groundAllSides;
    for (std::size_t shape = 0; shape < canonicalMasks.size(); ++shape) {
        for (uint32_t turn = 0; turn < 4; ++turn) {
            const uint8_t rotated = static_cast<uint8_t>(
                ((canonicalMasks[shape] << turn) |
                    (canonicalMasks[shape] >> (4 - turn))) & groundAllSides);
            if (rotated == mask) return { shape, turn };
        }
    }
    return { 0, 0 };
}

// Resolve authored and hover-preview cliffs against emitted, same-height
// cliff cells. Their body stays undeformed and their flat top stays splattable.
void processCliffWallGeometry(
    std::span<RenderFrameData::Tile> tiles, const AssetManifest& manifest);

} // namespace sokoban
