#pragma once

#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace sokoban {

// Must match GroundDebris.glsl. These are full excursion limits, including
// both sine components, used for conservative visibility and shadow bounds.
inline constexpr float groundDebrisHorizontalAmplitude = 0.020f;
inline constexpr float groundDebrisVerticalAmplitude = 0.060f;

[[nodiscard]] inline bool hasAnimatedGroundDebris(const RenderFrameData::Tile& tile) noexcept
{
    // Interior undersides have no detached fragments.
    return tile.groundModule && tile.groundModuleBottom && tile.groundModuleSideMask != 0;
}

// One instance seed survives sorting, batching and frame reconstruction. The
// bit hash avoids periodic sine hashes and also distinguishes layered tiles.
[[nodiscard]] inline Vec4 groundDebrisAnimationData(
    const RenderFrameData::Tile& tile, float timeSeconds) noexcept
{
    if (!hasAnimatedGroundDebris(tile)) return {};
    uint32_t seed = 0x6d2b79f5U;
    const auto mix = [&seed](uint32_t value) {
        seed ^= value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    };
    mix(static_cast<uint32_t>(tile.cell.x));
    mix(static_cast<uint32_t>(tile.cell.y));
    mix(static_cast<uint32_t>(tile.cell.z));
    mix(std::bit_cast<uint32_t>(tile.position.x));
    mix(std::bit_cast<uint32_t>(tile.position.y));
    mix(std::bit_cast<uint32_t>(tile.baseElevation));
    mix(tile.groundRockVariant);
    mix(tile.modelRotationQuarterTurns);
    // Keep 24 bits so the GPU receives a stable exactly representable seed.
    return { std::isfinite(timeSeconds) ? std::max(timeSeconds, 0.0f) : 0.0f,
        static_cast<float>(seed & 0xffffffU) / 16777216.0f, 1.0f, 0.0f };
}

} // namespace sokoban
