#pragma once

#include "engine/Math.hpp"

namespace sokoban {

// Maps a position between two coordinate spaces that cover the same area,
// such as a window measured in points and in pixels.
[[nodiscard]] inline Vec2 scaleUiPosition(Vec2 position, Vec2 from, Vec2 to)
{
    return {
        from.x > 0.0f ? position.x * to.x / from.x : position.x,
        from.y > 0.0f ? position.y * to.y / from.y : position.y,
    };
}

} // namespace sokoban
