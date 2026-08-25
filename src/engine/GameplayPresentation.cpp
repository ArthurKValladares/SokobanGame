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
        const Quat target =
            quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, std::atan2(-dx, dy));
        const float blend = config::enemyFacingSlerpSeconds <= 0.0f
            ? 1.0f
            : 1.0f - std::exp(
                  -4.0f * std::max(dt, 0.0f) /
                  config::enemyFacingSlerpSeconds);
        // The shared slerp does not clamp - that is the mathematical
        // operation, and the local copy this replaced clamped while the glTF
        // one did not. `blend` is 1 - exp(-k*dt), so it cannot leave [0, 1)
        // for a non-negative dt; the clamp is kept anyway to preserve exactly
        // what this call site used to guarantee for itself.
        enemies_[enemyIndex].orientation = slerp(
            enemies_[enemyIndex].orientation,
            target,
            std::clamp(blend, 0.0f, 1.0f));
    }
}


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

    // Each leg is resolved as its own transaction and the results are laid end
    // to end. Motion alone used to be enough here, on the reasoning that only
    // the first leg is player-driven and only it can produce an animated event.
    // That is not true: a player crushed or drowned part-way through a slide
    // dies on leg four, and running the builder over leg one only gave it
    // correct motion and no clip at all.
    //
    // `playerPushing` is deliberately confined to the first leg. A push is
    // something input does, and the later legs are momentum spending itself
    // out - carrying the flag through would play the push animation for the
    // whole length of the slide.
    ActionPresentationTimeline timeline;
    for (std::size_t leg = 0; leg < legs.size(); ++leg) {
        GameplaySession::Action legAction = action;
        legAction.before = leg == 0 ? action.before : legs[leg - 1];
        legAction.after = legs[leg];
        legAction.durationSeconds = stepDuration;
        legAction.playerPushing = leg == 0 && action.playerPushing;

        timeline = concatenateTimelines(
            std::move(timeline),
            buildActionPresentation(legAction),
            static_cast<float>(leg) * stepDuration);
    }
    // The action's own duration is authoritative: rounding across legs must not
    // shorten or stretch it.
    timeline.durationSeconds = action.durationSeconds;
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
    // Every player instance turns, including ones this action does not move.
    //
    // This is intended, not an oversight, and it is the mirror mechanic's whole
    // read: the copies are one character the player is controlling in several
    // places, not several characters. They share one input, so they share one
    // facing. A copy pressed against a wall while its siblings walk right must
    // still turn right, or the set stops looking like one body and starts
    // looking like a crowd that has lost sync.
    //
    // Scoping this to the players the action moves therefore stays wrong even
    // under concurrency, where the obvious refactor would be to narrow it. What
    // does need narrowing is ambient facing - see the `!playerInput` branch of
    // `plans::worldStep`, which faces players from whoever a belt or slide
    // happened to move. That is not an input and has no business turning
    // players another action is driving.
    //
    // Pinned by `playerCopiesShareTheInputFacing` in PresentationTests.
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

    // One entity can own several motion tracks - a chained slide has one per
    // leg - and only the leg it is currently on may be applied.
    //
    // Applying all of them let the last one win, and a track whose leg has not
    // begun sets the entity to *that* leg's starting cell. So a block one tile
    // into a five-tile slide was drawn at the start of the final leg, which is
    // to say at its destination, for the entire slide. It then snapped back the
    // moment anything else re-synchronised it.
    const auto chosenTrackFor = [&](EntityTarget target) {
        std::size_t chosen = timeline.motions.size();
        for (std::size_t i = 0; i < timeline.motions.size(); ++i) {
            if (!(timeline.motions[i].target == target)) {
                continue;
            }
            if (chosen == timeline.motions.size()) {
                chosen = i;
                continue;
            }
            const float candidate = timeline.motions[i].startSeconds;
            const float current = timeline.motions[chosen].startSeconds;
            const bool candidateBegun = candidate <= sourceTime;
            const bool currentBegun = current <= sourceTime;
            // The latest leg that has begun; before any has, the earliest,
            // which is what holds the entity at the action's start pose.
            if (candidateBegun && (!currentBegun || candidate > current)) {
                chosen = i;
            } else if (!candidateBegun && !currentBegun && candidate < current) {
                chosen = i;
            }
        }
        return chosen;
    };

    for (std::size_t index = 0; index < timeline.motions.size(); ++index) {
        const ActionMotionTrack& track = timeline.motions[index];
        EntityVisual* visual = findMotionVisual(track.target);
        if (visual == nullptr) {
            continue;
        }
        if (chosenTrackFor(track.target) != index) {
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
