#include "TestHarness.hpp"

#include "engine/ParticleSystem.hpp"
#include "engine/TurretParticleEffect.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

using namespace sokoban;

bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

ParticleEffectDefinition fixedEffect()
{
    return {
        .textures = { RenderTexture { 2 }, RenderTexture { 3 } },
        .color = { 0.72f, 0.97f, 1.0f, 0.8f },
        .emissiveStrength = 1.7f,
        .particleCount = 4,
        .lifetimeSeconds = { 0.5f, 0.5f },
        .initialSize = { 0.4f, 0.4f },
        .finalSize = { 0.8f, 0.8f },
        .minimumVelocity = { 0.0f, 0.0f, 0.5f },
        .maximumVelocity = { 0.0f, 0.0f, 0.5f },
        .drawOnTop = true,
    };
}

void testBurstSimulationAndRenderData()
{
    ParticleSystem particles(7);
    particles.emit({ 2.5f, 3.5f, 1.0f }, fixedEffect());
    CHECK(particles.activeParticleCount() == 4);

    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 4);
    for (const RenderFrameData::Particle& particle : frame.particles) {
        CHECK(near(particle.position.x, 2.5f));
        CHECK(near(particle.position.y, 3.5f));
        CHECK(near(particle.position.z, 1.0f));
        CHECK(near(particle.size.x, 0.4f));
        CHECK(near(particle.color.w, 0.8f));
        CHECK(near(particle.emissiveStrength, 1.7f));
        CHECK(particle.drawOnTop);
        CHECK(particle.texture == RenderTexture { 2 } ||
            particle.texture == RenderTexture { 3 });
    }

    particles.update(0.25f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 4);
    for (const RenderFrameData::Particle& particle : frame.particles) {
        CHECK(near(particle.position.z, 1.125f));
        CHECK(near(particle.size.x, 0.6f));
        CHECK(near(particle.color.w, 0.4f));
    }

    particles.update(0.25f);
    CHECK(particles.activeParticleCount() == 0);
}

void testEmptyEffectsAndReset()
{
    ParticleSystem particles(3);
    ParticleEffectDefinition empty;
    empty.particleCount = 5;
    particles.emit({}, empty);
    CHECK(particles.activeParticleCount() == 0);

    particles.emit({}, fixedEffect());
    CHECK(particles.activeParticleCount() == 4);
    particles.update(-1.0f);
    CHECK(particles.activeParticleCount() == 4);
    particles.reset();
    CHECK(particles.activeParticleCount() == 0);
}

void testDelayedParticlesDoNotAgeOrMoveBeforeTheyAppear()
{
    TEST("delayedParticlesDoNotAgeOrMoveBeforeTheyAppear");
    ParticleSystem particles(11);
    particles.emit({ 1.0f, 2.0f, 1.0f }, fixedEffect(), 0.25f);

    particles.update(0.125f);
    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.empty());

    particles.update(0.25f);
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 4);
    for (const RenderFrameData::Particle& particle : frame.particles) {
        // Only the 0.125 seconds after the delay contributes movement.
        CHECK(near(particle.position.z, 1.0625f));
    }
}

void testTrailSamplesAppearAlongTheLineAtProjectileSpeed()
{
    TEST("trailSamplesAppearAlongTheLineAtProjectileSpeed");
    ParticleSystem particles(13);
    ParticleTrailDefinition trail {
        .particle = fixedEffect(),
        .spacing = 0.25f,
        .speed = 10.0f,
    };
    trail.particle.initialSize = { 1.0f, 1.0f };
    trail.particle.finalSize = { 1.0f, 1.0f };
    trail.particle.initialSizeScale = { 0.35f, 0.12f };
    trail.particle.finalSizeScale = { 0.35f, 0.02f };
    particles.emitTrail(
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        trail);
    CHECK(particles.activeParticleCount() == 5);

    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    CHECK(near(frame.particles.front().position.x, 0.0f));
    CHECK(near(frame.particles.front().size.x, 0.35f));
    CHECK(near(frame.particles.front().size.y, 0.12f));
    CHECK((frame.particles.front().billboardAlignment ==
        Vec3 { 1.0f, 0.0f, 0.0f }));

    particles.update(0.11f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 5);
    CHECK(near(frame.particles.back().position.x, 1.0f));
    CHECK(near(frame.particles.front().size.x, 0.35f));
    CHECK(frame.particles.front().size.y < frame.particles.back().size.y);
    CHECK(frame.particles.front().color.w < frame.particles.back().color.w);
}

