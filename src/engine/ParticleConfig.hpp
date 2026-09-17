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
inline constexpr std::string_view turretGlowTextureName = "ParticleGlow";
inline constexpr std::string_view turretTrailTextureName = "BulletTrace";
inline constexpr Vec4 turretMuzzleColor { 1.0f, 0.82f, 0.28f, 1.0f };
inline constexpr uint32_t turretMuzzleParticleCount = 3;
inline constexpr Vec2 turretMuzzleLifetimeSeconds { 0.11f, 0.16f };
inline constexpr Vec2 turretMuzzleInitialSize { 1.08f, 1.32f };
inline constexpr Vec2 turretMuzzleFinalSize { 0.30f, 0.51f };
inline constexpr float turretMuzzleSpawnRadius = 0.045f;
inline constexpr Vec3 turretMuzzleMinimumVelocity {
    -0.08f, -0.08f, 0.03f };
inline constexpr Vec3 turretMuzzleMaximumVelocity {
    0.08f, 0.08f, 0.16f };

// A broad glow under the authored muzzle sprite gives the flash enough visual
// mass to survive bright ground textures and a distant camera.
inline constexpr Vec4 turretMuzzleGlowColor { 1.0f, 0.38f, 0.04f, 0.86f };
inline constexpr uint32_t turretMuzzleGlowParticleCount = 2;
inline constexpr Vec2 turretMuzzleGlowLifetimeSeconds { 0.13f, 0.19f };
inline constexpr Vec2 turretMuzzleGlowInitialSize { 1.23f, 1.53f };
inline constexpr Vec2 turretMuzzleGlowFinalSize { 0.39f, 0.63f };

// The authored trace fades and narrows from V=1 (the bullet head) toward V=0
// (the tail). One moving ribbon is used per layer, never a row of sprites.
inline constexpr Vec4 turretTrailColor { 1.0f, 0.58f, 0.06f, 0.72f };
inline constexpr Vec4 turretTrailCoreColor { 1.0f, 0.96f, 0.70f, 0.96f };
inline constexpr float turretTrailWidth = 0.33f;
inline constexpr float turretTrailCoreWidth = 0.1275f;
inline constexpr float turretTrailMaximumLength = 7.0f;
inline constexpr float turretBulletSpeed = 48.0f;
inline constexpr float turretTrailAfterMuzzleSeconds = 0.008f;
inline constexpr float turretMuzzleForwardOffset = 0.52f;
inline constexpr float turretMuzzleElevation = 0.64f;
inline constexpr float turretTargetElevation = 0.58f;
inline constexpr bool turretParticlesDrawOnTop = true;

inline constexpr float turretRecoilDurationSeconds = 0.20f;
inline constexpr float turretRecoilDistance = 0.12f;

} // namespace sokoban::config
