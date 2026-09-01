#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>

namespace sokoban {

// The arithmetic that several scene draws were each writing out for
// themselves, in one place and reachable from a test.
//
// GpuDrawInstance is a fixed 256-byte block whose lanes mean different things
// depending on which pass claims them, so there is no single "base instance"
// every draw can start from - water and the mirror effect reuse the same lanes
// for border parameters and scanline constants, and building them from a scene
// base would say something false about what they contain. What the scene draws
// genuinely share is smaller and more specific: how the frame's light is packed
// into three lanes, when a grid line counts as present, and how four world
// corners become four vec4s. Those are here; nothing else is.
//
// Vulkan-free, like the numbers it computes.

// The lanes that carry the frame's directional and ambient light.
//
// Ambient is split one channel per lane because the vec4s it shares are
// already full: red rides with the surface normal, green with the sun
// direction, blue with the sun radiance. `ambientRed` is returned on its own
// rather than as a whole lane because the three xyz components it joins differ
// by caller - a face sends its normal, a model sends zeros.
struct SunAmbientLanes {
    float ambientRed = 0.0f;
    Vec4 sunDirectionAndAmbientGreen {};
    Vec4 sunRadianceAndAmbientBlue {};
};

[[nodiscard]] inline SunAmbientLanes sunAmbientLanes(
    const RenderFrameData::Lighting& lighting)
{
    const Vec3 sunRadiance {
        lighting.sun.color.x * lighting.sun.intensity,
        lighting.sun.color.y * lighting.sun.intensity,
        lighting.sun.color.z * lighting.sun.intensity,
    };
    const Vec3 ambientRadiance {
        lighting.ambient.color.x * lighting.ambient.intensity,
        lighting.ambient.color.y * lighting.ambient.intensity,
        lighting.ambient.color.z * lighting.ambient.intensity,
    };
    return SunAmbientLanes {
        .ambientRed = ambientRadiance.x,
        .sunDirectionAndAmbientGreen = {
            lighting.sun.direction.x,
            lighting.sun.direction.y,
            lighting.sun.direction.z,
            ambientRadiance.y,
        },
        .sunRadianceAndAmbientBlue = {
            sunRadiance.x,
            sunRadiance.y,
            sunRadiance.z,
            ambientRadiance.z,
        },
    };
}

// The grid line's width, or zero when the grid is not actually drawable.
//
// The shader treats a non-zero width as "draw the grid", so every one of these
// four conditions has to be checked here rather than left to the shader: a
// transparent grid colour, a zero width, or a degenerate cell in either axis
// all mean no grid. Written out once because it was previously two copies that
// had to agree, and a grid that renders on tiles but not on ground - or the
// reverse - is the kind of difference that reads as an art bug.
[[nodiscard]] inline float gridLineWidthOrZero(
    Vec4 gridColor, float gridLineWidth, Vec2 gridSize)
{
    const bool drawable = gridColor.w > 0.0f && gridLineWidth > 0.0f
        && gridSize.x > 0.0f && gridSize.y > 0.0f;
    return drawable ? gridLineWidth : 0.0f;
}

// The shadow lane for a scene face: whether shadows are on, how strongly they
// darken, the depth bias, and the grid width sharing the spare slot.
//
// Only for face draws. The model path deliberately computes a different
// opacity - it folds in `modelShadowReceive`, which tiles do not have - and
// the mirror-energy model overwrites this lane entirely with scanline
// constants. Those stay written out where they are, because they are one site
// each and pretending they are this function would hide a real difference.
[[nodiscard]] inline Vec4 faceShadowOptions(
    const RenderFrameData::Lighting& lighting,
    Vec4 gridColor,
    float gridLineWidth,
    Vec2 gridSize)
{
    return Vec4 {
        lighting.shadows.enabled ? 1.0f : 0.0f,
        std::clamp(lighting.shadows.opacity, 0.0f, 1.0f),
        std::max(lighting.shadows.bias, 0.0f),
        gridLineWidthOrZero(gridColor, gridLineWidth, gridSize),
    };
}

// Four world corners as four vec4s, with `quadSpace` in w.
//
// w tells the vertex shader whether these corners still need projecting:
// worldSpaceQuad for scene geometry, clipSpaceQuad for the UI and the
// top-down board, which are positioned already. See VulkanRenderConstants.hpp
// for both constants - they are not named here so this header stays a leaf.
[[nodiscard]] inline std::array<Vec4, 4> quadVertices(
    const std::array<Vec3, 4>& corners, float quadSpace)
{
    return std::array<Vec4, 4> {
        Vec4 { corners[0].x, corners[0].y, corners[0].z, quadSpace },
        Vec4 { corners[1].x, corners[1].y, corners[1].z, quadSpace },
        Vec4 { corners[2].x, corners[2].y, corners[2].z, quadSpace },
        Vec4 { corners[3].x, corners[3].y, corners[3].z, quadSpace },
    };
}

} // namespace sokoban
