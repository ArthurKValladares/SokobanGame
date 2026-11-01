#pragma once

#include "engine/Math.hpp"
#include "engine/render/NineSlice.hpp"

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
inline constexpr Vec4 witchSwapSmokeColor { 0.72f, 0.18f, 0.94f, 0.78f };

// Gates use the renderer's animated energy-surface shader for a translucent
// cube and its edge rails. Particles are limited to textured corner blooms,
// giving the barrier a readable silhouette instead of a lattice of dots.
inline constexpr std::string_view gateParticleTextureName = "ParticleGlow";
inline constexpr float gateEnergyShellAlpha = 0.30f;
inline constexpr float gateEnergyCoreAlpha = 0.12f;
inline constexpr float gateEnergyEdgeAlpha = 0.88f;
// A closed gate is a solid block, so its shell and rails fill the whole
// cell; the hair of inset only keeps its faces off neighbouring blocks'.
inline constexpr float gateEnergyInset = 0.002f;
inline constexpr float gateEnergyEdgeThickness = 0.075f;
inline constexpr uint32_t gateEnergyTileCount = 14;
// Editor opacity of a gate that starts open (closes when its plates are
// pressed), so it reads as the empty space it is during play.
inline constexpr float gateStartOpenEditorOpacity = 0.35f;
inline constexpr float gateCornerParticleOpacity = 0.42f;
inline constexpr float gateCornerParticleEmissiveStrength = 2.8f;
inline constexpr float gateCornerParticleSize = 0.38f;
inline constexpr uint32_t gateCornerParticleCount = 8;

// The connected fire surface creates its flowing billows in the shader.
inline constexpr std::string_view turretGlowTextureName = "ParticleGlow";
inline constexpr Vec4 turretFlameCoreColor { 1.0f, 0.86f, 0.20f, 0.96f };
inline constexpr float turretFlameEmissiveStrength = 3.2f;
inline constexpr float turretFlameWidth = 0.72f;
// Preserve the laser's initial origin-to-destination travel time.
inline constexpr float turretFlameGrowthSeconds = 0.08f;
inline constexpr float turretFlameAfterImpactSeconds = 0.08f;

inline constexpr float turretMuzzleForwardOffset = 0.52f;
inline constexpr float turretMuzzleElevation = 0.64f;
inline constexpr float turretTargetElevation = 0.58f;
inline constexpr bool turretParticlesDrawOnTop = true;

inline constexpr float turretRecoilDurationSeconds = 0.20f;
inline constexpr float turretRecoilDistance = 0.12f;

} // namespace sokoban::config
