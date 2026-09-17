#include "engine/TurretParticleEffect.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/ParticleConfig.hpp"

#include <algorithm>
#include <cmath>

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
    effects.bulletTrail = {
        .texture = manifest.textureIdByName(config::turretTrailTextureName),
        .color = config::turretTrailColor,
        .width = config::turretTrailWidth,
        .maxLength = config::turretTrailMaximumLength,
        .speed = config::turretBulletSpeed,
        .flipTextureV = true,
        .drawOnTop = config::turretParticlesDrawOnTop,
    };
    effects.bulletTrailCore = effects.bulletTrail;
    effects.bulletTrailCore.color = config::turretTrailCoreColor;
    effects.bulletTrailCore.width = config::turretTrailCoreWidth;
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
    const float dx = target.x - muzzle.x;
    const float dy = target.y - muzzle.y;
    const float dz = target.z - muzzle.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float travelSeconds = distance /
        std::max(effects.bulletTrail.speed, 0.001f);
    const float muzzleDelay = std::max(
        impactDelaySeconds - travelSeconds -
            config::turretTrailAfterMuzzleSeconds,
        0.0f);

    particles.emit(muzzle, effects.muzzleGlow, muzzleDelay);
    particles.emit(muzzle, effects.muzzleFlash, muzzleDelay);
    particles.emitRibbon(
        muzzle,
        target,
        effects.bulletTrail,
        muzzleDelay + config::turretTrailAfterMuzzleSeconds);
    particles.emitRibbon(
        muzzle,
        target,
        effects.bulletTrailCore,
        muzzleDelay + config::turretTrailAfterMuzzleSeconds);
    return muzzleDelay;
}

} // namespace sokoban
