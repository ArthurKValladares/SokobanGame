#pragma once

#include "engine/ParticleSystem.hpp"
#include "engine/Rules.hpp"

namespace sokoban {

class AssetManifest;

struct TurretParticleEffects {
    ParticleEffectDefinition muzzleFlash;
    ParticleTrailDefinition bulletTrail;
};

[[nodiscard]] TurretParticleEffects makeTurretParticleEffects(
    const AssetManifest& manifest);

// Schedules a muzzle flash and a fast, fading trace whose final sample reaches
// the target at impactDelaySeconds. Returns the muzzle/recoil start delay.
[[nodiscard]] float emitTurretShotParticles(
    ParticleSystem& particles,
    const TurretParticleEffects& effects,
    const rules::TurretShot& shot,
    float impactDelaySeconds);

} // namespace sokoban
