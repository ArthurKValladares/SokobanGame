#pragma once

#include "engine/Math.hpp"

#include <cstdint>

namespace sokoban::config {

// Undiscovered overworld screens are filled with a dense participating
// medium. It is rendered by the atmosphere pass, so these values describe
// physical extinction and scattering rather than the alpha of an overlay.
inline constexpr Vec3 fogOfWarColor { 0.34f, 0.42f, 0.52f };
inline constexpr float fogOfWarDensity = 4.25f;
inline constexpr float fogOfWarScatteringStrength = 0.72f;
inline constexpr float fogOfWarAnisotropy = 0.18f;
inline constexpr uint32_t fogOfWarSampleCount = 20;

// World-space fractal noise breaks the volume into broad billows with a
// smaller detail octave. Strength stays below one so noise can shape the
// cover without punching transparent holes through an undiscovered screen.
inline constexpr float fogOfWarNoiseScale = 0.28f;
inline constexpr float fogOfWarNoiseStrength = 0.58f;
inline constexpr float fogOfWarNoiseSpeed = 0.12f;

// Keep the cover to one consistent three-tile column above the board. The
// horizontal feather begins outside the authored screen rectangle, so every
// tile in an undiscovered screen still receives full-density fog.
inline constexpr float fogOfWarMinimumHeight = 0.0f;
inline constexpr float fogOfWarHeight = 3.0f;
inline constexpr float fogOfWarEdgeFadeDistance = 1.25f;

// A first visit clears outwards from the centre of the entry tile. The soft
// edge keeps the transition volumetric instead of reading as a hard wipe.
inline constexpr float fogOfWarRevealDurationSeconds = 1.6f;
inline constexpr float fogOfWarInitialRevealRadius = 0.72f;
inline constexpr float fogOfWarRevealFeather = 0.9f;

} // namespace sokoban::config
