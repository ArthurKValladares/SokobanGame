#include "TestHarness.hpp"

#include "engine/ParticleSystem.hpp"

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
    particles.emitTrail(
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        trail);
    CHECK(particles.activeParticleCount() == 5);

    RenderFrameData frame;
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 1);
    CHECK(near(frame.particles.front().position.x, 0.0f));

    particles.update(0.11f);
    frame.particles.clear();
    particles.appendRenderData(frame);
    CHECK(frame.particles.size() == 5);
    CHECK(near(frame.particles.back().position.x, 1.0f));
}

} // namespace

int main()
{
    testBurstSimulationAndRenderData();
    testEmptyEffectsAndReset();
    testDelayedParticlesDoNotAgeOrMoveBeforeTheyAppear();
    testTrailSamplesAppearAlongTheLineAtProjectileSpeed();

    if (failures == 0) {
        std::cout << "ParticleSystemTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "ParticleSystemTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
