#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sokoban {

// Two one-based texture handles fit exactly in a float's 24-bit integer
// precision. Ground splats use these pairs to keep the shared 256-byte draw
// ABI while carrying four albedo, normal and ORM layers. Mirrors GLSL's
// unpackGroundTextureHandles; zero still means an absent companion map.
inline constexpr uint32_t groundTextureHandleBits = 12;
inline constexpr uint32_t groundTextureHandleMask = (1U << groundTextureHandleBits) - 1;
[[nodiscard]] constexpr float packGroundTextureHandles(RenderTexture low, RenderTexture high)
{
    if (low.value > groundTextureHandleMask || high.value > groundTextureHandleMask) {
        throw std::out_of_range("Ground texture handle exceeds terrain packing capacity");
    }
    return static_cast<float>(low.value | (high.value << groundTextureHandleBits));
}

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

// The frame-wide lanes of the cloud light, as SceneFrame.glsl declares them
// and CloudLight.glsl reads them.
//
// The pattern is laid out along the sun's rays: every shader evaluates it
// where the ray through its own position crosses height zero, so a shaft in
// the air and the pool of light under it are one piece. `projection.xy` is
// what gets a position there, per unit of its height. A sun close to the
// horizon would stretch that without limit, so the sun counts as no lower
// than lowestCloudLightSunHeight here; below it shafts and pools part
// company a little rather than the pattern smearing across the level.
struct CloudLightLanes {
    // xy as above; z = pattern frequency, w = detail frequency.
    Vec4 projection {};
    // xy = how far the clouds have drifted, z = coverage, w = edge softness.
    Vec4 shape {};
    // x = dimming on surfaces, y = strength of the detail the air sees. Zero
    // in both when the cloud light is off, which is what the shaders test.
    Vec4 strength {};
    // xy = the unit direction along which the detail is drawn out into rays,
    // z = how much lower its frequency is that way. See cloudLightRayAxis.
    Vec4 rays { 1.0f, 0.0f, 1.0f, 0.0f };
};

inline constexpr float lowestCloudLightSunHeight = 0.2f;

// The direction, in the pattern's own plane, that the camera's line of sight
// sweeps along as it passes through the air: a step of `cameraForward` moves
// a point's pattern coordinate by this. A ray of sight gathers the light of
// everything along that sweep, so detail that is short that way averages
// out before it reaches the eye, and detail drawn out that way arrives
// whole. z of the result is zero when sight runs along the sun's own rays
// and there is no sweep to speak of; then the detail should stay round.
[[nodiscard]] inline Vec3 cloudLightRayAxis(
    Vec2 projection,
    Vec3 cameraForward)
{
    const Vec2 sweep {
        cameraForward.x + cameraForward.z * projection.x,
        cameraForward.y + cameraForward.z * projection.y,
    };
    const float length = std::sqrt(sweep.x * sweep.x + sweep.y * sweep.y);
    if (length <= 0.0001f) {
        return { 1.0f, 0.0f, 0.0f };
    }
    return { sweep.x / length, sweep.y / length, length };
}

[[nodiscard]] inline CloudLightLanes cloudLightLanes(
    const RenderFrameData::Lighting& lighting,
    float effectAnimationTimeSeconds,
    Vec3 cameraForward)
{
    const RenderFrameData::Lighting::CloudLight& clouds = lighting.cloudLight;
    if (!clouds.enabled) {
        return {};
    }
    // The fallback and the normalization the lit shaders apply to the sun.
    Vec3 sun = lighting.sun.direction;
    const float length =
        std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
    sun = length > 0.0001f
        ? Vec3 { sun.x / length, sun.y / length, sun.z / length }
        : Vec3 { 0.0f, 0.0f, 1.0f };
    const float height = std::max(sun.z, lowestCloudLightSunHeight);
    const Vec2 projection { -sun.x / height, -sun.y / height };
    const Vec3 rayAxis = cloudLightRayAxis(projection, cameraForward);
    // Full stretch once the sweep is a quarter of a unit per unit of sight,
    // easing back to round detail as the sweep vanishes, so the rays do not
    // snap to an arbitrary direction as a camera swings past the sun's.
    const float stretch = clouds.detailStretch +
        (1.0f - clouds.detailStretch) *
            (1.0f - std::clamp(rayAxis.z * 4.0f, 0.0f, 1.0f));
    return CloudLightLanes {
        .projection = {
            projection.x,
            projection.y,
            std::max(clouds.scale, 0.0f),
            std::max(clouds.detailScale, 0.0f),
        },
        .shape = {
            clouds.driftVelocity.x * effectAnimationTimeSeconds,
            clouds.driftVelocity.y * effectAnimationTimeSeconds,
            std::clamp(clouds.coverage, 0.0f, 1.0f),
            std::max(clouds.softness, 0.0f),
        },
        .strength = {
            std::clamp(clouds.surfaceStrength, 0.0f, 1.0f),
            std::clamp(clouds.detailStrength, 0.0f, 1.0f),
            0.0f,
            0.0f,
        },
        .rays = {
            rayAxis.x,
            rayAxis.y,
            std::clamp(stretch, 0.05f, 1.0f),
            0.0f,
        },
    };
}

