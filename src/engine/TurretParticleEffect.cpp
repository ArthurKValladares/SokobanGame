#include "engine/TurretParticleEffect.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/ParticleConfig.hpp"

#include <algorithm>
#include <utility>

namespace sokoban {

TurretParticleEffects makeTurretParticleEffects(const AssetManifest& manifest)
{
    TurretParticleEffects effects;
    effects.muzzleGlow = {
        .textures = {
            manifest.textureIdByName(config::turretGlowTextureName),
        },
        .color = config::turretMuzzleGlowColor,
        .emissiveStrength = config::turretMuzzleGlowEmissiveStrength,
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
        .emissiveStrength = config::turretMuzzleEmissiveStrength,
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
        .emissiveStrength = config::turretLaserEmissiveStrength,
        .textureNineSlice = config::turretLaserTextureNineSlice,
        .width = config::turretLaserWidth,
        .lifetimeSeconds = config::turretLaserLifetimeSeconds,
        .revealSeconds = config::turretLaserGrowthSeconds,
        .fullLength = true,
        .drawOnTop = config::turretParticlesDrawOnTop,
        .drawOrder = config::turretLaserDrawOrder,
    };
    effects.laserBeamCore = effects.laserBeam;
    effects.laserBeamCore.color = config::turretLaserCoreColor;
    effects.laserBeamCore.emissiveStrength =
        config::turretLaserCoreEmissiveStrength;
    effects.laserBeamCore.width = config::turretLaserCoreWidth;
    effects.laserBeamCore.drawOrder = config::turretLaserCoreDrawOrder;
    effects.impactGlow = {
        .textures = {
            manifest.textureIdByName(config::turretGlowTextureName),
        },
        .color = config::turretImpactColor,
        .emissiveStrength = config::turretImpactEmissiveStrength,
        .particleCount = 1,
        .lifetimeSeconds = config::turretImpactLifetimeSeconds,
        .initialSize = config::turretImpactInitialSize,
        .finalSize = config::turretImpactFinalSize,
        .drawOnTop = config::turretParticlesDrawOnTop,
        .drawOrder = config::turretImpactDrawOrder,
    };
    effects.impactCore = {
        .textures = {
            manifest.textureIdByName(config::turretGlowTextureName),
        },
        .color = config::turretImpactCoreColor,
        .emissiveStrength = config::turretImpactCoreEmissiveStrength,
        .particleCount = 1,
        .lifetimeSeconds = config::turretImpactCoreLifetimeSeconds,
        .initialSize = config::turretImpactCoreInitialSize,
        .finalSize = config::turretImpactCoreFinalSize,
        .drawOnTop = config::turretParticlesDrawOnTop,
        .drawOrder = config::turretImpactCoreDrawOrder,
    };
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
    const auto segmentPoints = [&](std::size_t index) {
        const auto& segment = shot.beamSegments[index];
        const Vec3 from = index == 0 ? muzzle
                                     : Vec3 {
                                           static_cast<float>(segment.from.x) +
                                               0.5f + static_cast<float>(segment.fromEdge.x) * 0.5f,
                                           static_cast<float>(segment.from.y) +
                                               0.5f + static_cast<float>(segment.fromEdge.y) * 0.5f,
                                           static_cast<float>(segment.from.z) +
                                               config::turretMuzzleElevation,
                                       };
        const Vec3 to = index + 1 == shot.beamSegments.size()
            ? target
            : Vec3 {
                  static_cast<float>(segment.to.x) + 0.5f +
                      static_cast<float>(segment.toEdge.x) * 0.5f,
                  static_cast<float>(segment.to.y) + 0.5f +
                      static_cast<float>(segment.toEdge.y) * 0.5f,
                  static_cast<float>(segment.to.z) +
                      config::turretMuzzleElevation,
              };
        return std::pair { from, to };
    };
    float beamLength = shot.beamSegments.empty() ? length(target - muzzle) : 0.0f;
    for (std::size_t index = 0; index < shot.beamSegments.size(); ++index) {
        const auto [from, to] = segmentPoints(index);
        beamLength += length(to - from);
    }
    const float beamSpeed = std::max(effects.laserBeam.speed, 0.001f);
    const float beamTravelSeconds = effects.laserBeam.fullLength
        ? std::max(effects.laserBeam.revealSeconds, 0.0f)
        : beamLength / beamSpeed;
    const float hitDelay = beamDelay + beamTravelSeconds;

    particles.emit(muzzle, effects.muzzleGlow, muzzleDelay);
    particles.emit(muzzle, effects.muzzleFlash, muzzleDelay);
    if (shot.beamSegments.empty()) {
        particles.emitRibbon(muzzle, target, effects.laserBeam, beamDelay);
        particles.emitRibbon(muzzle, target, effects.laserBeamCore, beamDelay);
    } else {
        float traveled = 0.0f;
        for (std::size_t index = 0; index < shot.beamSegments.size(); ++index) {
            const auto [from, to] = segmentPoints(index);
            const float delay = beamDelay + (effects.laserBeam.fullLength ? 0.0f : traveled / beamSpeed);
            particles.emitRibbon(from, to, effects.laserBeam, delay);
            particles.emitRibbon(from, to, effects.laserBeamCore, delay);
            traveled += length(to - from);
        }
    }
    particles.emit(target, effects.impactGlow, hitDelay);
    particles.emit(target, effects.impactCore, hitDelay);
    return muzzleDelay;
}

} // namespace sokoban
