#pragma once

#include "engine/TileTypes.hpp"
#include "engine/render/RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sokoban {

// Sample the cart's visual position, so opening follows every rail segment
// and plays backwards during undo. Nearby parked carts leave the gate shut.
[[nodiscard]] inline float minecartGateOpenness(
    GridPosition3 cell,
    Vec3 position,
    Vec3 from,
    Vec3 to,
    bool moving)
{
    const Vec3 gate {
        static_cast<float>(cell.x),
        static_cast<float>(cell.y),
        static_cast<float>(cell.z),
    };
    const auto atGate = [&](Vec3 point) {
        return std::abs(point.x - gate.x) < 0.001f &&
            std::abs(point.y - gate.y) < 0.001f &&
            std::abs(point.z - gate.z) < 0.001f;
    };
    if (!moving) {
        return atGate(position) ? 1.0f : 0.0f;
    }
    if ((!atGate(from) && !atGate(to)) ||
        std::abs(position.z - gate.z) >= 0.001f) {
        return 0.0f;
    }
    const float dx = position.x - gate.x;
    const float dy = position.y - gate.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    // Fully raised before the cart's front reaches the barrier; start
    // lowering only after its rear has cleared it.
    const float amount = std::clamp((0.95f - distance) / 0.45f, 0.0f, 1.0f);
    return amount * amount * (3.0f - 2.0f * amount);
}

inline void appendMinecartGateVisual(
    RenderFrameData& frame,
    GridPosition3 cell,
    TileType rail,
    float openness = 0.0f,
    float opacity = 1.0f,
    bool pickable = false)
{
    const float x = static_cast<float>(cell.x);
    const float y = static_cast<float>(cell.y);
    const float z = static_cast<float>(cell.z);
    const bool acrossY =
        (railOrientationQuarterTurns(rail).value_or(0) % 2) != 0;
    const auto point = [&](float across, float along, float height) {
        return acrossY ? Vec3 { x + along, y + across, z + height }
                       : Vec3 { x + across, y + along, z + height };
    };
    const auto post = [&](float across, Vec4 color, float base, float height) {
        const Vec3 origin = point(across, 0.41f, base);
        color.w *= opacity;
        frame.tiles.push_back(
            {
                .cell = cell,
                .position = { origin.x, origin.y },
                .size = acrossY ? Vec2 { 0.18f, 0.12f } : Vec2 { 0.12f, 0.18f },
                .color = color,
                .baseElevation = origin.z,
                .height = height,
                .pickable = false,
                .showGrid = false,
            });
    };
    const Vec4 iron { 0.20f, 0.24f, 0.27f, 1.0f };
    const Vec4 brass { 0.74f, 0.48f, 0.16f, 1.0f };
    for (float across : { 0.01f, 0.87f }) {
        post(across, iron, 0.0f, 0.66f);
        post(across, brass, 0.60f, 0.12f);
    }

    // The striped horizontal boom pivots upwards beside the track. Its
    // geometry is shared by gameplay, editor previews and palette baking.
    const float angle = std::clamp(openness, 0.0f, 1.0f) * 1.52f;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const auto armPoint = [&](float length, float along, float height) {
        return point(
            0.07f + length * cosine - height * sine,
            along,
            0.66f + length * sine + height * cosine);
    };
    constexpr int stripes = 6;
    for (int stripe = 0; stripe < stripes; ++stripe) {
        const float start = 0.86f * static_cast<float>(stripe) / stripes;
        const float end = 0.86f * static_cast<float>(stripe + 1) / stripes;
        const Vec4 color = stripe % 2 == 0
            ? Vec4 { 0.94f, 0.70f, 0.25f, opacity }
            : Vec4 { 0.25f, 0.19f, 0.13f, opacity };
        const std::array<Vec3, 8> corners {
            armPoint(start, 0.44f, -0.055f), armPoint(end, 0.44f, -0.055f),
            armPoint(end, 0.56f, -0.055f),   armPoint(start, 0.56f, -0.055f),
            armPoint(start, 0.44f, 0.055f),  armPoint(end, 0.44f, 0.055f),
            armPoint(end, 0.56f, 0.055f),    armPoint(start, 0.56f, 0.055f),
        };
        for (const std::array<std::size_t, 4> face : {
                 std::array<std::size_t, 4> { 0, 3, 2, 1 },
                 { 4, 5, 6, 7 },
                 { 0, 1, 5, 4 },
                 { 3, 7, 6, 2 },
                 { 0, 4, 7, 3 },
                 { 1, 2, 6, 5 },
             }) {
            // Swapping the transverse axes reverses face winding.
            const std::array<Vec3, 4> vertices = acrossY
                ? std::array { corners[face[3]],
                               corners[face[2]],
                               corners[face[1]],
                               corners[face[0]] }
                : std::array { corners[face[0]],
                               corners[face[1]],
                               corners[face[2]],
                               corners[face[3]] };
            frame.isoFaces.push_back(
                {
                    .vertices = vertices,
                    .color = color,
                    .translucent = opacity < 1.0f,
                    .castsShadows = opacity >= 1.0f,
                });
        }
    }
    if (pickable) {
        frame.tiles.push_back(
            {
                .cell = cell,
                .position = { x, y },
                .baseElevation = z,
                .height = 1.0f,
                .pickOnly = true,
                .showGrid = false,
            });
    }
}

} // namespace sokoban
