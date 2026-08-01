#pragma once

#include "engine/AnimationEventSequencer.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <vector>
#include <optional>

namespace sokoban {

// Presentation-only state derived from GameplaySession snapshots. It owns
// interpolation, clip clocks, facing, and conveyor/world animation time, but
// never mutates authoritative gameplay state.
class GameplayPresentation {
public:
    GameplayPresentation();

    struct EntityVisual {
        Vec3 renderPosition {};
        Vec3 animationStart {};
        Vec3 animationEnd {};
        float animationElapsed = 0.0f;
        float animationDuration = 0.0f;
        float animationSecondsPerTile = 0.0f;
        bool moving = false;
    };

    struct AnimatedActorVisual {
        EntityVisual motion;
        float clipTimeSeconds = 0.0f;
        float clipPlaybackRate = 1.0f;
        // Unit quaternion. Actor presentation owns smooth orientation;
        // render data receives only the resulting yaw angle.
        Vec4 orientation { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct PlayerVisual : AnimatedActorVisual {
        RenderAnimation movingClip {};
        uint32_t facingQuarterTurns = 0;
        bool deathTransitionPending = false;
        bool deathTransitionPlaying = false;
        bool revivedDuringUndo = false;
        std::optional<uint64_t> deathGateSourceInstance;
    };

    struct EnemyVisual : AnimatedActorVisual {
        bool attackTransitionPlaying = false;
    };

    // Ids come from the asset manifest; must be set before actions begin.
    void setActorClips(
        RenderAnimation moveClip,
        RenderAnimation pushClip,
        RenderAnimation enemyAttackClip);
    void setAnimationCatalog(const AnimationCatalog* catalog)
    {
        animationCatalog_ = catalog;
    }
    void setPlayerClips(RenderAnimation moveClip, RenderAnimation pushClip)
    {
        setActorClips(moveClip, pushClip, enemyAttackClip_);
    }
    void resetEntities(const GameState& state);
    void advanceClocks(float dt, bool reversed);
    void updateCameraPitch(
        float targetDegrees,
        float dt,
        float transitionSeconds);
    void advanceAnimations(float dt, const GameState& state);
    void advanceAnimations(float dt) { advanceAnimations(dt, {}); }
    [[nodiscard]] ActionPresentationTimeline buildActionPresentation(
        const GameplaySession::Action& action) const;
    [[nodiscard]] float reverseDuration(
        const GameplaySession::Action& action) const;
    void beginAction(const GameplaySession::Action& action);
    void seekAction(
        const GameplaySession::Action& action,
        float elapsedSeconds);
    void finishAction(const GameState& state);
    void syncToGameState(const GameState& state);

    [[nodiscard]] float conveyorBeltScrollOffset(float stepDurationSeconds) const;
    [[nodiscard]] float worldAnimationTimeSeconds() const { return worldAnimationTimeSeconds_; }
    [[nodiscard]] float cameraPitchDegrees() const { return cameraPitchDegrees_; }
    [[nodiscard]] const std::vector<PlayerVisual>& players() const
    {
        return players_;
    }
    [[nodiscard]] const std::vector<EntityVisual>& movables() const { return movables_; }
    [[nodiscard]] const std::vector<EnemyVisual>& enemies() const { return enemies_; }

private:
    static void setImmediatePosition(EntityVisual& visual, Vec3 target);

    std::vector<PlayerVisual> players_;
    std::vector<EntityVisual> movables_;
    std::vector<EnemyVisual> enemies_;
    RenderAnimation playerMoveClip_ {};
    RenderAnimation playerPushClip_ {};
    RenderAnimation enemyAttackClip_ {};
    const AnimationCatalog* animationCatalog_ = nullptr;
    AnimationEventSequencer eventSequencer_;
    ActionPresentationTimeline trackedTimeline_;
    float trackedTimelineSeconds_ = 0.0f;
    float reverseSourceStartSeconds_ = 0.0f;
    bool trackedTimelineReversed_ = false;
    float worldAnimationTimeSeconds_ = 0.0f;
    float cameraPitchDegrees_ = 0.0f;
    float cameraPitchStartDegrees_ = 0.0f;
    float cameraPitchTargetDegrees_ = 0.0f;
    float cameraPitchTransitionElapsed_ = 0.0f;
};

} // namespace sokoban
