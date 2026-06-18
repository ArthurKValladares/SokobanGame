#include "engine/BoardLayout.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

BoardPixelLayout calculateBoardPixelLayout(Vec2 viewportSizePixels, uint32_t levelWidth, uint32_t levelHeight)
{
    if (viewportSizePixels.x <= 0.0f || viewportSizePixels.y <= 0.0f || levelWidth == 0 || levelHeight == 0) {
        return {};
    }

    const float tileSize = std::min(
        viewportSizePixels.x / static_cast<float>(levelWidth),
        viewportSizePixels.y / static_cast<float>(levelHeight));
    const Vec2 boardSize {
        tileSize * static_cast<float>(levelWidth),
        tileSize * static_cast<float>(levelHeight),
    };

    return {
        .bottomLeft = {
            (viewportSizePixels.x - boardSize.x) * 0.5f,
            (viewportSizePixels.y + boardSize.y) * 0.5f,
        },
        .size = boardSize,
        .tileSize = tileSize,
    };
}

std::optional<GridPosition> pixelToGridPosition(
    Vec2 pixelPosition,
    const BoardPixelLayout& layout,
    uint32_t levelWidth,
    uint32_t levelHeight)
{
    if (layout.tileSize <= 0.0f || levelWidth == 0 || levelHeight == 0) {
        return std::nullopt;
    }

    const float localX = pixelPosition.x - layout.bottomLeft.x;
    const float localY = pixelPosition.y - (layout.bottomLeft.y - layout.size.y);
    if (localX < 0.0f || localY < 0.0f || localX >= layout.size.x || localY >= layout.size.y) {
        return std::nullopt;
    }

    const GridPosition position {
        static_cast<int>(std::floor(localX / layout.tileSize)),
        static_cast<int>(std::floor(localY / layout.tileSize)),
    };
    if (position.x < 0 || position.y < 0 || position.x >= static_cast<int>(levelWidth) || position.y >= static_cast<int>(levelHeight)) {
        return std::nullopt;
    }

    return position;
}

} // namespace sokoban
