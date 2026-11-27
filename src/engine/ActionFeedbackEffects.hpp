#pragma once

#include "engine/GameplayFeedback.hpp"
#include "engine/ParticleSystem.hpp"

#include <vector>

namespace sokoban {

class AssetManifest;

// Turns feedback cues into dust. Vulkan-free like the particle system it
// feeds: a cue says what happened and where, and this says what that looks
// like. The puffs are ordinary particles, depth-tested and never on top, so
// a wall or the stone that raised them hides them as it would hide anything.
class ActionFeedbackEffects {
public:
    ActionFeedbackEffects() = default;
    explicit ActionFeedbackEffects(const AssetManifest& manifest);

    void emit(ParticleSystem& particles, const GameplayFeedbackCue& cue) const;

private:
    // A few puffs from one spot, each with a velocity somewhere in the box
    // between the two given, in tiles a second. Sizes are ranges in tiles;
    // the shares scale the configured lifetime and opacity.
    void puff(
        ParticleSystem& particles,
        Vec3 origin,
        uint32_t count,
        Vec3 minimumVelocity,
        Vec3 maximumVelocity,
        Vec2 initialSize,
        Vec2 finalSize,
        float lifetimeShare,
        float opacityShare) const;

    std::vector<RenderTexture> textures_;
};

} // namespace sokoban
