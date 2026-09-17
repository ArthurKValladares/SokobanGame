#pragma once

#include "engine/ParticleSystem.hpp"
#include "engine/Rules.hpp"

namespace sokoban {

class AssetManifest;

struct TurretParticleEffects {
    ParticleEffectDefinition muzzleGlow;
    ParticleEffectDefinition muzzleFlash;
    ParticleRibbonDefinition bulletTrail;
    ParticleRibbonDefinition bulletTrailCore;
};

[[nodiscard]] TurretParticleEffects makeTurretParticleEffects(
    const AssetManifest& manifest);

// Schedules a layered muzzle burst and a fast, tapered ribbon whose head reaches
// the target at impactDelaySeconds. Returns the muzzle/recoil start delay.
[[nodiscard]] float emitTurretShotParticles(
    ParticleSystem& particles,
    const TurretParticleEffects& effects,
    const rules::TurretShot& shot,
    float impactDelaySeconds);

} // namespace sokoban
