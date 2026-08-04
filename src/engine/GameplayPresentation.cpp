#include "engine/GameplayPresentation.hpp"

#include "engine/PresentationTransactionBuilder.hpp"
#include "engine/render/AnimationConfig.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/WaterConfig.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

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

Vec3 playerRenderTarget(GridPosition3 position, bool drowned)
{
    Vec3 target = toVec3(position);
    if (drowned) {
        target.z -= config::drownedPlayerDepthBelowGround;
    }
    return target;
}

EntityTarget playerTarget(const GameState::Player& player, std::size_t index)
{
    return {
        EntityKind::Player,
        resolvedEntityId(EntityKind::Player, player.id, index),
    };
}

EntityTarget movableTarget(const GameState::Movable& movable, std::size_t index)
{
    return {
        EntityKind::Movable,
        resolvedEntityId(EntityKind::Movable, movable.id, index),
    };
}

EntityTarget enemyTarget(const GameState::Enemy& enemy, std::size_t index)
{
    return {
        EntityKind::Enemy,
        resolvedEntityId(EntityKind::Enemy, enemy.id, index),
    };
}

AnimationUse playerRestAnimation(const GameState::Player& player)
{
    return player.dead ? AnimationUse::PlayerDeadIdle : AnimationUse::PlayerIdle;
}

Vec4 normalizedQuaternion(Vec4 value)
{
    const float length = std::sqrt(
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w);
    if (length <= 0.000001f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, value.y / length, value.z / length, value.w / length };
}

Vec4 yawQuaternion(float radians)
{
    return { 0.0f, 0.0f, std::sin(radians * 0.5f), std::cos(radians * 0.5f) };
}

Vec4 slerp(Vec4 from, Vec4 to, float amount)
{
    from = normalizedQuaternion(from);
    to = normalizedQuaternion(to);
    float dot = from.x * to.x + from.y * to.y +
        from.z * to.z + from.w * to.w;
    if (dot < 0.0f) {
        to = { -to.x, -to.y, -to.z, -to.w };
        dot = -dot;
    }
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (dot > 0.9995f) {
        return normalizedQuaternion({
            from.x + (to.x - from.x) * amount,
            from.y + (to.y - from.y) * amount,
            from.z + (to.z - from.z) * amount,
            from.w + (to.w - from.w) * amount,
        });
    }
    const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float denominator = std::sin(angle);
    const float fromWeight = std::sin((1.0f - amount) * angle) / denominator;
    const float toWeight = std::sin(amount * angle) / denominator;
    return {
        from.x * fromWeight + to.x * toWeight,
        from.y * fromWeight + to.y * toWeight,
        from.z * fromWeight + to.z * toWeight,
        from.w * fromWeight + to.w * toWeight,
    };
}

float gridDistance(Vec3 from, Vec3 to)
{
    return std::abs(to.x - from.x) +
        std::abs(to.y - from.y) +
        std::abs(to.z - from.z);
}

Vec3 interpolateGridMotion(
    Vec3 from,
    Vec3 to,
    float elapsedSeconds,
    float secondsPerTile)
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

template <typename Visual>
void setRestAnimation(Visual& visual, AnimationUse use)
{
    if (visual.animationUse != use) {
        visual.clipTimeSeconds = 0.0f;
    }
    visual.animationUse = use;
    visual.animationFallbackUse.reset();
    visual.clipPlaybackRate = 1.0f;
    visual.animationLoops = true;
    visual.animationCrossfades = true;
}

} // namespace

GameplayPresentation::GameplayPresentation()
    : cameraPitchDegrees_(config::cameraPitchDegrees)
    , cameraPitchStartDegrees_(config::cameraPitchDegrees)
    , cameraPitchTargetDegrees_(config::cameraPitchDegrees)
{
}

void GameplayPresentation::resetEntities(const GameState& state)
{
    reverseSourceStartSeconds_ = 0.0f;
    players_.clear();
    movables_.clear();
    enemies_.clear();
    syncToGameState(state);
}

