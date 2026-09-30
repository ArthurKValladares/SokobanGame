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
    // Multiplies RGB after simulation while leaving opacity untouched. Values
    // above one carry real HDR radiance into the renderer and can bloom.
    float emissiveStrength = 1.0f;
    uint32_t particleCount = 1;
    Vec2 lifetimeSeconds { 0.5f, 0.5f };
    Vec2 initialSize { 0.5f, 0.5f };
    Vec2 finalSize { 0.5f, 0.5f };
    // The size fields above remain scalar random ranges. These per-axis
    // multipliers turn the resulting billboard into an ellipse or streak and
    // may differ at the two ends of its life to produce a tapered wake.
    Vec2 initialSizeScale { 1.0f, 1.0f };
    Vec2 finalSizeScale { 1.0f, 1.0f };
    // A non-zero world direction aligns the billboard's X axis with that
    // direction after camera projection. Trail emission fills this in from
    // its path; ordinary bursts retain their randomized screen rotation.
    Vec3 billboardAlignment {};
    float spawnRadius = 0.0f;
    Vec3 minimumVelocity {};
    Vec3 maximumVelocity {};
    float minimumAngularVelocity = 0.0f;
    float maximumAngularVelocity = 0.0f;
    bool drawOnTop = false;
    // Higher overlay orders render later among particles in the same depth
    // mode. This is for layered effects whose visual stacking must not vary
    // with camera direction.
    int32_t drawOrder = 0;
};

// A reusable line emitter built from ordinary particles. Each path-aligned
// sample becomes visible when a virtual projectile reaches it, then fades and
// changes size on its own, leaving a tapered trace behind the moving head.
struct ParticleTrailDefinition {
    ParticleEffectDefinition particle;
    float spacing = 0.15f;
    float speed = 24.0f;
};

// A single connected path effect. By default its head advances at projectile
// speed while its tail follows at most maxLength world units behind. Setting
// fullLength keeps the tail anchored at start for lifetimeSeconds. With a
// positive revealSeconds its head grows toward end over that short interval;
// zero reveals the complete path immediately. Unlike emitTrail's burst of
// independent samples, either mode remains one textured quad and cannot break
// into visible beads. The geometry's V axis runs from start/tail (0) to
// end/head (1), with flipTextureV available for asymmetric authored textures.
struct ParticleRibbonDefinition {
    RenderTexture texture = noTexture;
    Vec4 color { 1.0f, 1.0f, 1.0f, 1.0f };
    float emissiveStrength = 1.0f;
    float width = 0.12f;
    float maxLength = 6.0f;
    float speed = 24.0f;
    float lifetimeSeconds = 0.15f;
    float revealSeconds = 0.0f;
    bool fullLength = false;
    bool flipTextureV = false;
    bool drawOnTop = false;
    int32_t drawOrder = 0;
};

// Vulkan-free particle simulation. Effects describe an emission burst while
// ParticleSystem owns each live particle's randomized state and lifetime.
class ParticleSystem {
public:
    ParticleSystem();
    explicit ParticleSystem(uint32_t randomSeed);

    void emit(
        Vec3 origin,
        const ParticleEffectDefinition& effect,
        float delaySeconds = 0.0f);
    void emitTrail(
        Vec3 start,
        Vec3 end,
        const ParticleTrailDefinition& trail,
        float delaySeconds = 0.0f);
    void emitRibbon(
        Vec3 start,
        Vec3 end,
        const ParticleRibbonDefinition& ribbon,
        float delaySeconds = 0.0f);
    void update(float dt);
    void reset();

    void appendRenderData(RenderFrameData& frame) const;
    [[nodiscard]] std::size_t activeParticleCount() const
    {
        return particles_.size();
    }
    [[nodiscard]] std::size_t activeRibbonCount() const
    {
        return ribbons_.size();
    }

private:
    struct Particle {
        Vec3 position {};
        Vec3 velocity {};
        Vec4 color {};
        float emissiveStrength = 1.0f;
        RenderTexture texture {};
        float rotationRadians = 0.0f;
        Vec3 billboardAlignment {};
        float angularVelocity = 0.0f;
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 1.0f;
        Vec2 initialSize { 1.0f, 1.0f };
        Vec2 finalSize { 1.0f, 1.0f };
        bool drawOnTop = false;
        int32_t drawOrder = 0;
    };

    struct Ribbon {
        Vec3 start {};
        Vec3 direction {};
        Vec4 color {};
        float emissiveStrength = 1.0f;
        RenderTexture texture = noTexture;
        float distance = 0.0f;
        float width = 0.12f;
        float maxLength = 6.0f;
        float speed = 24.0f;
        float lifetimeSeconds = 0.15f;
        float revealSeconds = 0.0f;
        float ageSeconds = 0.0f;
        bool fullLength = false;
        bool flipTextureV = false;
        bool drawOnTop = false;
        int32_t drawOrder = 0;
    };

    [[nodiscard]] float randomRange(float minimum, float maximum);

    std::mt19937 random_;
    std::vector<Particle> particles_;
    std::vector<Ribbon> ribbons_;
};

} // namespace sokoban