void testTurretShotLayersTheMuzzleAndFullPathLaser()
{
    TEST("turretShotLayersTheMuzzleAndFullPathLaser");
    TurretParticleEffects effects;
    effects.muzzleGlow = fixedEffect();
    effects.muzzleGlow.textures = { RenderTexture { 10 } };
    effects.muzzleGlow.particleCount = 1;
    effects.muzzleFlash = fixedEffect();
    effects.muzzleFlash.textures = { RenderTexture { 11 } };
    effects.muzzleFlash.particleCount = 1;

    effects.laserBeam = {
        .texture = RenderTexture { 12 },
        .color = { 1.0f, 0.6f, 0.1f, 0.8f },
        .emissiveStrength = 2.25f,
        .textureNineSlice =
            NineSlice::symmetricFitTargetWidth({ 0.2f, 0.2f }),
        .width = 0.40f,
        .lifetimeSeconds = 0.20f,
        .revealSeconds = 0.06f,
        .fullLength = true,
        .drawOnTop = true,
    };
    effects.laserBeamCore = effects.laserBeam;
    effects.laserBeamCore.texture = RenderTexture { 13 };
    effects.laserBeamCore.width = 0.12f;
    effects.impactGlow = fixedEffect();
    effects.impactGlow.textures = { RenderTexture { 14 } };
    effects.impactGlow.color = effects.laserBeam.color;
    effects.impactGlow.color.w = 1.0f;
    effects.impactGlow.emissiveStrength = 2.6f;
    effects.impactGlow.particleCount = 1;
    effects.impactGlow.drawOrder = 2;
    effects.impactGlow.initialSize = { 0.4f, 0.4f };
    effects.impactGlow.finalSize = { 1.6f, 1.6f };
    effects.impactGlow.minimumVelocity = {};
    effects.impactGlow.maximumVelocity = {};
    effects.impactCore = fixedEffect();
    effects.impactCore.textures = { RenderTexture { 15 } };
    effects.impactCore.color = { 1.0f, 0.96f, 0.7f, 0.96f };
    effects.impactCore.color.w = 1.0f;
    effects.impactCore.emissiveStrength = 4.2f;
    effects.impactCore.particleCount = 1;
    effects.impactCore.drawOrder = 3;
    effects.impactCore.minimumVelocity = {};
    effects.impactCore.maximumVelocity = {};

    ParticleSystem particles(17);
    const float muzzleDelay = emitTurretShotParticles(
        particles,
        effects,
        rules::TurretShot {
            .turretCell = { 0, 0, 1 },
            .targetCell = { 2, 0, 1 },
            .direction = MoveDirection::Right,
        },
        0.25f);

    particles.update(muzzleDelay + 0.001f);
    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 2);
    CHECK(std::ranges::any_of(frame.particles, [](const auto& particle) {
        return particle.texture == RenderTexture { 10 };
    }));
    CHECK(std::ranges::any_of(frame.particles, [](const auto& particle) {
        return particle.texture == RenderTexture { 11 };
    }));

    // One millisecond before the beam reaches full length, neither hit layer
    // is visible yet.
    particles.update(0.066f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    const auto laserParticle = std::ranges::find_if(
        frame.particles,
        [](const auto& particle) {
            return particle.texture == RenderTexture { 12 };
        });
    CHECK(laserParticle != frame.particles.end());
    if (laserParticle != frame.particles.end()) {
        CHECK(near(laserParticle->size.x, 0.40f));
        CHECK(laserParticle->size.y > 1.4f);
        CHECK(laserParticle->billboardAlignment.x > 0.99f);
        CHECK(laserParticle->billboardAlignmentUsesY);
        CHECK(!laserParticle->flipTextureV);
        CHECK(near(laserParticle->emissiveStrength, 2.25f));
        CHECK(near(laserParticle->textureNineSlice.sourceBorders.x, 0.2f));
        CHECK(near(laserParticle->textureNineSlice.sourceBorders.y, 0.2f));
        CHECK(laserParticle->size.y < 1.48f);
    }
    CHECK(std::ranges::any_of(frame.particles, [](const auto& particle) {
        return particle.texture == RenderTexture { 13 };
    }));
    CHECK(std::ranges::none_of(frame.particles, [](const auto& particle) {
        return particle.texture == RenderTexture { 14 } ||
            particle.texture == RenderTexture { 15 };
    }));

    // Crossing the remaining millisecond completes the ribbon and begins
    // both centered energy-sphere layers in the same simulation update.
    particles.update(0.002f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    const auto arrivedLaser = std::ranges::find_if(
        frame.particles,
        [](const auto& particle) {
            return particle.texture == RenderTexture { 12 };
        });
    const auto impactGlow = std::ranges::find_if(
        frame.particles,
        [](const auto& particle) {
            return particle.texture == RenderTexture { 14 };
        });
    const auto impactCore = std::ranges::find_if(
        frame.particles,
        [](const auto& particle) {
            return particle.texture == RenderTexture { 15 };
        });
    CHECK(arrivedLaser != frame.particles.end());
    if (arrivedLaser != frame.particles.end()) {
        CHECK(near(
            arrivedLaser->size.y,
            std::sqrt(1.48f * 1.48f + 0.06f * 0.06f)));
    }
    CHECK(impactGlow != frame.particles.end());
    CHECK(impactCore != frame.particles.end());
    if (impactGlow != frame.particles.end()) {
        CHECK(near(impactGlow->position.x, 2.5f));
        CHECK(near(impactGlow->position.y, 0.5f));
        CHECK(near(impactGlow->position.z, 1.58f));
        CHECK(near(impactGlow->emissiveStrength, 2.6f));
        CHECK(impactGlow->color.w > 0.999f);
        CHECK(impactGlow->size.x > 0.4f);
        CHECK(impactGlow->drawOrder == 2);
    }
    if (impactCore != frame.particles.end()) {
        CHECK(near(impactCore->position.x, 2.5f));
        CHECK(near(impactCore->position.y, 0.5f));
        CHECK(near(impactCore->position.z, 1.58f));
        CHECK(near(impactCore->emissiveStrength, 4.2f));
        CHECK(impactCore->color.w > 0.999f);
        CHECK(impactCore->drawOrder == 3);
    }
}

void testFullLengthRibbonStaysVisibleUntilItsLifetimeEnds()
{
    TEST("fullLengthRibbonStaysVisibleUntilItsLifetimeEnds");
    ParticleSystem particles(23);
    particles.emitRibbon(
        { 0.0f, 0.0f, 0.0f },
        { 2.0f, 0.0f, 0.0f },
        ParticleRibbonDefinition {
            .texture = RenderTexture { 21 },
            .color = { 1.0f, 0.7f, 0.2f, 0.9f },
            .textureNineSlice =
                NineSlice::symmetricFitTargetWidth({ 0.2f, 0.2f }),
            .width = 0.6f,
            .lifetimeSeconds = 0.5f,
            .fullLength = true,
            .drawOnTop = true,
        });

    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    if (!frame.particles.empty()) {
        CHECK(near(frame.particles[0].position.x, 1.0f));
        CHECK(near(frame.particles[0].size.x, 0.6f));
        CHECK(near(frame.particles[0].size.y, 2.0f));
        CHECK(near(
            frame.particles[0].textureNineSlice.sourceBorders.x, 0.2f));
        CHECK(near(
            frame.particles[0].textureNineSlice.sourceBorders.y, 0.2f));
    }

    particles.update(0.49f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    if (!frame.particles.empty()) {
        CHECK(near(frame.particles[0].position.x, 1.0f));
        CHECK(near(frame.particles[0].size.y, 2.0f));
    }

    particles.update(0.02f);
    CHECK(particles.activeRibbonCount() == 0);
}

void testFullLengthRibbonGrowsFromItsOriginThenHolds()
{
    TEST("fullLengthRibbonGrowsFromItsOriginThenHolds");
    ParticleSystem particles(29);
    particles.emitRibbon(
        { 0.0f, 0.0f, 0.0f },
        { 2.0f, 0.0f, 0.0f },
        ParticleRibbonDefinition {
            .texture = RenderTexture { 22 },
            .color = { 1.0f, 0.7f, 0.2f, 0.9f },
            .width = 0.6f,
            .lifetimeSeconds = 0.5f,
            .revealSeconds = 0.1f,
            .fullLength = true,
            .drawOnTop = true,
        });

    particles.update(0.05f);
    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    if (!frame.particles.empty()) {
        CHECK(near(frame.particles[0].position.x, 0.5f));
        CHECK(near(frame.particles[0].size.y, 1.0f));
    }

    particles.update(0.05f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    if (!frame.particles.empty()) {
        CHECK(near(frame.particles[0].position.x, 1.0f));
        CHECK(near(frame.particles[0].size.y, 2.0f));
    }

    particles.update(0.2f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    if (!frame.particles.empty()) {
        CHECK(near(frame.particles[0].position.x, 1.0f));
        CHECK(near(frame.particles[0].size.y, 2.0f));
    }
}

void testRibbonRemainsOneConnectedMovingTracer()
{
    TEST("ribbonRemainsOneConnectedMovingTracer");
    ParticleSystem particles(19);
    particles.emitRibbon(
        { 0.0f, 0.0f, 0.0f },
        { 2.0f, 0.0f, 0.0f },
        ParticleRibbonDefinition {
            .texture = RenderTexture { 20 },
            .color = { 1.0f, 0.8f, 0.2f, 0.9f },
            .width = 0.12f,
            .maxLength = 0.6f,
            .speed = 2.0f,
            .drawOnTop = true,
        });
    CHECK(particles.activeRibbonCount() == 1);

    particles.update(0.25f);
    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    CHECK(near(frame.particles[0].position.x, 0.25f));
    CHECK(near(frame.particles[0].size.x, 0.12f));
    CHECK(near(frame.particles[0].size.y, 0.5f));
    CHECK(frame.particles[0].billboardAlignmentUsesY);

    particles.update(0.25f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    // The head is at x=1 and the tail has advanced to x=0.4, represented by
    // one quad spanning the entire 0.6-unit wake rather than point samples.
    CHECK(near(frame.particles[0].position.x, 0.7f));
    CHECK(near(frame.particles[0].size.y, 0.6f));

    particles.update(0.8f);
    CHECK(particles.activeRibbonCount() == 0);
}

} // namespace

int main()
{
    testBurstSimulationAndRenderData();
    testEmptyEffectsAndReset();
    testDelayedParticlesDoNotAgeOrMoveBeforeTheyAppear();
    testTrailSamplesAppearAlongTheLineAtProjectileSpeed();
    testTurretShotLayersTheMuzzleAndFullPathLaser();
    testRibbonRemainsOneConnectedMovingTracer();
    testFullLengthRibbonStaysVisibleUntilItsLifetimeEnds();
    testFullLengthRibbonGrowsFromItsOriginThenHolds();

    if (failures == 0) {
        std::cout << "ParticleSystemTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "ParticleSystemTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
