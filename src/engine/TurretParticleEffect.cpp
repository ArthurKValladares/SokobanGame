#include "engine/TurretParticleEffect.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/ParticleConfig.hpp"

#include <algorithm>

namespace sokoban {

TurretParticleEffects makeTurretParticleEffects(const AssetManifest& manifest)
{
    TurretParticleEffects effects;
    effects.muzzleGlow = {
        .textures = {
            manifest.textureIdByName(config::turretGlowTextureName),
        },
        .color = config::turretMuzzleGlowColor,
        .particleCount = config::turretMuzzleGlowParticleCount,
        .lifetimeSeconds = config::turretMuzzleGlowLifetimeSeconds,
        .initialSize = config::turretMuzzleGlowInitialSize,
        .finalSize = config::turretMuzzleGlowFinalSize,
        .drawOnTop = config::turretParticlesDrawOnTop,
    };
    effects.muzzleFlash = {
        .textures = {
            manifest.textureIdByName(config::turretMuzzleTextureName),
        },
        .color = config::turretMuzzleColor,
        .particleCount = config::turretMuzzleParticleCount,
        .lifetimeSeconds = config::turretMuzzleLifetimeSeconds,
        .initialSize = config::turretMuzzleInitialSize,
        .finalSize = config::turretMuzzleFinalSize,
        .spawnRadius = config::turretMuzzleSpawnRadius,
        .minimumVelocity = config::turretMuzzleMinimumVelocity,
        .maximumVelocity = config::turretMuzzleMaximumVelocity,
        .minimumAngularVelocity = -2.5f,
        .maximumAngularVelocity = 2.5f,
        .drawOnTop = config::turretParticlesDrawOnTop,
    };
    effects.laserBeam = {
        .texture = manifest.textureIdByName(config::turretLaserTextureName),
        .color = config::turretLaserColor,
        .width = config::turretLaserWidth,
        .lifetimeSeconds = config::turretLaserLifetimeSeconds,
        .revealSeconds = config::turretLaserGrowthSeconds,
        .fullLength = true,
        .drawOnTop = config::turretParticlesDrawOnTop,
    };
    effects.laserBeamCore = effects.laserBeam;
    effects.laserBeamCore.color = config::turretLaserCoreColor;
    effects.laserBeamCore.width = config::turretLaserCoreWidth;
    return effects;
}

float emitTurretShotParticles(
    ParticleSystem& particles,
    const TurretParticleEffects& effects,
    const rules::TurretShot& shot,
    float impactDelaySeconds)
{
    const GridPosition direction = rules::directionOffset(shot.direction);
    const Vec3 muzzle {
        static_cast<float>(shot.turretCell.x) + 0.5f +
            static_cast<float>(direction.x) * config::turretMuzzleForwardOffset,
        static_cast<float>(shot.turretCell.y) + 0.5f +
            static_cast<float>(direction.y) * config::turretMuzzleForwardOffset,
        static_cast<float>(shot.turretCell.z) + config::turretMuzzleElevation,
    };
    const Vec3 target {
        static_cast<float>(shot.targetCell.x) + 0.5f,
        static_cast<float>(shot.targetCell.y) + 0.5f,
        static_cast<float>(shot.targetCell.z) + config::turretTargetElevation,
    };
    const float beamLeadSeconds = std::max(
        effects.laserBeam.lifetimeSeconds -
            config::turretLaserAfterImpactSeconds,
        0.0f);
    const float beamDelay = std::max(
        impactDelaySeconds - beamLeadSeconds,
        0.0f);
    const float muzzleDelay = std::max(
        beamDelay - config::turretLaserAfterMuzzleSeconds,
        0.0f);

    particles.emit(muzzle, effects.muzzleGlow, muzzleDelay);
    particles.emit(muzzle, effects.muzzleFlash, muzzleDelay);
    particles.emitRibbon(
        muzzle,
        target,
        effects.laserBeam,
        beamDelay);
    particles.emitRibbon(
        muzzle,
        target,
        effects.laserBeamCore,
        beamDelay);
    return muzzleDelay;
}

} // namespace sokoban
