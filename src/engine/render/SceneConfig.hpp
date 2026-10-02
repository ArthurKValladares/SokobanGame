#pragma once

#include "engine/Math.hpp"

namespace sokoban::config {

inline constexpr float surfaceEntityHeight = 0.08f;
inline constexpr float minimumSurfaceEntityHeight = 0.01f;
inline constexpr float maximumSurfaceEntityHeight = 0.5f;
inline constexpr float surfaceEntityWidthDepth = 0.72f;
inline constexpr float minimumSurfaceEntityWidthDepth = 0.1f;
inline constexpr float maximumSurfaceEntityWidthDepth = 1.0f;

inline constexpr float minTileScale = 0.05f;
inline constexpr float maxTileScale = 3.0f;
inline constexpr float linkedObjectAuraScale = 1.12f;
inline constexpr float linkedObjectAuraOpacity = 0.26f;
inline constexpr float linkedObjectAuraRimPower = 1.35f;
inline constexpr float linkedObjectAuraRimStrength = 1.8f;
inline constexpr float linkedObjectAuraWispSpeed = 1.7f;
inline constexpr float linkedObjectAuraWispStrength = 0.42f;
// Per-tile render scales live in assets/manifest.json (tile entries).
inline constexpr float conveyorTileHeight = 0.12f;

// Multiplies the glass model's own authored 0.5 BLEND alpha, so 1.0 here is
// a half-opaque block over the blurred scene behind it.
inline constexpr float iceTintAlpha = 1.0f;
inline constexpr float iceBlurRadiusPixels = 3.0f;

inline constexpr Vec4 tileGridLineColor { 0.26f, 0.27f, 0.29f, 0.42f };
inline constexpr float tileGridLineWidth = 1.25f;
inline constexpr float minimumTileGridLineWidth = 0.0f;
inline constexpr float maximumTileGridLineWidth = 12.0f;
inline constexpr float tileGridElevationOffset = 0.015f;

} // namespace sokoban::config
