#include "engine/ParticleSystem.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sokoban {

ParticleSystem::ParticleSystem()
    : ParticleSystem(std::random_device {}())
{
}

ParticleSystem::ParticleSystem(uint32_t randomSeed)
    : random_(randomSeed)
{
}

void ParticleSystem::emit(
    Vec3 origin,
    const ParticleEffectDefinition& effect)
{
    if (effect.textures.empty() || effect.particleCount == 0) {
        return;
    }

    particles_.reserve(particles_.size() + effect.particleCount);
    for (uint32_t i = 0; i < effect.particleCount; ++i) {
        const float angle = randomRange(
            0.0f, std::numbers::pi_v<float> * 2.0f);
        const float radius = std::sqrt(randomRange(0.0f, 1.0f)) *
            std::max(effect.spawnRadius, 0.0f);
        const std::size_t textureIndex = static_cast<std::size_t>(
            random_() % effect.textures.size());
        particles_.push_back({
            .position = {
                origin.x + std::cos(angle) * radius,
                origin.y + std::sin(angle) * radius,
                origin.z,
            },
            .velocity = {
                randomRange(
                    effect.minimumVelocity.x,
                    effect.maximumVelocity.x),
                randomRange(
                    effect.minimumVelocity.y,
                    effect.maximumVelocity.y),
                randomRange(
                    effect.minimumVelocity.z,
                    effect.maximumVelocity.z),
            },
            .color = effect.color,
            .texture = effect.textures[textureIndex],
            .rotationRadians = randomRange(
                0.0f, std::numbers::pi_v<float> * 2.0f),
            .angularVelocity = randomRange(
                effect.minimumAngularVelocity,
                effect.maximumAngularVelocity),
            .lifetimeSeconds = std::max(
                randomRange(
                    effect.lifetimeSeconds.x,
                    effect.lifetimeSeconds.y),
                0.001f),
            .initialSize = std::max(
                randomRange(effect.initialSize.x, effect.initialSize.y),
                0.0f),
            .finalSize = std::max(
                randomRange(effect.finalSize.x, effect.finalSize.y),
                0.0f),
            .drawOnTop = effect.drawOnTop,
        });
    }
}

void ParticleSystem::update(float dt)
{
    dt = std::max(dt, 0.0f);
    for (Particle& particle : particles_) {
        particle.ageSeconds += dt;
        particle.position.x += particle.velocity.x * dt;
        particle.position.y += particle.velocity.y * dt;
        particle.position.z += particle.velocity.z * dt;
        particle.rotationRadians += particle.angularVelocity * dt;
    }
    std::erase_if(particles_, [](const Particle& particle) {
        return particle.ageSeconds >= particle.lifetimeSeconds;
    });
}

void ParticleSystem::reset()
{
    particles_.clear();
}

void ParticleSystem::appendRenderData(RenderFrameData& frame) const
{
    frame.particles.reserve(frame.particles.size() + particles_.size());
    for (const Particle& particle : particles_) {
        const float progress = std::clamp(
            particle.ageSeconds / particle.lifetimeSeconds, 0.0f, 1.0f);
        const float fade = 1.0f - progress * progress * (3.0f - 2.0f * progress);
        const float size = particle.initialSize +
            (particle.finalSize - particle.initialSize) * progress;
        Vec4 color = particle.color;
        color.w *= fade;
        frame.particles.push_back({
            .position = particle.position,
            .size = { size, size },
            .rotationRadians = particle.rotationRadians,
            .color = color,
            .texture = particle.texture,
            .drawOnTop = particle.drawOnTop,
        });
    }
}

float ParticleSystem::randomRange(float minimum, float maximum)
{
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    return std::uniform_real_distribution<float>(minimum, maximum)(random_);
}

} // namespace sokoban
