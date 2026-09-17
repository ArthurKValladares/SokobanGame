#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace sokoban::config {

inline constexpr std::array<std::string_view, 10>
    mirrorSwapSmokeTextureNames {
        "Smoke01", "Smoke02", "Smoke03", "Smoke04", "Smoke05",
        "Smoke06", "Smoke07", "Smoke08", "Smoke09", "Smoke10",
    };

inline constexpr uint32_t mirrorSwapSmokeParticleCount = 7;
inline constexpr Vec2 mirrorSwapSmokeLifetimeSeconds { 0.42f, 0.72f };
inline constexpr float mirrorSwapSmokeScale = 1.6f;
inline constexpr Vec2 mirrorSwapSmokeInitialSize { 0.34f, 0.52f };
inline constexpr Vec2 mirrorSwapSmokeFinalSize { 0.72f, 1.02f };
inline constexpr float mirrorSwapSmokeSpawnRadius = 0.20f;
inline constexpr Vec3 mirrorSwapSmokeMinimumVelocity {
    -0.18f, -0.18f, 0.28f };
inline constexpr Vec3 mirrorSwapSmokeMaximumVelocity {
    0.18f, 0.18f, 0.62f };
inline constexpr float mirrorSwapSmokeMinimumAngularVelocity = -1.8f;
inline constexpr float mirrorSwapSmokeMaximumAngularVelocity = 1.8f;
inline constexpr float mirrorSwapSmokeElevation = 0.72f;
inline constexpr float mirrorSwapSmokeOpacity = 0.72f;
inline constexpr bool mirrorSwapSmokeDrawOnTop = true;

inline constexpr std::string_view turretMuzzleTextureName = "Muzzle01";
inline constexpr std::string_view turretTrailTextureName = "ParticleGlow";
inline constexpr Vec4 turretMuzzleColor { 1.0f, 0.58f, 0.10f, 1.0f };
inline constexpr uint32_t turretMuzzleParticleCount = 2;
inline constexpr Vec2 turretMuzzleLifetimeSeconds { 0.055f, 0.09f };
inline constexpr Vec2 turretMuzzleInitialSize { 0.38f, 0.48f };
inline constexpr Vec2 turretMuzzleFinalSize { 0.08f, 0.14f };
inline constexpr float turretMuzzleSpawnRadius = 0.035f;
inline constexpr Vec3 turretMuzzleMinimumVelocity {
    -0.05f, -0.05f, 0.02f };
inline constexpr Vec3 turretMuzzleMaximumVelocity {
    0.05f, 0.05f, 0.12f };

inline constexpr Vec4 turretTrailColor { 1.0f, 0.78f, 0.24f, 0.94f };
inline constexpr Vec2 turretTrailLifetimeSeconds { 0.07f, 0.105f };
inline constexpr Vec2 turretTrailInitialSize { 0.12f, 0.15f };
inline constexpr Vec2 turretTrailFinalSize { 0.025f, 0.045f };
inline constexpr float turretTrailSpacing = 0.10f;
inline constexpr float turretBulletSpeed = 48.0f;
inline constexpr float turretTrailAfterMuzzleSeconds = 0.008f;
inline constexpr float turretMuzzleForwardOffset = 0.40f;
inline constexpr float turretMuzzleElevation = 0.62f;
inline constexpr float turretTargetElevation = 0.58f;
inline constexpr bool turretParticlesDrawOnTop = true;

inline constexpr float turretRecoilDurationSeconds = 0.20f;
inline constexpr float turretRecoilDistance = 0.12f;

} // namespace sokoban::config
