#include "engine/GameplayPresentation.hpp"

#include "engine/render/CameraConfig.hpp"
#include "engine/render/WaterConfig.hpp"

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

Vec3 movableRenderTarget(GridPosition3 position, bool fallen)
{
    Vec3 target = toVec3(position);
    if (fallen) {
        target.z -= config::waterDepthBelowGround;
    }
    return target;
}

Vec3 playerRenderTarget(GridPosition3 position, bool dead)
{
    Vec3 target = toVec3(position);
    if (dead) {
        target.z -= config::drownedPlayerDepthBelowGround;
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

GameplayPresentation::GameplayPresentation()
    : cameraPitchDegrees_(config::cameraPitchDegrees)
    , cameraPitchStartDegrees_(config::cameraPitchDegrees)
    , cameraPitchTargetDegrees_(config::cameraPitchDegrees)
{
}

void GameplayPresentation::setPlayerClips(RenderAnimation moveClip, RenderAnimation pushClip)
{
    playerMoveClip_ = moveClip;
    playerPushClip_ = pushClip;
    for (PlayerVisual& player : players_) {
        if (player.movingClip.isNone()) {
            player.movingClip = moveClip;
        }
    }
}

void GameplayPresentation::resetEntities(const GameState& state)
{
    players_.assign(state.players.size(), {});
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        setImmediatePosition(
            players_[i].motion,
            playerRenderTarget(state.players[i].cell, state.players[i].dead));
        players_[i].facingQuarterTurns =
            facingQuarterTurns(MoveDirection::Down);
        players_[i].movingClip = playerMoveClip_;
    }

    movables_.clear();
    movables_.resize(state.movables.size());
    for (std::size_t i = 0; i < state.movables.size(); ++i) {
        setImmediatePosition(
            movables_[i],
            movableRenderTarget(state.movables[i].cell, state.movables[i].fallen));
    }
}

void GameplayPresentation::advanceClocks(float dt, bool reversed)
{
    worldAnimationTimeSeconds_ += reversed ? -dt : dt;
    for (PlayerVisual& player : players_) {
        player.clipTimeSeconds += dt * player.clipPlaybackRate;
    }
}

void GameplayPresentation::updateCameraPitch(
    float targetDegrees,
    float dt,
    float transitionSeconds)
{
    targetDegrees = std::clamp(targetDegrees, 0.0f, 89.0f);
    dt = std::max(dt, 0.0f);
    transitionSeconds = std::max(transitionSeconds, 0.0f);

    if (std::abs(targetDegrees - cameraPitchTargetDegrees_) > 0.0001f) {
        cameraPitchStartDegrees_ = cameraPitchDegrees_;
        cameraPitchTargetDegrees_ = targetDegrees;
        cameraPitchTransitionElapsed_ = 0.0f;
    }
    if (transitionSeconds <= 0.0f) {
        cameraPitchDegrees_ = cameraPitchTargetDegrees_;
        cameraPitchTransitionElapsed_ = 0.0f;
        return;
    }

    cameraPitchTransitionElapsed_ = std::min(
        cameraPitchTransitionElapsed_ + dt,
        transitionSeconds);
    const float progress = cameraPitchTransitionElapsed_ / transitionSeconds;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    cameraPitchDegrees_ = cameraPitchStartDegrees_ +
        (cameraPitchTargetDegrees_ - cameraPitchStartDegrees_) * eased;
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

    for (PlayerVisual& player : players_) {
        advance(player.motion);
    }
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

    while (players_.size() < action.after.players.size()) {
        const std::size_t playerIndex = players_.size();
        PlayerVisual visual;
        visual.movingClip = playerMoveClip_;
        visual.facingQuarterTurns = players_.empty()
            ? facingQuarterTurns(MoveDirection::Down)
            : players_.front().facingQuarterTurns;
        setImmediatePosition(
            visual.motion,
            playerRenderTarget(
                action.after.players[playerIndex].cell,
                action.after.players[playerIndex].dead));
        players_.push_back(std::move(visual));
    }

    auto beginPlayer = [&](PlayerVisual& visual,
                           const GameState::Player& before,
                           const GameState::Player& after) {
        if (action.facingDirection) {
            visual.facingQuarterTurns =
                facingQuarterTurns(*action.facingDirection);
        }
        if (!before.dead && after.dead) {
            visual.deathTransitionPlaying = true;
            visual.clipTimeSeconds = 0.0f;
            visual.clipPlaybackRate = 1.0f;
        } else if (before.dead && !after.dead) {
            visual.deathTransitionPlaying = false;
            visual.clipTimeSeconds = 0.0f;
        }
        beginMotion(
            visual.motion,
            playerRenderTarget(after.cell, after.dead));
        if (visual.motion.moving) {
            visual.movingClip =
                action.playerPushing ? playerPushClip_ : playerMoveClip_;
            visual.clipPlaybackRate = action.reversed ? -1.0f : 1.0f;
        } else {
            visual.clipPlaybackRate = 1.0f;
        }
    };

    const std::size_t sharedPlayerCount = std::min(
        action.before.players.size(), action.after.players.size());
    for (std::size_t i = 0; i < sharedPlayerCount; ++i) {
        beginPlayer(
            players_[i],
            action.before.players[i],
            action.after.players[i]);
    }

    const std::size_t movableCount =
        std::min(action.before.movables.size(), action.after.movables.size());
    for (std::size_t i = 0; i < movableCount && i < movables_.size(); ++i) {
        beginMotion(
            movables_[i],
            movableRenderTarget(action.after.movables[i].cell, action.after.movables[i].fallen));
    }
}

void GameplayPresentation::finishAction(const GameState& state)
{
    syncToGameState(state);
    for (PlayerVisual& player : players_) {
        player.clipPlaybackRate = 1.0f;
    }
}

void GameplayPresentation::syncToGameState(const GameState& state)
{
    if (players_.size() > state.players.size()) {
        players_.resize(state.players.size());
    }
    while (players_.size() < state.players.size()) {
        const std::size_t i = players_.size();
        PlayerVisual visual;
        visual.movingClip = playerMoveClip_;
        visual.facingQuarterTurns = players_.empty()
            ? facingQuarterTurns(MoveDirection::Down)
            : players_.front().facingQuarterTurns;
        setImmediatePosition(
            visual.motion,
            playerRenderTarget(state.players[i].cell, state.players[i].dead));
        players_.push_back(std::move(visual));
    }
    for (std::size_t i = 0; i < players_.size(); ++i) {
        if (!players_[i].motion.moving) {
            setImmediatePosition(
                players_[i].motion,
                playerRenderTarget(state.players[i].cell, state.players[i].dead));
        }
    }
    for (std::size_t i = 0; i < movables_.size() && i < state.movables.size(); ++i) {
        if (!movables_[i].moving) {
            setImmediatePosition(
                movables_[i],
                movableRenderTarget(state.movables[i].cell, state.movables[i].fallen));
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
