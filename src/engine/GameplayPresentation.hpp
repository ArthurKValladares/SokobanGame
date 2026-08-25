#pragma once

#include "engine/GameplaySession.hpp"
#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <optional>
#include <vector>

namespace sokoban {

// Presentation-only state sampled from immutable action transactions. It owns
// interpolation, animation clocks, facing, and world animation time, but never
// mutates authoritative gameplay state.
class GameplayPresentation {
public:
    GameplayPresentation();

    struct EntityVisual {
        EntityTarget target;
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
        AnimationUse animationUse = AnimationUse::PlayerIdle;
        std::optional<AnimationUse> animationFallbackUse;
        float clipTimeSeconds = 0.0f;
        float clipPlaybackRate = 1.0f;
        bool animationLoops = true;
        bool animationCrossfades = true;
        // Unit quaternion. Actor presentation owns smooth orientation;
        // render data receives only the resulting yaw angle.
        Quat orientation {};
    };

    struct PlayerVisual : AnimatedActorVisual {
        uint32_t facingQuarterTurns = 0;
    };

    struct EnemyVisual : AnimatedActorVisual {};

    void setAnimationCatalog(const AnimationCatalog* catalog)
    {
        animationCatalog_ = catalog;
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
    // Chain-aware: `legs` are the states the action passes through, one per
    // world step, so a slide animates tile by tile instead of interpolating
    // once from start to finish. One leg (or none) is the ordinary case above.
    [[nodiscard]] ActionPresentationTimeline buildActionPresentation(
        const GameplaySession::Action& action,
        const std::vector<GameState>& legs) const;
    [[nodiscard]] float reverseDuration(
        const GameplaySession::Action& action) const;
    // `worldState` is the session's current state, used to create and remove
    // visuals. Deliberately not taken from `action.before`: see the definition.
    void beginAction(
        const GameplaySession::Action& action, const GameState& worldState);
    void seekAction(
        const GameplaySession::Action& action,
        float elapsedSeconds);
    void finishAction(const GameState& state);
    void syncToGameState(const GameState& state);

    [[nodiscard]] float conveyorBeltScrollOffset(float stepDurationSeconds) const;
    [[nodiscard]] float worldAnimationTimeSeconds() const { return worldAnimationTimeSeconds_; }
    [[nodiscard]] float cameraPitchDegrees() const { return cameraPitchDegrees_; }
    [[nodiscard]] const std::vector<PlayerVisual>& players() const { return players_; }
    [[nodiscard]] const std::vector<EntityVisual>& movables() const { return movables_; }
    [[nodiscard]] const std::vector<EnemyVisual>& enemies() const { return enemies_; }

private:
    static void setImmediatePosition(EntityVisual& visual, Vec3 target);
    [[nodiscard]] EntityVisual* findMotionVisual(EntityTarget target);
    [[nodiscard]] AnimatedActorVisual* findAnimatedVisual(EntityTarget target);

    std::vector<PlayerVisual> players_;
    std::vector<EntityVisual> movables_;
    std::vector<EnemyVisual> enemies_;
    const AnimationCatalog* animationCatalog_ = nullptr;
    // Where a reversed action's timeline is sampled from. The only piece of
    // per-action state the presentation keeps; three sibling members were
    // written on every action and never read once, so they are gone.
    //
    // Undo is the only reversed action, and only runs when nothing else is in
    // flight, so one value suffices even with concurrent actions.
    float reverseSourceStartSeconds_ = 0.0f;
    float worldAnimationTimeSeconds_ = 0.0f;
    float cameraPitchDegrees_ = 0.0f;
    float cameraPitchStartDegrees_ = 0.0f;
    float cameraPitchTargetDegrees_ = 0.0f;
    float cameraPitchTransitionElapsed_ = 0.0f;
};

} // namespace sokoban
