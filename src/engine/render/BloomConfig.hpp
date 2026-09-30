#pragma once

#include <cstdint>

namespace sokoban::config {

// Bloom is deliberately a restrained finishing effect. Only HDR values above
// ordinary white contribute, with a soft knee so highlights enter the glow
// without a hard halo boundary. The separable blur runs at half resolution;
// that both limits its cost and gives it a wider, smoother footprint.
inline constexpr uint32_t bloomResolutionDivisor = 2;
inline constexpr float bloomThreshold = 1.0f;
inline constexpr float bloomSoftKnee = 0.5f;
inline constexpr float bloomIntensity = 0.18f;

} // namespace sokoban::config
