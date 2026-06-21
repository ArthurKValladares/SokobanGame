#pragma once

#include "engine/Math.hpp"

namespace sokoban::config {

inline constexpr float playerMoveDurationSeconds = 0.15f;
inline constexpr float boardPitchDegrees = 15.0f;
inline constexpr float perspectiveFovDegrees = 35.0f;
inline constexpr float perspectiveCameraDistanceScale = 2.2f;
inline constexpr float maxWireframeLineWidth = 16.0f;
inline constexpr float waterDepthBelowGround = 0.18f;
inline constexpr float sunAzimuthDegrees = -122.5f;
inline constexpr float sunTiltDegrees = 33.0f;
inline constexpr Vec3 sunColor { 1.0f, 0.96f, 0.86f };
inline constexpr float sunIntensity = 1.25f;
inline constexpr Vec3 ambientLightColor { 0.55f, 0.62f, 0.70f };
inline constexpr float ambientLightIntensity = 0.28f;

} // namespace sokoban::config
