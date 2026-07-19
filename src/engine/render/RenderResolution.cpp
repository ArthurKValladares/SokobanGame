#include "engine/render/RenderResolution.hpp"

#include <algorithm>
#include <array>

namespace sokoban {
namespace {

constexpr std::array supportedPresets { 25, 50, 67, 75, 100 };

uint32_t scaledDimension(uint32_t value, int percent)
{
    if (value == 0) {
        return 0;
    }
    const uint64_t rounded = percent == 67
        ? (static_cast<uint64_t>(value) * 2U + 1U) / 3U
        : (static_cast<uint64_t>(value) * static_cast<uint64_t>(percent) + 50U) /
            100U;
    return std::max(static_cast<uint32_t>(rounded), 1U);
}

} // namespace

int normalizedRenderScalePercent(int percent)
{
    return std::clamp(
        percent, minimumRenderScalePercent, maximumRenderScalePercent);
}

int normalizedRenderScalePresetPercent(int percent)
{
    return std::ranges::find(supportedPresets, percent) != supportedPresets.end()
        ? percent
        : 100;
}

PixelExtent scaledRenderExtent(PixelExtent outputExtent, int renderScalePercent)
{
    const int percent = normalizedRenderScalePercent(renderScalePercent);
    return {
        .width = scaledDimension(outputExtent.width, percent),
        .height = scaledDimension(outputExtent.height, percent),
    };
}

} // namespace sokoban
