#pragma once

#include "engine/GameplaySession.hpp"
#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <vector>

namespace sokoban {

// Presentation-only state derived from GameplaySession snapshots. It owns
// interpolation, clip clocks, facing, and conveyor/world animation time, but
// never mutates authoritative gameplay state.
class GameplayPresentation {
public:
    struct EntityVisual {
        Vec3 renderPosition {};
        Vec3 animationStart {};
        Vec3 animationEnd {};
        float animationElapsed = 0.0f;
        float animationDuration = 0.0f;
        float animationSecondsPerTile = 0.0f;
        bool moving = false;
    };

    struct PlayerVisual {
        EntityVisual motion;
        RenderAnimation movingClip {};
        float clipTimeSeconds = 0.0f;
        float clipPlaybackRate = 1.0f;
        uint32_t facingQuarterTurns = 0;
    };

    // Ids come from the asset manifest; must be set before actions begin.
    void setPlayerClips(RenderAnimation moveClip, RenderAnimation pushClip);
    void resetEntities(const GameState& state);
    void advanceClocks(float dt, bool reversed);
    void advanceAnimations(float dt);
    void beginAction(const GameplaySession::Action& action);
    void finishAction(const GameState& state);
    void syncToGameState(const GameState& state);

    [[nodiscard]] float conveyorBeltScrollOffset(float stepDurationSeconds) const;
    [[nodiscard]] float worldAnimationTimeSeconds() const { return worldAnimationTimeSeconds_; }
    [[nodiscard]] const PlayerVisual& player() const { return player_; }
    [[nodiscard]] const std::vector<EntityVisual>& movables() const { return movables_; }

private:
    static void setImmediatePosition(EntityVisual& visual, Vec3 target);

    PlayerVisual player_;
    std::vector<EntityVisual> movables_;
    RenderAnimation playerMoveClip_ {};
    RenderAnimation playerPushClip_ {};
    float worldAnimationTimeSeconds_ = 0.0f;
};

} // namespace sokoban
