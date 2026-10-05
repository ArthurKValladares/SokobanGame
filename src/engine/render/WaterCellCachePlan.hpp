#pragma once

#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sokoban {

// std430 and the compute push constants both use two 16-byte vectors.
struct alignas(16) WaterCellCachePlan {
    static constexpr uint32_t extent = 128;
    static constexpr uint32_t featureBytes = 32;
    std::array<int32_t, 4> origins { };
    std::array<uint32_t, 4> dimensions { extent, extent, 0, 0 };

    bool operator==(const WaterCellCachePlan&) const = default;
};
static_assert(sizeof(WaterCellCachePlan) == 32);
static_assert(WaterCellCachePlan::extent % 8 == 0);

[[nodiscard]] inline WaterCellCachePlan waterCellCachePlan(
    const RenderFrameData& frame,
    bool enabled = true)
{
    WaterCellCachePlan result;
    if (!enabled || frame.waterSurfaces.empty()) {
        return result;
    }
    const auto& bounds = frame.waterGridBounds;
    const float frequency =
        std::max(frame.waterRendering.rippleSpatialFrequency, 0.01f);
    const float time =
        frame.waterAnimationTimeSeconds * frame.waterRendering.rippleSpeed;
    const float x = (static_cast<float>(bounds.originX) +
                     static_cast<float>(bounds.width) * 0.5f) *
            frequency +
        time * 0.10f;
    const float y = (static_cast<float>(bounds.originY) +
                     static_cast<float>(bounds.height) * 0.5f) *
            frequency -
        time * 0.075f;
    const std::array<Vec2, 2> centers { Vec2 { x, y },
                                        Vec2 { -y + 2.31f, x - 1.73f } };
    for (std::size_t pattern = 0; pattern < centers.size(); ++pattern) {
        const float row = centers[pattern].y * 1.1547005f;
        const std::array<float, 2> lattice { centers[pattern].x - row * 0.5f,
                                             row };
        for (std::size_t axis = 0; axis < lattice.size(); ++axis) {
            // Stay well inside the exact integer range of FP32 and avoid
            // undefined integer conversion for malformed/extreme editor data.
            if (!std::isfinite(lattice[axis]) ||
                std::abs(lattice[axis]) > 1048576.0f) {
                return WaterCellCachePlan { };
            }
            // A 32-cell step amortizes rebuilds as the field drifts. The
            // 128-cell window retains at least 32 cells of margin on each side.
            result.origins[pattern * 2 + axis] =
                static_cast<int32_t>(std::floor(lattice[axis] / 32.0f)) * 32 -
                64;
        }
    }
    result.dimensions[3] = 1;
    return result;
}

} // namespace sokoban