void GameplayPresentation::advanceClocks(float dt, bool reversed)
{
    worldAnimationTimeSeconds_ += reversed ? -dt : dt;
    for (PlayerVisual& player : players_) {
        player.clipTimeSeconds += dt * player.clipPlaybackRate;
    }
    for (EnemyVisual& enemy : enemies_) {
        enemy.clipTimeSeconds += dt * enemy.clipPlaybackRate;
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

void GameplayPresentation::advanceAnimations(float dt, const GameState& state)
{
    for (std::size_t enemyIndex = 0;
         enemyIndex < enemies_.size() && enemyIndex < state.enemies.size();
         ++enemyIndex) {
        if (state.enemies[enemyIndex].fallen) {
            continue;
        }
        const PlayerVisual* closest = nullptr;
        float closestDistance = 0.0f;
        for (std::size_t playerIndex = 0;
             playerIndex < players_.size() && playerIndex < state.players.size();
             ++playerIndex) {
            if (state.players[playerIndex].dead) {
                continue;
            }
            const float dx = players_[playerIndex].motion.renderPosition.x -
                enemies_[enemyIndex].motion.renderPosition.x;
            const float dy = players_[playerIndex].motion.renderPosition.y -
                enemies_[enemyIndex].motion.renderPosition.y;
            const float distance = dx * dx + dy * dy;
            if (closest == nullptr || distance < closestDistance) {
                closest = &players_[playerIndex];
                closestDistance = distance;
            }
        }
        if (closest == nullptr) {
            continue;
        }
        const float dx = closest->motion.renderPosition.x -
            enemies_[enemyIndex].motion.renderPosition.x;
        const float dy = closest->motion.renderPosition.y -
            enemies_[enemyIndex].motion.renderPosition.y;
        if (std::abs(dx) + std::abs(dy) <= 0.0001f) {
            continue;
        }
        const Vec4 target = yawQuaternion(std::atan2(-dx, dy));
        const float blend = config::enemyFacingSlerpSeconds <= 0.0f
            ? 1.0f
            : 1.0f - std::exp(
                  -4.0f * std::max(dt, 0.0f) /
                  config::enemyFacingSlerpSeconds);
        enemies_[enemyIndex].orientation = slerp(
            enemies_[enemyIndex].orientation,
            target,
            blend);
    }
}

namespace {

// Motion for one leg of a chained action: every entity that changed cell
// between two consecutive world steps, timed to that leg's slot.
void appendLegMotions(
    ActionPresentationTimeline& timeline,
    const GameState& before,
    const GameState& after,
    float startSeconds,
    float durationSeconds)
{
    const auto add = [&](EntityTarget target, Vec3 from, Vec3 to) {
        if (gridDistance(from, to) > 0.0001f) {
            timeline.motions.push_back({
                .target = target,
                .from = from,
                .to = to,
                .startSeconds = startSeconds,
                .durationSeconds = durationSeconds,
            });
        }
    };

    const std::size_t players =
        std::min(before.players.size(), after.players.size());
    for (std::size_t i = 0; i < players; ++i) {
        add(playerTarget(before.players[i], i),
            playerRenderTarget(before.players[i].cell, before.players[i].drowned),
            playerRenderTarget(after.players[i].cell, after.players[i].drowned));
    }

    const std::size_t movables =
        std::min(before.movables.size(), after.movables.size());
    for (std::size_t i = 0; i < movables; ++i) {
        add(movableTarget(before.movables[i], i),
            movableRenderTarget(before.movables[i].cell, before.movables[i].fallen),
            movableRenderTarget(after.movables[i].cell, after.movables[i].fallen));
    }

    const std::size_t enemies =
        std::min(before.enemies.size(), after.enemies.size());
    for (std::size_t i = 0; i < enemies; ++i) {
        add(enemyTarget(before.enemies[i], i),
            movableRenderTarget(before.enemies[i].cell, before.enemies[i].fallen),
            movableRenderTarget(after.enemies[i].cell, after.enemies[i].fallen));
    }
}

} // namespace

ActionPresentationTimeline GameplayPresentation::buildActionPresentation(
    const GameplaySession::Action& action,
    const std::vector<GameState>& legs) const
{
    if (legs.size() <= 1) {
        return buildActionPresentation(action);
    }

    // A chained slide is one action spanning several world steps. Interpolating
    // once from start to finish would be right for a single block travelling in
    // a straight line, but wrong the moment a chain is involved: a block that
    // only starts moving on the fourth step would set off immediately.
    const float stepDuration =
        action.durationSeconds / static_cast<float>(legs.size());

    // Animations - pushes, deaths, attacks - come from the ordinary builder run
    // over the first leg alone, which is the only leg the player drove. Nothing
    // about how those are chosen changes.
    //
    // Known gap: an animated event that happens in a *later* leg, such as a
    // player killed part-way through a slide, is not represented yet. The
    // motion is correct; only the clip is missing. Building a timeline per leg
    // and merging them would fix it.
    GameplaySession::Action firstLeg = action;
    firstLeg.after = legs.front();
    firstLeg.durationSeconds = stepDuration;
    ActionPresentationTimeline timeline = buildActionPresentation(firstLeg);
    timeline.durationSeconds = action.durationSeconds;

    for (std::size_t leg = 1; leg < legs.size(); ++leg) {
        appendLegMotions(
            timeline,
            legs[leg - 1],
            legs[leg],
            static_cast<float>(leg) * stepDuration,
            stepDuration);
    }
    return timeline;
}

ActionPresentationTimeline GameplayPresentation::buildActionPresentation(
    const GameplaySession::Action& action) const
{
    PresentationTransactionBuilder builder(animationCatalog_);
    const float motionDuration = std::max(action.durationSeconds, 0.0f);

    const std::size_t playerCount = std::min(
        action.before.players.size(),
        action.after.players.size());
    for (std::size_t index = 0; index < playerCount; ++index) {
        const GameState::Player& before = action.before.players[index];
        const GameState::Player& after = action.after.players[index];
        const EntityTarget target = playerTarget(before, index);
        float initialClipTime = 0.0f;
        const auto visual = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        if (visual != players_.end()) {
            initialClipTime = visual->clipTimeSeconds;
        }
        builder.setInitialAnimation(
            target,
            playerRestAnimation(before),
            initialClipTime);
        const Vec3 from = playerRenderTarget(before.cell, before.drowned);
        const Vec3 to = playerRenderTarget(after.cell, after.drowned);
        if (gridDistance(from, to) > 0.0001f) {
            builder.addMotion({
                .target = target,
                .from = from,
                .to = to,
                .durationSeconds = motionDuration,
            });
            static_cast<void>(builder.addAnimation({
                .target = target,
                .use = action.playerPushing
                    ? AnimationUse::PlayerPush
                    : AnimationUse::PlayerMove,
                .completionUse = AnimationUse::PlayerIdle,
                .clipStartSeconds = initialClipTime,
                .durationSeconds = motionDuration,
                .loops = true,
            }));
        }
    }

    const std::size_t movableCount = std::min(
        action.before.movables.size(),
        action.after.movables.size());
    for (std::size_t index = 0; index < movableCount; ++index) {
        const GameState::Movable& before = action.before.movables[index];
        const GameState::Movable& after = action.after.movables[index];
        const Vec3 from = movableRenderTarget(before.cell, before.fallen);
        const Vec3 to = movableRenderTarget(after.cell, after.fallen);
        if (gridDistance(from, to) > 0.0001f) {
            builder.addMotion({
                .target = movableTarget(before, index),
                .from = from,
                .to = to,
                .durationSeconds = motionDuration,
            });
        }
    }

    using IntentId = PresentationTransactionBuilder::AnimationIntentId;
    const std::size_t enemyCount = std::min(
        action.before.enemies.size(),
        action.after.enemies.size());
    std::vector<std::vector<std::size_t>> attackedPlayers(enemyCount);
    std::vector<std::optional<IntentId>> attackIntents(enemyCount);
    for (std::size_t enemyIndex = 0; enemyIndex < enemyCount; ++enemyIndex) {
        const GameState::Enemy& before = action.before.enemies[enemyIndex];
        const GameState::Enemy& after = action.after.enemies[enemyIndex];
        const EntityTarget target = enemyTarget(before, enemyIndex);
        float initialClipTime = 0.0f;
        const auto visual = std::ranges::find_if(
            enemies_,
            [&](const EnemyVisual& candidate) {
                return candidate.motion.target == target;
            });
        if (visual != enemies_.end()) {
            initialClipTime = visual->clipTimeSeconds;
        }
        builder.setInitialAnimation(
            target,
            AnimationUse::EnemyIdle,
            initialClipTime);
        const Vec3 from = movableRenderTarget(before.cell, before.fallen);
        const Vec3 to = movableRenderTarget(after.cell, after.fallen);
        if (gridDistance(from, to) > 0.0001f) {
            builder.addMotion({
                .target = target,
                .from = from,
                .to = to,
                .durationSeconds = motionDuration,
            });
        }

        for (std::size_t playerIndex = 0;
             playerIndex < playerCount;
             ++playerIndex) {
            const GameState::Player& playerBefore = action.before.players[playerIndex];
            const GameState::Player& playerAfter = action.after.players[playerIndex];
            if (playerBefore.dead || !playerAfter.dead || playerAfter.drowned ||
                after.fallen || playerAfter.cell.z != after.cell.z) {
                continue;
            }
            const int distance =
                std::abs(playerAfter.cell.x - after.cell.x) +
                std::abs(playerAfter.cell.y - after.cell.y);
            if (distance == 1) {
                attackedPlayers[enemyIndex].push_back(playerIndex);
            }
        }
        if (!attackedPlayers[enemyIndex].empty()) {
            attackIntents[enemyIndex] = builder.addAnimation({
                .target = target,
                .use = AnimationUse::EnemyAttack,
                .completionUse = AnimationUse::EnemyIdle,
                .fallbackUse = AnimationUse::EnemyIdle,
            });
        }
    }

    for (std::size_t playerIndex = 0;
         playerIndex < playerCount;
         ++playerIndex) {
        const GameState::Player& before = action.before.players[playerIndex];
        const GameState::Player& after = action.after.players[playerIndex];
        if (before.dead || !after.dead) {
            continue;
        }
        const IntentId deathIntent = builder.addAnimation({
            .target = playerTarget(before, playerIndex),
            .use = AnimationUse::PlayerDeath,
            .completionUse = AnimationUse::PlayerDeadIdle,
            .fallbackUse = AnimationUse::PlayerDeadIdle,
        });
        if (after.drowned) {
            continue;
        }
        for (std::size_t enemyIndex = 0;
             enemyIndex < attackedPlayers.size();
             ++enemyIndex) {
            if (!attackIntents[enemyIndex] ||
                std::ranges::find(attackedPlayers[enemyIndex], playerIndex) ==
                    attackedPlayers[enemyIndex].end()) {
                continue;
            }
            if (builder.startAfterCatalogEvent(
                    deathIntent,
                    *attackIntents[enemyIndex])) {
                break;
            }
        }
    }

    return builder.build();
}

float GameplayPresentation::reverseDuration(
    const GameplaySession::Action& action) const
{
    return action.presentation.empty()
        ? std::max(action.durationSeconds, 0.0f)
        : action.presentation.durationSeconds;
}

void GameplayPresentation::beginAction(
    const GameplaySession::Action& action, const GameState& worldState)
{
    // Structural sync - creating visuals for players a mirror just made, and
    // dropping ones undo removed - comes from the world, not from this action's
    // snapshot of it. The two are the same value today, because state does not
    // advance until an action completes; they stop being the same the moment a
    // second action is in flight, and then rebuilding from one action's `before`
    // would snap the other action's entities back to where they started.
    syncToGameState(worldState);
    reverseSourceStartSeconds_ = action.reversed
        ? action.presentation.durationSeconds
        : 0.0f;
    if (action.facingDirection) {
        for (PlayerVisual& player : players_) {
            player.facingQuarterTurns = facingQuarterTurns(*action.facingDirection);
        }
    }
}

void GameplayPresentation::seekAction(
    const GameplaySession::Action& action,
    float elapsedSeconds)
{
    if (action.presentation.empty()) {
        return;
    }
    const ActionPresentationTimeline& timeline = action.presentation;
    const float elapsed = std::max(elapsedSeconds, 0.0f);
    const float sourceTime = std::clamp(
        action.reversed
            ? reverseSourceStartSeconds_ - elapsed
            : elapsed,
        0.0f,
        timeline.durationSeconds);
    // Only the entities this action drives. Clearing every visual would be
    // fine while one action exists, but with two in flight whichever seeks last
    // would stop the other's entities dead every frame.
    //
    // Entities this action does not touch are not its business: either nothing
    // is moving them, or another action is, and that action clears and sets
    // them itself.
    for (const ActionMotionTrack& track : timeline.motions) {
        if (EntityVisual* visual = findMotionVisual(track.target)) {
            visual->moving = false;
        }
    }

    for (const ActionMotionTrack& track : timeline.motions) {
        EntityVisual* visual = findMotionVisual(track.target);
        if (visual == nullptr) {
            continue;
        }
        const float end = track.startSeconds + track.durationSeconds;
        if (sourceTime < track.startSeconds ||
            (action.reversed && sourceTime <= track.startSeconds) ||
            track.durationSeconds <= 0.0f) {
            setImmediatePosition(*visual, track.from);
            continue;
        }
        if (sourceTime >= end) {
            setImmediatePosition(*visual, track.to);
            continue;
        }
        const float motionElapsed = sourceTime - track.startSeconds;
        const float distance = gridDistance(track.from, track.to);
        visual->animationStart = track.from;
        visual->animationEnd = track.to;
        visual->animationElapsed = motionElapsed;
        visual->animationDuration = track.durationSeconds;
        visual->animationSecondsPerTile = distance > 0.0001f
            ? track.durationSeconds / distance
            : 0.0f;
        visual->renderPosition = interpolateGridMotion(
            track.from,
            track.to,
            motionElapsed,
            visual->animationSecondsPerTile);
        visual->moving = distance > 0.0001f;
    }

    for (const ActionAnimationTrack& track : timeline.animations) {
        AnimatedActorVisual* visual = findAnimatedVisual(track.target);
        if (visual == nullptr) {
            continue;
        }
        visual->animationUse = track.initialUse;
        visual->animationFallbackUse.reset();
        visual->clipTimeSeconds = track.initialClipTimeSeconds + sourceTime;
        visual->animationLoops = true;
        visual->animationCrossfades = !action.reversed;
        visual->clipPlaybackRate = action.reversed ? -1.0f : 1.0f;

        for (const ActionAnimationSegment& segment : track.segments) {
            if (sourceTime < segment.startSeconds) {
                break;
            }
            const float end = segment.startSeconds + segment.durationSeconds;
            if (sourceTime < end) {
                visual->animationUse = segment.use;
                visual->animationFallbackUse = segment.fallbackUse;
                visual->clipTimeSeconds = segment.clipStartSeconds +
                    sourceTime - segment.startSeconds;
                visual->animationLoops = segment.loops;
                continue;
            }
            visual->animationUse = segment.completionUse;
            visual->animationFallbackUse.reset();
            visual->clipTimeSeconds = sourceTime - end;
            visual->animationLoops = true;
        }
    }
}

void GameplayPresentation::finishAction(const GameState& state)
{
    syncToGameState(state);
    reverseSourceStartSeconds_ = 0.0f;
}

void GameplayPresentation::syncToGameState(const GameState& state)
{
    std::vector<PlayerVisual> oldPlayers = std::move(players_);
    players_.clear();
    players_.reserve(state.players.size());
    for (std::size_t index = 0; index < state.players.size(); ++index) {
        const GameState::Player& player = state.players[index];
        const EntityTarget target = playerTarget(player, index);
        auto existing = std::ranges::find_if(
            oldPlayers,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        PlayerVisual visual;
        if (existing != oldPlayers.end()) {
            visual = std::move(*existing);
        } else {
            visual.facingQuarterTurns = players_.empty()
                ? facingQuarterTurns(MoveDirection::Down)
                : players_.front().facingQuarterTurns;
        }
        visual.motion.target = target;
        setImmediatePosition(
            visual.motion,
            playerRenderTarget(player.cell, player.drowned));
        setRestAnimation(visual, playerRestAnimation(player));
        players_.push_back(std::move(visual));
    }

    std::vector<EntityVisual> oldMovables = std::move(movables_);
    movables_.clear();
    movables_.reserve(state.movables.size());
    for (std::size_t index = 0; index < state.movables.size(); ++index) {
        const GameState::Movable& movable = state.movables[index];
        const EntityTarget target = movableTarget(movable, index);
        auto existing = std::ranges::find(
            oldMovables,
            target,
            &EntityVisual::target);
        EntityVisual visual;
        if (existing != oldMovables.end()) {
            visual = std::move(*existing);
        }
        visual.target = target;
        setImmediatePosition(
            visual,
            movableRenderTarget(movable.cell, movable.fallen));
        movables_.push_back(std::move(visual));
    }

    std::vector<EnemyVisual> oldEnemies = std::move(enemies_);
    enemies_.clear();
    enemies_.reserve(state.enemies.size());
    for (std::size_t index = 0; index < state.enemies.size(); ++index) {
        const GameState::Enemy& enemy = state.enemies[index];
        const EntityTarget target = enemyTarget(enemy, index);
        auto existing = std::ranges::find_if(
            oldEnemies,
            [&](const EnemyVisual& candidate) {
                return candidate.motion.target == target;
            });
        EnemyVisual visual;
        if (existing != oldEnemies.end()) {
            visual = std::move(*existing);
        }
        visual.motion.target = target;
        setImmediatePosition(
            visual.motion,
            movableRenderTarget(enemy.cell, enemy.fallen));
        setRestAnimation(visual, AnimationUse::EnemyIdle);
        enemies_.push_back(std::move(visual));
    }
}

float GameplayPresentation::conveyorBeltScrollOffset(
    float stepDurationSeconds) const
{
    if (stepDurationSeconds <= 0.0f) {
        return 0.0f;
    }
    return std::fmod(
        worldAnimationTimeSeconds_ / stepDurationSeconds,
        1.0f);
}

void GameplayPresentation::setImmediatePosition(
    EntityVisual& visual,
    Vec3 target)
{
    visual.renderPosition = target;
    visual.animationStart = target;
    visual.animationEnd = target;
    visual.animationElapsed = 0.0f;
    visual.animationDuration = 0.0f;
    visual.animationSecondsPerTile = 0.0f;
    visual.moving = false;
}

GameplayPresentation::EntityVisual* GameplayPresentation::findMotionVisual(
    EntityTarget target)
{
    if (target.kind == EntityKind::Player) {
        const auto found = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        return found == players_.end() ? nullptr : &found->motion;
    }
    if (target.kind == EntityKind::Movable) {
        const auto found = std::ranges::find(
            movables_, target, &EntityVisual::target);
        return found == movables_.end() ? nullptr : &*found;
    }
    const auto found = std::ranges::find_if(
        enemies_,
        [&](const EnemyVisual& candidate) {
            return candidate.motion.target == target;
        });
    return found == enemies_.end() ? nullptr : &found->motion;
}

GameplayPresentation::AnimatedActorVisual*
GameplayPresentation::findAnimatedVisual(EntityTarget target)
{
    if (target.kind == EntityKind::Player) {
        const auto found = std::ranges::find_if(
            players_,
            [&](const PlayerVisual& candidate) {
                return candidate.motion.target == target;
            });
        return found == players_.end() ? nullptr : &*found;
    }
    if (target.kind == EntityKind::Enemy) {
        const auto found = std::ranges::find_if(
            enemies_,
            [&](const EnemyVisual& candidate) {
                return candidate.motion.target == target;
            });
        return found == enemies_.end() ? nullptr : &*found;
    }
    return nullptr;
}

} // namespace sokoban
