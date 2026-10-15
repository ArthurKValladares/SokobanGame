#pragma once

#include "engine/MirrorParticleEffect.hpp"
#include "engine/TurretParticleEffect.hpp"
#include "engine/GameplayPresentation.hpp"

#include <string_view>

namespace sokoban {

class AssetManifest;
class AnimationCatalog;
class GroundGeometryCache;
class PresentationSettings;

[[nodiscard]] constexpr bool isPerformanceEffectScenario(std::string_view name)
{
    return name == "mirror-swap" || name == "witch-swap" ||
        name == "turret-volley" || name == "portals" ||
        name == "special-blocks" || name == "mixed-stress";
}

[[nodiscard]] constexpr bool isEvidenceEffectScenario(std::string_view name)
{
    return isPerformanceEffectScenario(name) || name == "rock-scratches";
}

// A frozen gameplay capture made from real pushes, with old and fresh marks
// around a corner and water bordering the ground receiver.
class RockScratchEvidenceFixture {
public:
    explicit RockScratchEvidenceFixture(const AnimationCatalog* animations = nullptr);
    [[nodiscard]] RenderFrameData build(const AssetManifest& manifest,
        const PresentationSettings& settings, GroundGeometryCache& groundCache,
        FrameArena& arena) const;

private:
    Level level_;
    GameplaySession session_;
    GameplayPresentation presentation_;
    const AnimationCatalog* animations_ = nullptr;
};

// Uses the production effect definitions and visual builders. Emission and
// simulation are measured separately from a retained GPU snapshot so a
// short-lived effect cannot expire during the evidence warm-up window.
class PerformanceEffectFixture {
public:
    explicit PerformanceEffectFixture(const AssetManifest& manifest);
    void append(RenderFrameData& frame, const AssetManifest& manifest,
        std::string_view scenario, uint32_t emitters);

private:
    ParticleSystem particles_ { 0x5A17U };
    ParticleEffectDefinition mirror_;
    ParticleEffectDefinition witch_;
    TurretParticleEffects turret_;
};

} // namespace sokoban
