#pragma once

#include "engine/Math.hpp"

namespace sokoban::config {

inline constexpr float waterDepthBelowGround = 0.18f;
inline constexpr float waterExteriorMarginScale = 2.0f;
inline constexpr float waterExteriorMinimumMarginTiles = 4.0f;
inline constexpr float drownedPlayerDepthBelowGround = 1.0f;
inline constexpr Vec4 waterSurfaceColor { 0.03f, 0.34f, 0.68f, 0.72f };

inline constexpr float waterToneSpatialFrequency = 0.38f;
inline constexpr float waterDarkToneMultiplier = 0.78f;
inline constexpr float waterLightToneMultiplier = 1.08f;
inline constexpr float waterToneTransitionWidth = 0.035f;
inline constexpr float waterToneSpeed = 0.08f;

inline constexpr Vec4 waterSecondaryRippleColor {
    0.34f,
    0.64f,
    0.80f,
    0.20f,
};
inline constexpr float waterRippleSpatialFrequency = 1.5f;
inline constexpr float waterRippleSpeed = 1.15f;
inline constexpr float waterRefractionStrength = 0.0020f;
inline constexpr float waterRippleCrestHalfWidth = 0.038f;
inline constexpr float waterRippleHaloWidth = 0.085f;
inline constexpr float waterRippleHaloStrength = 0.08f;
inline constexpr float waterRippleCrestStrength = 0.50f;
inline constexpr float waterSecondaryRippleThicknessScale = 0.48f;

inline constexpr float waterShorelineNearDistance = 0.055f;
inline constexpr float waterShorelineFarDistance = 0.090f;
inline constexpr float waterShorelineFarOpacity = 0.55f;
inline constexpr float waterShorelineFarThickness = 0.010f;

} // namespace sokoban::config
