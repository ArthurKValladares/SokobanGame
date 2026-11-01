#pragma once

#include "engine/Rules.hpp"
#include "engine/render/RenderTypes.hpp"

#include <vector>

namespace sokoban {

class AssetManifest;
class GameplayPresentation;

struct TurretParticleEffects {
    RenderTexture glowTexture = noTexture;
};

[[nodiscard]] TurretParticleEffects makeTurretParticleEffects(
    const AssetManifest& manifest);

// Continuous streams use gameplay collision and portal traversal. Animation
// is sampled without accumulating particles, keeping idle flames bounded.
class TurretFlameSystem {
public:
    void update(const Level& level, const GameState& state, float dt,
        const GameplayPresentation* presentation = nullptr);
    // A lethal volley must remain visible long enough for the flame to arrive
    // even when gameplay has already removed its target or both turrets.
    void triggerShot(const rules::TurretShot& shot, float impactDelaySeconds);
    void appendRenderData(RenderFrameData& frame,
        const TurretParticleEffects& effects) const;
    void reset();

private:
    struct Stream {
        EntityId turret = invalidEntityId;
        GridPosition3 origin {};
        MoveDirection direction = MoveDirection::Up;
        std::vector<rules::TurretRaySegment> segments;
        float ageSeconds = 0.0f;
        float shotHoldSeconds = 0.0f;
        bool visible = false;
    };

    std::vector<Stream> streams_;
};

} // namespace sokoban
