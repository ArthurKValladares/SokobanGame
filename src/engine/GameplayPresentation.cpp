#include "engine/GameplayPresentation.hpp"

#include "engine/Config.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {
namespace {

Vec3 toVec3(GridPosition3 position)
{
    return {
        static_cast<float>(position.x),
        static_cast<float>(position.y),
        static_cast<float>(position.z),
    };
}

Vec3 entityRenderTarget(GridPosition3 position, bool fallen)
{
    Vec3 target = toVec3(position);
    if (fallen) {
        target.z -= config::waterDepthBelowGround;
    }
    return target;
}

float gridDistance(Vec3 from, Vec3 to)
{
    return std::abs(to.x - from.x) +
        std::abs(to.y - from.y) +
        std::abs(to.z - from.z);
}

Vec3 interpolateGridMotion(Vec3 from, Vec3 to, float elapsedSeconds, float secondsPerTile)
{
    const float distance = gridDistance(from, to);
    if (distance <= 0.0001f || secondsPerTile <= 0.0f) {
        return to;
    }

    float remaining = std::min(elapsedSeconds / secondsPerTile, distance);
    Vec3 result = from;
    auto travelAxis = [&](float target, float& value) {
        const float delta = target - value;
        const float step = std::min(std::abs(delta), remaining);
        if (step > 0.0f) {
            value += std::copysign(step, delta);
            remaining -= step;
        }
    };

    if (to.z > from.z) {
        travelAxis(to.z, result.z);
    }
    travelAxis(to.x, result.x);
    travelAxis(to.y, result.y);
    if (to.z <= from.z) {
        travelAxis(to.z, result.z);
    }
    return result;
}

uint32_t facingQuarterTurns(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Down:
        return 0;
    case MoveDirection::Left:
        return 1;
    case MoveDirection::Up:
        return 2;
    case MoveDirection::Right:
        return 3;
    }
    return 0;
}

} // namespace

void GameplayPresentation::resetEntities(const GameState& state)
{
    player_ = {};
    setImmediatePosition(player_.motion, entityRenderTarget(state.player, state.playerDead));
    player_.facingQuarterTurns = facingQuarterTurns(MoveDirection::Down);

    movables_.clear();
    movables_.resize(state.movables.size());
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        setImmediatePosition(
            movables_[i],
            entityRenderTarget(state.movables[i].cell, state.movables[i].fallen));
    }
}

void GameplayPresentation::advanceClocks(float dt, bool reversed)
{
    worldAnimationTimeSeconds_ += reversed ? -dt : dt;
    player_.clipTimeSeconds += dt * player_.clipPlaybackRate;
}

void GameplayPresentation::advanceAnimations(float dt)
{
    auto advance = [dt](EntityVisual& visual) {
        if (!visual.moving) {
            return;
        }
        visual.animationElapsed =
            std::min(visual.animationElapsed + dt, visual.animationDuration);
        if (visual.animationDuration <= 0.0f ||
            visual.animationElapsed >= visual.animationDuration) {
            setImmediatePosition(visual, visual.animationEnd);
            return;
        }
        visual.renderPosition = interpolateGridMotion(
            visual.animationStart,
            visual.animationEnd,
            visual.animationElapsed,
            visual.animationSecondsPerTile);
    };

    advance(player_.motion);
    for (EntityVisual& visual : movables_) {
        advance(visual);
    }
}

void GameplayPresentation::beginAction(const GameplaySession::Action& action)
{
    auto beginMotion = [&action](EntityVisual& visual, Vec3 target) {
        if (gridDistance(visual.renderPosition, target) <= 0.0001f) {
            setImmediatePosition(visual, target);
            return;
        }

        visual.animationStart = visual.renderPosition;
        visual.animationEnd = target;
        visual.animationElapsed = 0.0f;
        const float distance = gridDistance(visual.animationStart, visual.animationEnd);
        visual.animationDuration = action.durationSeconds;
        visual.animationSecondsPerTile =
            distance > 0.0001f ? action.durationSeconds / distance : 0.0f;
        visual.moving = true;
    };

    if (action.facingDirection) {
        player_.facingQuarterTurns = facingQuarterTurns(*action.facingDirection);
    }
    beginMotion(
        player_.motion,
        entityRenderTarget(action.after.player, action.after.playerDead));
    if (player_.motion.moving) {
        player_.movingClip =
            action.playerPushing ? RenderAnimation::RoguePush : RenderAnimation::RogueMovement;
        player_.clipPlaybackRate = action.reversed ? -1.0f : 1.0f;
    } else {
        player_.clipPlaybackRate = 1.0f;
    }

    const std::size_t movableCount =
        std::min(action.before.movables.size(), action.after.movables.size());
    for (std::size_t i = 0; i < movableCount && i < movables_.size(); ++i) {
        beginMotion(
            movables_[i],
            entityRenderTarget(action.after.movables[i].cell, action.after.movables[i].fallen));
    }
}

void GameplayPresentation::finishAction(const GameState& state)
{
    syncToGameState(state);
    player_.clipPlaybackRate = 1.0f;
}

void GameplayPresentation::syncToGameState(const GameState& state)
{
    if (!player_.motion.moving) {
        setImmediatePosition(
            player_.motion,
            entityRenderTarget(state.player, state.playerDead));
    }
    for (std::size_t i = 0; i < movables_.size() && i < state.movables.size(); ++i) {
        if (!movables_[i].moving) {
            setImmediatePosition(
                movables_[i],
                entityRenderTarget(state.movables[i].cell, state.movables[i].fallen));
        }
    }
}

float GameplayPresentation::conveyorBeltScrollOffset(float stepDurationSeconds) const
{
    if (stepDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::fmod(worldAnimationTimeSeconds_ / stepDurationSeconds, 1.0f);
}

void GameplayPresentation::setImmediatePosition(EntityVisual& visual, Vec3 target)
{
    visual.renderPosition = target;
    visual.animationStart = target;
    visual.animationEnd = target;
    visual.animationElapsed = 0.0f;
    visual.animationDuration = 0.0f;
    visual.animationSecondsPerTile = 0.0f;
    visual.moving = false;
}

} // namespace sokoban
