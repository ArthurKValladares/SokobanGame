#pragma once

#include "engine/ParticleSystem.hpp"
#include "engine/Rules.hpp"

namespace sokoban {

class AssetManifest;

struct TurretParticleEffects {
    ParticleEffectDefinition muzzleGlow;
    ParticleEffectDefinition muzzleFlash;
    ParticleRibbonDefinition laserBeam;
    ParticleRibbonDefinition laserBeamCore;
};

[[nodiscard]] TurretParticleEffects makeTurretParticleEffects(
    const AssetManifest& manifest);

// Schedules a layered muzzle burst and a full-path laser that remains visible
// across the gameplay impact. Returns the muzzle/recoil start delay.
[[nodiscard]] float emitTurretShotParticles(
    ParticleSystem& particles,
    const TurretParticleEffects& effects,
    const rules::TurretShot& shot,
    float impactDelaySeconds);

} // namespace sokoban
