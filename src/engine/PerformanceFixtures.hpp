#pragma once

#include "engine/MirrorParticleEffect.hpp"
#include "engine/TurretParticleEffect.hpp"

#include <string_view>

namespace sokoban {

class AssetManifest;

[[nodiscard]] constexpr bool isPerformanceEffectScenario(std::string_view name)
{
    return name == "mirror-swap" || name == "witch-swap" ||
        name == "turret-volley" || name == "portals" ||
        name == "special-blocks" || name == "mixed-stress";
}

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
