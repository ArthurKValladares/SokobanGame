#pragma once

#include "engine/Math.hpp"

#include <cstdint>

namespace sokoban::config {

inline constexpr float sunAzimuthDegrees = -122.5f;
inline constexpr float minimumSunAzimuthDegrees = -180.0f;
inline constexpr float maximumSunAzimuthDegrees = 180.0f;
inline constexpr float sunTiltDegrees = 33.0f;
inline constexpr float minimumSunTiltDegrees = -90.0f;
inline constexpr float maximumSunTiltDegrees = 90.0f;
inline constexpr Vec3 sunColor { 1.0f, 0.96f, 0.86f };
inline constexpr float sunIntensity = 1.05f;
inline constexpr float maximumSunIntensity = 4.0f;
inline constexpr Vec3 ambientLightColor { 0.70f, 0.76f, 0.84f };
inline constexpr float ambientLightIntensity = 0.44f;
inline constexpr float maximumAmbientLightIntensity = 2.0f;
// A scene-wide dial on the specular half of the BRDF, full strength by
// default. It was 0.16 under Blinn-Phong, where it was a fudge factor on an
// unnormalized lobe; F3c's Cook-Torrance term is normalized and carries its
// own Fresnel, so damping it by default would only mean throwing away energy
// the material asked for. Its companion, specularPower, is gone with the
// exponent it fed - a material's roughness is what decides gloss now.
inline constexpr float specularStrength = 1.0f;
inline constexpr float maximumSpecularStrength = 1.0f;
inline constexpr float modelShadowReceive = 0.35f;
inline constexpr float maximumModelShadowReceive = 1.0f;

inline constexpr bool ambientOcclusionEnabled = true;
inline constexpr float ambientOcclusionStrength = 0.55f;
inline constexpr float maximumAmbientOcclusionStrength = 1.0f;
inline constexpr float ssaoRadiusPixels = 10.0f;
inline constexpr float ssaoDepthRange = 0.02f;

inline constexpr bool shadowsEnabled = true;
inline constexpr float shadowOpacity = 0.5f;
inline constexpr float maximumShadowOpacity = 0.85f;
inline constexpr float shadowBias = 0.010f;
inline constexpr float maximumShadowBias = 0.05f;
inline constexpr float shadowMapPadding = 1.0f;
inline constexpr uint32_t shadowMapSize = 2048;
inline constexpr uint32_t pointShadowMapSize = 512;
inline constexpr float pointShadowNearPlane = 0.05f;

} // namespace sokoban::config
