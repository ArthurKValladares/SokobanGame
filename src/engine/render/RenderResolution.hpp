#pragma once

#include <cstdint>

namespace sokoban {

struct PixelExtent {
    uint32_t width = 0;
    uint32_t height = 0;

    bool operator==(const PixelExtent&) const = default;
};

inline constexpr int minimumRenderScalePercent = 25;
inline constexpr int maximumRenderScalePercent = 100;

[[nodiscard]] int normalizedRenderScalePercent(int percent);
[[nodiscard]] int normalizedRenderScalePresetPercent(int percent);
[[nodiscard]] PixelExtent scaledRenderExtent(
    PixelExtent outputExtent,
    int renderScalePercent);

} // namespace sokoban
