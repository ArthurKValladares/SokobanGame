#pragma once

#include "engine/GameplaySession.hpp"
#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>
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
        static constexpr std::size_t animationUseCount =
            static_cast<std::size_t>(AnimationUse::Count);

        EntityVisual motion;
        AnimationUse animationUse = AnimationUse::PlayerIdle;
        std::optional<AnimationUse> animationFallbackUse;
        float clipTimeSeconds = 0.0f;
        // Each semantic animation owns its own clock. A single shared clock
        // made a short movement action overwrite locomotion phase with the
        // idle clip's time, so every following step restarted on the same
        // foot. Keeping the clocks here also makes newly added looping uses
        // phase-continuous without adding another special case.
        std::array<float, animationUseCount> clipTimeSecondsByUse {};
        float clipPlaybackRate = 1.0f;
        bool animationLoops = true;
        bool animationCrossfades = true;
        // Unit quaternion. Actor presentation owns smooth orientation;
        // render data receives only the resulting yaw angle.
        Quat orientation {};

        [[nodiscard]] float clipTimeFor(AnimationUse use) const
        {
            return clipTimeSecondsByUse[static_cast<std::size_t>(use)];
        }

        void setClipTimeFor(AnimationUse use, float timeSeconds)
        {
            clipTimeSecondsByUse[static_cast<std::size_t>(use)] = timeSeconds;
            if (animationUse == use) {
                clipTimeSeconds = timeSeconds;
            }
        }
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
    void triggerTurretShot(
        EntityId turretId,
        MoveDirection direction,
        float delaySeconds = 0.0f);
    [[nodiscard]] Vec2 turretRecoilOffset(EntityId turretId) const;
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
    // Monotonic presentation time used for transitions between clips. Unlike
    // worldAnimationTimeSeconds it does not run backward during undo, and
    // unlike clip-local clocks it does not jump when the selected clip changes.
    [[nodiscard]] float animationTransitionTimeSeconds() const
    {
        return animationTransitionTimeSeconds_;
    }
    [[nodiscard]] float cameraPitchDegrees() const { return cameraPitchDegrees_; }
    [[nodiscard]] const std::vector<PlayerVisual>& players() const { return players_; }
    [[nodiscard]] const std::vector<EntityVisual>& movables() const { return movables_; }
    [[nodiscard]] const std::vector<EnemyVisual>& enemies() const { return enemies_; }

private:
    struct TurretRecoil {
        EntityId turretId = invalidEntityId;
        MoveDirection direction = MoveDirection::Up;
        float ageSeconds = 0.0f;
    };

    static void setImmediatePosition(EntityVisual& visual, Vec3 target);
    [[nodiscard]] EntityVisual* findMotionVisual(EntityTarget target);
    [[nodiscard]] AnimatedActorVisual* findAnimatedVisual(EntityTarget target);

    std::vector<PlayerVisual> players_;
    std::vector<EntityVisual> movables_;
    std::vector<EnemyVisual> enemies_;
    std::vector<TurretRecoil> turretRecoils_;
    const AnimationCatalog* animationCatalog_ = nullptr;
    // Where a reversed action's timeline is sampled from. The only piece of
    // per-action state the presentation keeps; three sibling members were
    // written on every action and never read once, so they are gone.
    //
    // Undo is the only reversed action, and only runs when nothing else is in
    // flight, so one value suffices even with concurrent actions.
    float reverseSourceStartSeconds_ = 0.0f;
    float worldAnimationTimeSeconds_ = 0.0f;
    float animationTransitionTimeSeconds_ = 0.0f;
    float cameraPitchDegrees_ = 0.0f;
    float cameraPitchStartDegrees_ = 0.0f;
    float cameraPitchTargetDegrees_ = 0.0f;
    float cameraPitchTransitionElapsed_ = 0.0f;
};

} // namespace sokoban
