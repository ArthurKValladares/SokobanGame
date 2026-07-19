#pragma once

#include <cstdint>

namespace sokoban {

struct PixelExtent {
    uint32_t width = 0;
    uint32_t height = 0;

    bool operator==(const PixelExtent&) const = default;
};

[[nodiscard]] int normalizedRenderScalePercent(int percent);
[[nodiscard]] PixelExtent scaledRenderExtent(
    PixelExtent outputExtent,
    int renderScalePercent);

} // namespace sokoban
