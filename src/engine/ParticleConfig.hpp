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

inline constexpr std::string_view turretMuzzleTextureName = "Muzzle01";
inline constexpr std::string_view turretGlowTextureName = "ParticleGlow";
inline constexpr std::string_view turretLaserTextureName = "LaserBeam";
inline constexpr Vec4 turretMuzzleColor { 1.0f, 0.82f, 0.28f, 1.0f };
inline constexpr float turretMuzzleEmissiveStrength = 2.4f;
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
inline constexpr float turretMuzzleGlowEmissiveStrength = 1.8f;
inline constexpr uint32_t turretMuzzleGlowParticleCount = 2;
inline constexpr Vec2 turretMuzzleGlowLifetimeSeconds { 0.13f, 0.19f };
inline constexpr Vec2 turretMuzzleGlowInitialSize { 1.23f, 1.53f };
inline constexpr Vec2 turretMuzzleGlowFinalSize { 0.39f, 0.63f };

// Both layers grow from the muzzle to the target, then hold as one connected
// beam. A broad warm glow surrounds a tighter near-white core.
inline constexpr Vec4 turretLaserColor { 1.0f, 0.58f, 0.06f, 0.72f };
inline constexpr Vec4 turretLaserCoreColor { 1.0f, 0.96f, 0.70f, 0.96f };
inline constexpr float turretLaserEmissiveStrength = 2.2f;
inline constexpr float turretLaserCoreEmissiveStrength = 3.8f;
// The laser texture is a vertical capsule. Nine-slicing keeps its rounded
// ends proportional to beam width while only its central band grows along
// the path.
inline constexpr NineSlice turretLaserTextureNineSlice =
    NineSlice::symmetricFitTargetWidth({ 0.20f, 0.20f });
inline constexpr int32_t turretLaserDrawOrder = 0;
inline constexpr int32_t turretLaserCoreDrawOrder = 1;
inline constexpr float turretLaserWidth = 0.90f;
inline constexpr float turretLaserCoreWidth = 0.30f;
inline constexpr float turretLaserLifetimeSeconds = 0.36f;
inline constexpr float turretLaserGrowthSeconds = 0.08f;
inline constexpr float turretLaserAfterImpactSeconds = 0.08f;
inline constexpr float turretLaserAfterMuzzleSeconds = 0.008f;

// The hit uses the same circular glow texture and palette as the beam. Two
// centered layers read as one expanding sphere: a broad warm shell and a
// shorter-lived white-hot core.
inline constexpr Vec4 turretImpactColor {
    turretLaserColor.x,
    turretLaserColor.y,
    turretLaserColor.z,
    1.0f,
};
inline constexpr Vec4 turretImpactCoreColor {
    turretLaserCoreColor.x,
    turretLaserCoreColor.y,
    turretLaserCoreColor.z,
    1.0f,
};
inline constexpr float turretImpactEmissiveStrength = 2.6f;
inline constexpr float turretImpactCoreEmissiveStrength = 4.2f;
inline constexpr Vec2 turretImpactLifetimeSeconds { 0.20f, 0.20f };
inline constexpr Vec2 turretImpactCoreLifetimeSeconds { 0.13f, 0.13f };
inline constexpr Vec2 turretImpactInitialSize { 0.50f, 0.50f };
inline constexpr Vec2 turretImpactFinalSize { 2.10f, 2.10f };
inline constexpr Vec2 turretImpactCoreInitialSize { 0.34f, 0.34f };
inline constexpr Vec2 turretImpactCoreFinalSize { 1.05f, 1.05f };
inline constexpr int32_t turretImpactDrawOrder = 2;
inline constexpr int32_t turretImpactCoreDrawOrder = 3;
inline constexpr float turretMuzzleForwardOffset = 0.52f;
inline constexpr float turretMuzzleElevation = 0.64f;
inline constexpr float turretTargetElevation = 0.58f;
inline constexpr bool turretParticlesDrawOnTop = true;

inline constexpr float turretRecoilDurationSeconds = 0.20f;
inline constexpr float turretRecoilDistance = 0.12f;

} // namespace sokoban::config
