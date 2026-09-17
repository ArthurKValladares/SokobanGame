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
    const ParticleEffectDefinition& effect,
    float delaySeconds)
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
        const float initialSize = std::max(
            randomRange(effect.initialSize.x, effect.initialSize.y),
            0.0f);
        const float finalSize = std::max(
            randomRange(effect.finalSize.x, effect.finalSize.y),
            0.0f);
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
            .billboardAlignment = effect.billboardAlignment,
            .angularVelocity = randomRange(
                effect.minimumAngularVelocity,
                effect.maximumAngularVelocity),
            .ageSeconds = -std::max(delaySeconds, 0.0f),
            .lifetimeSeconds = std::max(
                randomRange(
                    effect.lifetimeSeconds.x,
                    effect.lifetimeSeconds.y),
                0.001f),
            .initialSize = {
                initialSize * std::max(effect.initialSizeScale.x, 0.0f),
                initialSize * std::max(effect.initialSizeScale.y, 0.0f),
            },
            .finalSize = {
                finalSize * std::max(effect.finalSizeScale.x, 0.0f),
                finalSize * std::max(effect.finalSizeScale.y, 0.0f),
            },
            .drawOnTop = effect.drawOnTop,
        });
    }
}

void ParticleSystem::emitTrail(
    Vec3 start,
    Vec3 end,
    const ParticleTrailDefinition& trail,
    float delaySeconds)
{
    const Vec3 delta {
        end.x - start.x,
        end.y - start.y,
        end.z - start.z,
    };
    const float distance = std::sqrt(
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    const float spacing = std::max(trail.spacing, 0.01f);
    const uint32_t segmentCount = std::max(
        1U,
        static_cast<uint32_t>(std::ceil(distance / spacing)));
    const float speed = std::max(trail.speed, 0.001f);
    ParticleEffectDefinition sample = trail.particle;
    sample.particleCount = 1;
    sample.spawnRadius = 0.0f;
    sample.billboardAlignment = distance > 0.0001f
        ? Vec3 { delta.x / distance, delta.y / distance, delta.z / distance }
        : Vec3 {};

    for (uint32_t i = 0; i <= segmentCount; ++i) {
        const float progress = static_cast<float>(i) /
            static_cast<float>(segmentCount);
        emit(
            {
                start.x + delta.x * progress,
                start.y + delta.y * progress,
                start.z + delta.z * progress,
            },
            sample,
            std::max(delaySeconds, 0.0f) +
                distance * progress / speed);
    }
}

void ParticleSystem::emitRibbon(
    Vec3 start,
    Vec3 end,
    const ParticleRibbonDefinition& ribbon,
    float delaySeconds)
{
    const Vec3 delta = end - start;
    const float distance = length(delta);
    if (ribbon.texture.isNone() || distance <= 0.0001f ||
        ribbon.width <= 0.0f || ribbon.maxLength <= 0.0f) {
        return;
    }
    ribbons_.push_back({
        .start = start,
        .direction = delta / distance,
        .color = ribbon.color,
        .texture = ribbon.texture,
        .distance = distance,
        .width = ribbon.width,
        .maxLength = ribbon.maxLength,
        .speed = std::max(ribbon.speed, 0.001f),
        .ageSeconds = -std::max(delaySeconds, 0.0f),
        .flipTextureV = ribbon.flipTextureV,
        .drawOnTop = ribbon.drawOnTop,
    });
}

void ParticleSystem::update(float dt)
{
    dt = std::max(dt, 0.0f);
    for (Particle& particle : particles_) {
        const float previousAge = particle.ageSeconds;
        particle.ageSeconds += dt;
        const float activeTime = std::max(particle.ageSeconds, 0.0f) -
            std::max(previousAge, 0.0f);
        particle.position.x += particle.velocity.x * activeTime;
        particle.position.y += particle.velocity.y * activeTime;
        particle.position.z += particle.velocity.z * activeTime;
        particle.rotationRadians += particle.angularVelocity * activeTime;
    }
    std::erase_if(particles_, [](const Particle& particle) {
        return particle.ageSeconds >= particle.lifetimeSeconds;
    });
    for (Ribbon& ribbon : ribbons_) {
        ribbon.ageSeconds += dt;
    }
    std::erase_if(ribbons_, [](const Ribbon& ribbon) {
        const float finishSeconds =
            (ribbon.distance + ribbon.maxLength) / ribbon.speed;
        return ribbon.ageSeconds >= finishSeconds;
    });
}

void ParticleSystem::reset()
{
    particles_.clear();
    ribbons_.clear();
}

void ParticleSystem::appendRenderData(RenderFrameData& frame) const
{
    frame.particles.reserve(
        frame.particles.size() + particles_.size() + ribbons_.size());
    for (const Particle& particle : particles_) {
        if (particle.ageSeconds < 0.0f) {
            continue;
        }
        const float progress = std::clamp(
            particle.ageSeconds / particle.lifetimeSeconds, 0.0f, 1.0f);
        const float fade = 1.0f - progress * progress * (3.0f - 2.0f * progress);
        const Vec2 size = lerp(
            particle.initialSize, particle.finalSize, progress);
        Vec4 color = particle.color;
        color.w *= fade;
        frame.particles.push_back({
            .position = particle.position,
            .size = size,
            .rotationRadians = particle.rotationRadians,
            .billboardAlignment = particle.billboardAlignment,
            .color = color,
            .texture = particle.texture,
            .drawOnTop = particle.drawOnTop,
        });
    }
    for (const Ribbon& ribbon : ribbons_) {
        if (ribbon.ageSeconds <= 0.0f) {
            continue;
        }
        const float virtualHead = ribbon.ageSeconds * ribbon.speed;
        const float headDistance = std::min(virtualHead, ribbon.distance);
        const float tailDistance = std::clamp(
            virtualHead - ribbon.maxLength, 0.0f, ribbon.distance);
        const float visibleLength = headDistance - tailDistance;
        if (visibleLength <= 0.0001f) {
            continue;
        }
        const float centerDistance =
            (headDistance + tailDistance) * 0.5f;
        frame.particles.push_back({
            .position = ribbon.start + ribbon.direction * centerDistance,
            .size = { ribbon.width, visibleLength },
            .billboardAlignment = ribbon.direction,
            .billboardAlignmentUsesY = true,
            .flipTextureV = ribbon.flipTextureV,
            .color = ribbon.color,
            .texture = ribbon.texture,
            .drawOnTop = ribbon.drawOnTop,
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
