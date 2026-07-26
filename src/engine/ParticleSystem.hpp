#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace sokoban {

struct ParticleEffectDefinition {
    std::vector<RenderTexture> textures;
    Vec4 color { 1.0f, 1.0f, 1.0f, 1.0f };
    uint32_t particleCount = 1;
    Vec2 lifetimeSeconds { 0.5f, 0.5f };
    Vec2 initialSize { 0.5f, 0.5f };
    Vec2 finalSize { 0.5f, 0.5f };
    float spawnRadius = 0.0f;
    Vec3 minimumVelocity {};
    Vec3 maximumVelocity {};
    float minimumAngularVelocity = 0.0f;
    float maximumAngularVelocity = 0.0f;
    bool drawOnTop = false;
};

// Vulkan-free particle simulation. Effects describe an emission burst while
// ParticleSystem owns each live particle's randomized state and lifetime.
class ParticleSystem {
public:
    ParticleSystem();
    explicit ParticleSystem(uint32_t randomSeed);

    void emit(Vec3 origin, const ParticleEffectDefinition& effect);
    void update(float dt);
    void reset();

    void appendRenderData(RenderFrameData& frame) const;
    [[nodiscard]] std::size_t activeParticleCount() const
    {
        return particles_.size();
    }

private:
    struct Particle {
        Vec3 position {};
        Vec3 velocity {};
        Vec4 color {};
        RenderTexture texture {};
        float rotationRadians = 0.0f;
        float angularVelocity = 0.0f;
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 1.0f;
        float initialSize = 1.0f;
        float finalSize = 1.0f;
        bool drawOnTop = false;
    };

    [[nodiscard]] float randomRange(float minimum, float maximum);

    std::mt19937 random_;
    std::vector<Particle> particles_;
};

} // namespace sokoban
