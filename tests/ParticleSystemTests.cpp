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

void testTurretShotLayersTheMuzzleAndTrace()
{
    TEST("turretShotLayersTheMuzzleAndTrace");
    TurretParticleEffects effects;
    effects.muzzleGlow = fixedEffect();
    effects.muzzleGlow.textures = { RenderTexture { 10 } };
    effects.muzzleGlow.particleCount = 1;
    effects.muzzleFlash = fixedEffect();
    effects.muzzleFlash.textures = { RenderTexture { 11 } };
    effects.muzzleFlash.particleCount = 1;

    effects.bulletTrail = {
        .texture = RenderTexture { 12 },
        .color = { 1.0f, 0.6f, 0.1f, 0.8f },
        .width = 0.10f,
        .maxLength = 1.0f,
        .speed = 10.0f,
        .flipTextureV = true,
        .drawOnTop = true,
    };
    effects.bulletTrailCore = effects.bulletTrail;
    effects.bulletTrailCore.texture = RenderTexture { 13 };
    effects.bulletTrailCore.width = 0.04f;

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

    particles.update(0.058f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    const auto traceParticle = std::ranges::find_if(
        frame.particles,
        [](const auto& particle) {
            return particle.texture == RenderTexture { 12 };
        });
    CHECK(traceParticle != frame.particles.end());
    if (traceParticle != frame.particles.end()) {
        CHECK(traceParticle->size.y > traceParticle->size.x);
        CHECK(traceParticle->billboardAlignment.x > 0.99f);
        CHECK(traceParticle->billboardAlignmentUsesY);
        CHECK(traceParticle->flipTextureV);
    }
    CHECK(std::ranges::any_of(frame.particles, [](const auto& particle) {
        return particle.texture == RenderTexture { 13 };
    }));
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
    testTurretShotLayersTheMuzzleAndTrace();
    testRibbonRemainsOneConnectedMovingTracer();

    if (failures == 0) {
        std::cout << "ParticleSystemTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "ParticleSystemTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
