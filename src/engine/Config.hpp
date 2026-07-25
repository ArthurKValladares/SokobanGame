#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <string_view>

namespace sokoban::config {

inline constexpr float stepDurationSeconds = 0.25f;
// Defaults for a newly created player profile. Runtime values are persisted.
inline constexpr float masterVolume = 0.03f;
inline constexpr float musicVolume = 0.5f; // relative to master
inline constexpr std::string_view uiFontPath = "ui/Karla-Regular.ttf";
inline constexpr std::string_view titleBackgroundPath =
    "custom/ui/main-menu-rogue-pushing-rock-4k.png";
// Per-sound-set volumes live in assets/manifest.json (sound entries).
inline constexpr float footstepIntervalSeconds = 0.2f; // one footstep per tile at the default step duration
inline constexpr float boardPitchDegrees = 15.0f;
inline constexpr float perspectiveFovDegrees = 35.0f;
inline constexpr float perspectiveCameraDistanceScale = 2.2f;
inline constexpr float maxWireframeLineWidth = 16.0f;
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
inline constexpr float surfaceEntityHeight = 0.08f;
inline constexpr float surfaceEntityWidthDepth = 0.72f;
inline constexpr float minTileScale = 0.05f;
inline constexpr float maxTileScale = 3.0f;
// Per-tile render scales live in assets/manifest.json (tile entries).
inline constexpr float conveyorTileHeight = 0.12f;
// Player clip sources and numbers live in assets/manifest.json (animation
// entries with the player animation roles).
inline constexpr float playerAnimationFadeSeconds = 0.12f; // crossfade between idle/walk/push clips
inline constexpr float sunAzimuthDegrees = -122.5f;
inline constexpr float sunTiltDegrees = 33.0f;
inline constexpr Vec3 sunColor { 1.0f, 0.96f, 0.86f };
inline constexpr float sunIntensity = 1.05f;
inline constexpr Vec3 ambientLightColor { 0.70f, 0.76f, 0.84f };
inline constexpr float ambientLightIntensity = 0.44f;
inline constexpr float specularStrength = 0.16f;
inline constexpr float specularPower = 36.0f;
inline constexpr float modelShadowReceive = 0.35f;
inline constexpr bool ambientOcclusionEnabled = true;
inline constexpr float ambientOcclusionStrength = 0.55f;
inline constexpr float ssaoRadiusPixels = 10.0f;
inline constexpr float ssaoDepthRange = 0.02f;
inline constexpr bool shadowsEnabled = true;
inline constexpr float shadowOpacity = 0.5f;
inline constexpr float shadowBias = 0.010f;
inline constexpr float shadowMapPadding = 1.0f;
inline constexpr uint32_t shadowMapSize = 2048;
inline constexpr float iceTintAlpha = 0.38f;
inline constexpr float iceBlurRadiusPixels = 3.0f;
inline constexpr Vec4 tileGridLineColor { 0.26f, 0.27f, 0.29f, 0.42f };
inline constexpr float tileGridLineWidth = 1.25f;
inline constexpr float tileGridElevationOffset = 0.015f;


} // namespace sokoban::config
