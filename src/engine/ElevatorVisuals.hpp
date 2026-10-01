#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

namespace sokoban {

namespace config {

// The platform is the Kenney Platformer Kit `platform` model (manifest tile
// `Elevator`). It is fitted into a full-footprint slab this many tiles thick,
// placed so its top is flush with the top of the layer it rests on: a
// platform at layer L spans L + 1 - height to L + 1, exactly where units on
// layer L + 1 stand. The source model is 1 x 0.195 x 1, so 0.2 keeps its
// proportions.
inline constexpr float elevatorPlatformHeight = 0.2f;
// How strongly the link color tints the textured platform. The full color
// would hide the model's own texture; none would not say which pressure
// plates drive it.
inline constexpr float elevatorTintStrength = 0.45f;
// Platform brightness while its linked plates are not all pressed.
inline constexpr float elevatorIdleBrightness = 0.85f;
// Travel time per layer, as a fraction of one world step. A platform sets
// off once the step that pressed its plates has played out, so a long ride
// lengthens that action rather than being squeezed into one step.
inline constexpr float elevatorSecondsPerLayerPerStep = 0.6f;

} // namespace config

[[nodiscard]] inline Vec4 elevatorPlatformColor(Vec3 linkColor, float brightness)
{
    const auto tint = [&](float component) {
        return (1.0f + (component - 1.0f) * config::elevatorTintStrength) *
            brightness;
    };
    return { tint(linkColor.x), tint(linkColor.y), tint(linkColor.z), 1.0f };
}

// The platform drawn for a platform cell at `position` (cell coordinates,
// fractional while it travels). `cell` is the grid cell it is attributed to
// for picking and visibility.
[[nodiscard]] inline RenderFrameData::Tile elevatorPlatformTile(
    GridPosition3 cell,
    Vec3 position,
    Vec4 color,
    RenderModel model)
{
    return {
        .cell = cell,
        .position = { position.x, position.y },
        .size = { 1.0f, 1.0f },
        .color = color,
        .baseElevation =
            position.z + 1.0f - config::elevatorPlatformHeight,
        .height = config::elevatorPlatformHeight,
        .model = model,
    };
}

} // namespace sokoban
