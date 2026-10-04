#pragma once

#include "engine/Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Pixel art for the level editor's tool cursors: the eyedropper shown while
// the Pick Tile key is held and the brush shown while the Paint Link Color
// key is held. Drawn in code so there is no asset to ship; the caller turns
// the pixels into an SDL cursor. Kept free of SDL so it can be tested
// headless.
namespace sokoban::editorCursorArt {

inline constexpr int size = 32;

struct Image {
    // Row-major RGBA, 8 bits per channel, straight (not premultiplied) alpha.
    std::vector<std::uint8_t> rgba;
    int hotspotX = 0;
    int hotspotY = 0;

    [[nodiscard]] std::uint8_t alphaAt(int x, int y) const
    {
        return rgba[(static_cast<std::size_t>(y) * size +
                     static_cast<std::size_t>(x)) * 4 + 3];
    }
};

// One stroke of the drawing: a capsule from `from` to `to`.
struct Capsule {
    Vec2 from {};
    Vec2 to {};
    float radius = 1.0f;
    Vec3 color {};
};

namespace detail {

[[nodiscard]] inline float distanceToSegment(Vec2 point, Vec2 from, Vec2 to)
{
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float lengthSquared = dx * dx + dy * dy;
    float t = 0.0f;
    if (lengthSquared > 0.0f) {
        t = std::clamp(
            ((point.x - from.x) * dx + (point.y - from.y) * dy) /
                lengthSquared,
            0.0f,
            1.0f);
    }
    const float px = from.x + dx * t - point.x;
    const float py = from.y + dy * t - point.y;
    return std::sqrt(px * px + py * py);
}

// Later capsules paint over earlier ones. Around the whole shape runs a
// dark outline and, outside that, a light halo, so the cursor reads on any
// background. Edges are 4x4 supersampled.
[[nodiscard]] inline Image rasterize(
    const std::vector<Capsule>& capsules, int hotspotX, int hotspotY)
{
    constexpr float outline = 1.1f;
    constexpr float halo = 1.0f;
    constexpr int samples = 4;
    Image image {
        .rgba = std::vector<std::uint8_t>(
            static_cast<std::size_t>(size * size * 4), 0),
        .hotspotX = hotspotX,
        .hotspotY = hotspotY,
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            float alpha = 0.0f;
            for (int sy = 0; sy < samples; ++sy) {
                for (int sx = 0; sx < samples; ++sx) {
                    const Vec2 point {
                        static_cast<float>(x) +
                            (static_cast<float>(sx) + 0.5f) / samples,
                        static_cast<float>(y) +
                            (static_cast<float>(sy) + 0.5f) / samples,
                    };
                    const Capsule* fill = nullptr;
                    float nearestEdge = 1.0e9f;
                    for (const Capsule& capsule : capsules) {
                        const float distance = distanceToSegment(
                            point, capsule.from, capsule.to);
                        if (distance <= capsule.radius) {
                            fill = &capsule;
                        }
                        nearestEdge = std::min(
                            nearestEdge, distance - capsule.radius);
                    }
                    Vec3 color {};
                    float coverage = 0.0f;
                    if (fill != nullptr) {
                        color = fill->color;
                        coverage = 1.0f;
                    } else if (nearestEdge <= outline) {
                        color = { 0.05f, 0.05f, 0.06f };
                        coverage = 1.0f;
                    } else if (nearestEdge <= outline + halo) {
                        color = { 1.0f, 1.0f, 1.0f };
                        coverage = 0.75f;
                    }
                    red += color.x * coverage;
                    green += color.y * coverage;
                    blue += color.z * coverage;
                    alpha += coverage;
                }
            }
            const float count = static_cast<float>(samples * samples);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * size +
                 static_cast<std::size_t>(x)) * 4;
            const auto byte = [](float value) {
                return static_cast<std::uint8_t>(
                    std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            };
            if (alpha > 0.0f) {
                image.rgba[offset + 0] = byte(red / alpha);
                image.rgba[offset + 1] = byte(green / alpha);
                image.rgba[offset + 2] = byte(blue / alpha);
            }
            image.rgba[offset + 3] = byte(alpha / count);
        }
    }
    return image;
}

} // namespace detail

// A pipette leaning to the upper right, its tip at the lower-left corner,
// which is the hotspot.
[[nodiscard]] inline Image eyedropper()
{
    const Vec3 glass { 0.92f, 0.95f, 0.98f };
    const Vec3 band { 0.55f, 0.58f, 0.62f };
    const Vec3 bulb { 0.22f, 0.24f, 0.28f };
    return detail::rasterize(
        {
            Capsule { { 3.0f, 29.0f }, { 6.0f, 26.0f }, 0.7f, glass },
            Capsule { { 6.0f, 26.0f }, { 8.0f, 24.0f }, 1.3f, glass },
            Capsule { { 8.0f, 24.0f }, { 17.0f, 15.0f }, 1.9f, glass },
            Capsule { { 16.5f, 15.5f }, { 19.0f, 13.0f }, 3.4f, band },
            Capsule { { 20.5f, 11.5f }, { 25.5f, 6.5f }, 4.2f, bulb },
        },
        3,
        29);
}

// A paintbrush leaning to the upper right, its bristles dipped in `paint`
// (the active link color); the bristle tip at the lower-left corner is the
// hotspot.
[[nodiscard]] inline Image brush(Vec3 paint)
{
    const Vec3 handle { 0.62f, 0.38f, 0.18f };
    const Vec3 ferrule { 0.74f, 0.76f, 0.80f };
    return detail::rasterize(
        {
            Capsule { { 14.5f, 17.5f }, { 27.5f, 4.5f }, 2.3f, handle },
            Capsule { { 10.5f, 21.5f }, { 15.0f, 17.0f }, 3.2f, ferrule },
            Capsule { { 7.5f, 24.5f }, { 10.5f, 21.5f }, 3.3f, paint },
            Capsule { { 5.0f, 27.0f }, { 7.5f, 24.5f }, 2.4f, paint },
            Capsule { { 3.0f, 29.0f }, { 5.0f, 27.0f }, 1.3f, paint },
        },
        3,
        29);
}

} // namespace sokoban::editorCursorArt
