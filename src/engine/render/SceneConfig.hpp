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
// Per-tile render scales live in assets/manifest.json (tile entries).
inline constexpr float conveyorTileHeight = 0.12f;

inline constexpr float iceTintAlpha = 0.38f;
inline constexpr float iceBlurRadiusPixels = 3.0f;

inline constexpr Vec4 tileGridLineColor { 0.26f, 0.27f, 0.29f, 0.42f };
inline constexpr float tileGridLineWidth = 1.25f;
inline constexpr float minimumTileGridLineWidth = 0.0f;
inline constexpr float maximumTileGridLineWidth = 12.0f;
inline constexpr float tileGridElevationOffset = 0.015f;

} // namespace sokoban::config