// CloudLight.glsl's function of the same name: where the sun ray through a
// world position crosses height zero, less the clouds' drift.
[[nodiscard]] inline Vec2 cloudLightCoordinate(
    const CloudLightLanes& lanes,
    Vec3 worldPosition)
{
    return {
        worldPosition.x + worldPosition.z * lanes.projection.x - lanes.shape.x,
        worldPosition.y + worldPosition.z * lanes.projection.y - lanes.shape.y,
    };
}

// CloudLight.glsl's cloudLightHashBits, cloudLightNoise and cloudLightGap, for
// what has to know on the CPU where the sun falls. The hash is the same
// integer arithmetic; the rest agrees with the shader to rounding. One with
// the cloud light off, as in the shader.
[[nodiscard]] inline float cloudLightHash(int32_t x, int32_t y)
{
    uint32_t h = (static_cast<uint32_t>(x) * 0x8da6b343u) ^
        (static_cast<uint32_t>(y) * 0xd8163841u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

[[nodiscard]] inline float cloudLightNoise(Vec2 position)
{
    const float floorX = std::floor(position.x);
    const float floorY = std::floor(position.y);
    const auto cellX = static_cast<int32_t>(floorX);
    const auto cellY = static_cast<int32_t>(floorY);
    const auto blend = [](float local) {
        return local * local * local * (local * (local * 6.0f - 15.0f) + 10.0f);
    };
    const float blendX = blend(position.x - floorX);
    const float blendY = blend(position.y - floorY);
    const auto mix = [](float from, float to, float amount) {
        return from + (to - from) * amount;
    };
    const float lower = mix(
        cloudLightHash(cellX, cellY), cloudLightHash(cellX + 1, cellY), blendX);
    const float upper = mix(
        cloudLightHash(cellX, cellY + 1), cloudLightHash(cellX + 1, cellY + 1), blendX);
    return mix(lower, upper, blendY);
}

[[nodiscard]] inline float cloudLightGap(const CloudLightLanes& lanes, Vec2 coordinate)
{
    if (lanes.projection.z <= 0.0f) {
        return 1.0f;
    }
    const Vec2 position {
        coordinate.x * lanes.projection.z, coordinate.y * lanes.projection.z };
    const Vec2 turned {
        (0.8f * position.x - 0.6f * position.y) * 2.03f + 17.3f,
        (0.6f * position.x + 0.8f * position.y) * 2.03f - 9.1f,
    };
    float pattern = cloudLightNoise(position) * 0.68f + cloudLightNoise(turned) * 0.32f;
    pattern = std::clamp((pattern - 0.5f) * 2.0f + 0.5f, 0.0f, 1.0f);
    const float coverage = lanes.shape.z;
    const float softness = std::max(lanes.shape.w, 0.001f);
    const float t = std::clamp(
        (pattern - (coverage - softness)) / (2.0f * softness), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// How the bloom's two scales reach the screen. The tonemap adds the bloom
// target times total. The pass that lays the skirt over the core writes the
// skirt times wideShare and keeps coreShare of the core, so each scale
// arrives at its own intensity whatever the other is set to. A skirt of no
// strength is not drawn, and the core then stands alone at full share.
struct BloomMix {
    float total = 0.0f;
    float coreShare = 1.0f;
    float wideShare = 0.0f;

    [[nodiscard]] bool drawsWide() const { return wideShare > 0.0f; }
};

[[nodiscard]] inline BloomMix bloomMix(float coreIntensity, float wideIntensity)
{
    const float core = std::max(coreIntensity, 0.0f);
    const float wide = std::max(wideIntensity, 0.0f);
    if (wide <= 0.0f) {
        return { .total = core };
    }
    const float total = core + wide;
    return {
        .total = total,
        .coreShare = core / total,
        .wideShare = wide / total,
    };
}

// What the cloud light does to the sun inside one participating medium: how
// much a cloud takes away, and how much brighter a gap glows. The air uses
// its own strength and gain, and sees the finer breaks that make rays. A
// medium dense enough to be read as a surface takes the light as a surface
// does: the surfaces' strength, the broad pattern only and no lift. It says
// so with cloudLightGainOfASurface, which no real gain can be. Off, it is
// no dimming at all, which is what the shader tests.
struct CloudLightMedium {
    float strength = 0.0f;
    float gain = 1.0f;
};

inline constexpr float cloudLightGainOfASurface = 0.0f;

[[nodiscard]] inline CloudLightMedium cloudLightMedium(
    const RenderFrameData::Lighting& lighting,
    bool readsAsSurface)
{
    const RenderFrameData::Lighting::CloudLight& clouds = lighting.cloudLight;
    if (!clouds.enabled) {
        return {};
    }
    if (readsAsSurface) {
        return {
            .strength = std::clamp(clouds.surfaceStrength, 0.0f, 1.0f),
            .gain = cloudLightGainOfASurface,
        };
    }
    return {
        .strength = std::clamp(clouds.mediumStrength, 0.0f, 1.0f),
        .gain = std::max(clouds.mediumGain, 1.0f),
    };
}

} // namespace sokoban
