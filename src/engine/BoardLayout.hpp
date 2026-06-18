#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <optional>

namespace sokoban {

struct BoardPixelLayout {
    Vec2 bottomLeft {};
    Vec2 size {};
    float tileSize = 0.0f;
};

[[nodiscard]] BoardPixelLayout calculateBoardPixelLayout(Vec2 viewportSizePixels, uint32_t levelWidth, uint32_t levelHeight);
[[nodiscard]] std::optional<GridPosition> pixelToGridPosition(
    Vec2 pixelPosition,
    const BoardPixelLayout& layout,
    uint32_t levelWidth,
    uint32_t levelHeight);

} // namespace sokoban
