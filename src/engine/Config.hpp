#pragma once

#include "engine/Math.hpp"

#include <cstdint>

namespace sokoban::config {

inline constexpr float playerMoveDurationSeconds = 0.25f;
inline constexpr float boardPitchDegrees = 15.0f;
inline constexpr float perspectiveFovDegrees = 35.0f;
inline constexpr float perspectiveCameraDistanceScale = 2.2f;
inline constexpr float maxWireframeLineWidth = 16.0f;
inline constexpr float waterDepthBelowGround = 0.18f;
inline constexpr float surfaceEntityHeight = 0.08f;
inline constexpr float surfaceEntityWidthDepth = 0.72f;
inline constexpr float minTileScale = 0.05f;
inline constexpr float maxTileScale = 3.0f;
inline constexpr float airTileScale = 1.0f;
inline constexpr float groundTileScale = 1.0f;
inline constexpr float wallTileScale = 1.1f;
inline constexpr float endTileScale = 1.0f;
inline constexpr float pressurePlateTileScale = 1.0f;
inline constexpr float playerTileScale = 1.0f;
inline constexpr float rockTileScale = 1.0f;
inline constexpr float iceTileScale = 1.0f;
inline constexpr float waterTileScale = 1.0f;
inline constexpr uint32_t playerMovementAnimationNumber = 7;
inline constexpr uint32_t playerIdleAnimationNumber = 15;
inline constexpr float sunAzimuthDegrees = -122.5f;
inline constexpr float sunTiltDegrees = 33.0f;
inline constexpr Vec3 sunColor { 1.0f, 0.96f, 0.86f };
inline constexpr float sunIntensity = 1.05f;
inline constexpr Vec3 ambientLightColor { 0.70f, 0.76f, 0.84f };
inline constexpr float ambientLightIntensity = 0.44f;
inline constexpr float specularStrength = 0.16f;
inline constexpr float specularPower = 36.0f;
inline constexpr float modelShadowReceive = 0.35f;
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
