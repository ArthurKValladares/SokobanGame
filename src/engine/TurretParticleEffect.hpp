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
    ParticleEffectDefinition impactGlow;
    ParticleEffectDefinition impactCore;
};

[[nodiscard]] TurretParticleEffects makeTurretParticleEffects(
    const AssetManifest& manifest);

// Schedules a layered muzzle burst, a full-path laser, and an energy-sphere
// hit at the instant the growing ribbon reaches its destination. The beam
// remains visible across the gameplay impact. Returns the muzzle/recoil start
// delay.
[[nodiscard]] float emitTurretShotParticles(
    ParticleSystem& particles,
    const TurretParticleEffects& effects,
    const rules::TurretShot& shot,
    float impactDelaySeconds);

} // namespace sokoban
