#pragma once

#include "engine/Math.hpp"

#include <algorithm>
#include <cstdint>

namespace sokoban {

// How authored border texels become target-space units. UI normally preserves
// source pixels exactly. World-space effects do not have a pixel scale, so
// they can fit the source texture's full width to the target width and use
// that same uniform scale for all four corners and edge bands.
enum class NineSliceScaleMode : uint8_t {
    PreserveSourcePixels,
    FitTargetWidth,
};

// Reusable 3-by-3 texture slicing. Borders are normalized source-texture
// insets in left, top, right, bottom order. Keeping the description in render
// code rather than in particles or UI lets every textured-quad path share the
// same contract and shader implementation.
struct NineSlice {
    Vec4 sourceBorders {};
    NineSliceScaleMode scaleMode =
        NineSliceScaleMode::PreserveSourcePixels;
    // Target units per source texel in PreserveSourcePixels mode. UI target
    // units are pixels, so 1 keeps authored corners at their native size.
    float sourcePixelScale = 1.0f;

    [[nodiscard]] constexpr bool enabled() const
    {
        return sourceBorders.x > 0.0f || sourceBorders.y > 0.0f ||
            sourceBorders.z > 0.0f || sourceBorders.w > 0.0f;
    }

    [[nodiscard]] static constexpr NineSlice preserveSourcePixels(
        Vec4 sourceBorders,
        float scale = 1.0f)
    {
        return {
            .sourceBorders = sourceBorders,
            .scaleMode = NineSliceScaleMode::PreserveSourcePixels,
            .sourcePixelScale = scale,
        };
    }

    [[nodiscard]] static constexpr NineSlice fitTargetWidth(
        Vec4 sourceBorders)
    {
        return {
            .sourceBorders = sourceBorders,
            .scaleMode = NineSliceScaleMode::FitTargetWidth,
        };
    }

    [[nodiscard]] static constexpr NineSlice symmetricFitTargetWidth(
        Vec2 sourceBorder)
    {
        return fitTargetWidth({
            sourceBorder.x,
            sourceBorder.y,
            sourceBorder.x,
            sourceBorder.y,
        });
    }

    friend constexpr bool operator==(NineSlice, NineSlice) = default;
};

// Exact lanes consumed by NineSlice.glsl. Keeping the packing beside the
// public component prevents scene and UI recorders from inventing subtly
// different layouts for the same shader helper.
struct NineSliceDrawData {
    Vec4 sourceBorders {};
    // xy target extent; z is target units per source texel. A zero z selects
    // FitTargetWidth in the shader. w is reserved.
    Vec4 targetExtentAndSourcePixelScale {};
};

[[nodiscard]] inline NineSlice sanitizedNineSlice(NineSlice slice)
{
    slice.sourceBorders = {
        std::clamp(slice.sourceBorders.x, 0.0f, 0.499f),
        std::clamp(slice.sourceBorders.y, 0.0f, 0.499f),
        std::clamp(slice.sourceBorders.z, 0.0f, 0.499f),
        std::clamp(slice.sourceBorders.w, 0.0f, 0.499f),
    };
    slice.sourcePixelScale = std::max(slice.sourcePixelScale, 0.0001f);
    return slice;
}

[[nodiscard]] inline NineSliceDrawData nineSliceDrawData(
    NineSlice slice,
    Vec2 targetExtent)
{
    slice = sanitizedNineSlice(slice);
    return {
        .sourceBorders = slice.sourceBorders,
        .targetExtentAndSourcePixelScale = {
            std::max(targetExtent.x, 0.0001f),
            std::max(targetExtent.y, 0.0001f),
            slice.scaleMode == NineSliceScaleMode::PreserveSourcePixels
                ? slice.sourcePixelScale
                : 0.0f,
            0.0f,
        },
    };
}

} // namespace sokoban
