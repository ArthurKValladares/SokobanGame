#include "engine/GameplaySession.hpp"

#include "engine/PresentationTransactionBuilder.hpp"
#include "engine/Reservation.hpp"
#include "engine/StateDelta.hpp"

#include <variant>

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace sokoban {
namespace {

bool anyEntityChangedCell(const GameState& before, const GameState& after)
{
    const std::size_t playerCount = std::min(
        before.players.size(), after.players.size());
    for (std::size_t i = 0; i < playerCount; ++i) {
        if (!(before.players[i].cell == after.players[i].cell)) {
            return true;
        }
    }

    const std::size_t movableCount = std::min(
        before.movables.size(), after.movables.size());
    for (std::size_t i = 0; i < movableCount; ++i) {
        if (!(before.movables[i].cell == after.movables[i].cell)) {
            return true;
        }
    }

    const std::size_t enemyCount = std::min(
        before.enemies.size(), after.enemies.size());
    for (std::size_t i = 0; i < enemyCount; ++i) {
        if (!(before.enemies[i].cell == after.enemies[i].cell)) {
            return true;
        }
    }
    return false;
}

std::vector<EntityId> platformRidersForScheduling(const GameState& source, EntityId robot)
{
    const auto platform = std::ranges::find_if(source.movables, [&](const auto& movable) {
        const auto index = static_cast<std::size_t>(&movable - source.movables.data());
        return resolvedEntityId(EntityKind::Movable, movable.id, index) == robot;
    });
    if (platform == source.movables.end()) return {};
    std::vector<EntityId> riders;
    GridPosition3 cell = platform->cell;
    for (++cell.z; ; ++cell.z) {
        const auto player = std::ranges::find_if(source.players, [&](const auto& unit) {
            return !unit.dead && unit.cell == cell;
        });
        if (player != source.players.end()) {
            riders.push_back(resolvedEntityId(EntityKind::Player, player->id,
                static_cast<std::size_t>(player - source.players.begin())));
        } else if (const auto* movable = rules::movableAt(source, cell)) {
            riders.push_back(resolvedEntityId(EntityKind::Movable, movable->id,
                static_cast<std::size_t>(movable - source.movables.data())));
        } else if (const auto* enemy = rules::enemyAt(source, cell)) {
            riders.push_back(resolvedEntityId(EntityKind::Enemy, enemy->id,
                static_cast<std::size_t>(enemy - source.enemies.data())));
        } else {
            break;
        }
    }
    return riders;
}

bool actionMovesPlatformRider(const GameplaySession::Action& action, EntityId robot)
{
    const auto changed = StateDelta::between(action.before, action.after);
    return changed.changesAny(platformRidersForScheduling(action.before, robot)) ||
        changed.changesAny(platformRidersForScheduling(action.after, robot));
}

float turretShotTriggerSeconds(
    const ActionScheduler::InFlight& action,
    const plans::TurretShotCue& cue)
{
    if (action.legs.empty() || cue.legIndex >= action.legs.size()) {
        return 0.0f;
    }

    const GameState& beforeLeg = cue.legIndex == 0
        ? action.plan.before
        : action.legs[cue.legIndex - 1];
    const GameState& afterLeg = action.legs[cue.legIndex];
    if (!anyEntityChangedCell(beforeLeg, afterLeg)) {
        // A stationary mutual volley is the action, rather than a reaction to
        // its movement, so it begins at the start of this leg.
        return action.legBoundarySeconds(cue.legIndex);
    }

    // Rule resolution observes the destination state, but presentation must
    // not reveal that reaction while the entity is still travelling there.
    return action.legBoundarySeconds(cue.legIndex + 1);
}

// movementDirection, anyPlayerMoved and firstPlayerMovementDirection now live
// in plans:: alongside the planners that need them.

// Replays the entry forward under `scope` and reports whether it lands exactly
// where it claims to, for some input the player could have given.
bool replayMatches(
    const Level& level,
    const GameplaySession::Action& action,
    const rules::StepRates& rates,
    const rules::StepScope& scope)
{
    constexpr std::array<std::optional<MoveDirection>, 5> inputs {
        std::nullopt,
        MoveDirection::Up,
        MoveDirection::Down,
        MoveDirection::Left,
        MoveDirection::Right,
    };
    return std::ranges::any_of(inputs, [&](std::optional<MoveDirection> input) {
        GameState current =
            rules::scopedStep(level, action.before, input, rates, scope);
        if (current == action.before) {
            return false;
        }
        // A move counts once, judged on the input-driven first step, however
        // many steps the slide it started runs for.
        const int expectedMoveCount = action.playerMoveCountBefore +
            (input && plans::anyPlayerMoved(action.before, current) ? 1 : 0);
        if (action.playerMoveCountAfter != expectedMoveCount) {
            return false;
        }
        // Saves written before slides were chained hold one step per action,
        // so a single step still has to be accepted. Newer saves hold the whole
        // chain, which is what replaying the plan produces.
        if (current == action.after) {
            return true;
        }
        if (!plans::anySlideMomentum(current)) {
            return false;
        }

        // With fixed input, scope and rates, repeating the complete mechanical
        // state proves the endpoint is unreachable. Brent's cycle detector
        // keeps one checkpoint instead of retaining every replayed state.
        GameState checkpoint = current;
        std::size_t power = 1;
        std::size_t distance = 0;
        // The input-driven first step already consumed one unit. Bound work
        // even when a long joint cycle has not repeated within this budget.
        for (std::size_t steps = 1;
             steps < GameplaySession::maxCheckpointReplaySteps &&
                 plans::anySlideMomentum(current);
             ++steps) {
            GameState next =
                rules::scopedStep(level, current, std::nullopt, rates, scope);
            if (next == action.after) {
                return true;
            }
            if (next == current || next == checkpoint) {
                return false;
            }
            current = std::move(next);
            if (++distance == power) {
                checkpoint = current;
                power *= 2;
                distance = 0;
            }
        }
        return false;
    });
}

bool matchesForwardTransition(
    const Level& level,
    const GameplaySession::Action& action,
    const rules::StepRates& rates)
{
    const GameState initial = rules::initialState(level);
    if (!rules::anyPlayerDead(action.before) && !(action.before == initial) &&
        action.after == initial && action.playerMoveCountAfter == 0) {
        return true;
    }

    if (const std::optional<GameState> reflected =
            rules::activate(level, action.before)) {
        if (*reflected == action.after &&
            action.playerMoveCountAfter == action.playerMoveCountBefore) {
            return true;
        }
    }

    // Whole world first, and unchanged. Every save written before actions were
    // planned per entity is validated by exactly the replay it always was -
    // `rules::step` is `scopedStep` with an empty scope, which is what this is.
    if (replayMatches(level, action, rates, rules::StepScope {})) {
        return true;
    }

    // Input belongs to one controller group. A gate closing can also change
    // another hero by killing it; replaying every changed hero as an input
    // actor would instead let the victim walk out of the gate. Try the actual
    // controller scopes, still requiring the entire state and count to match.
    std::vector<EntityId> triedControllers;
    for (std::size_t i = 0; i < action.before.players.size(); ++i) {
        if (action.before.players[i].dead) {
            continue;
        }
        const EntityId controller = rules::playerControllerId(action.before, i);
        if (std::ranges::find(triedControllers, controller) !=
            triedControllers.end()) {
            continue;
        }
        triedControllers.push_back(controller);
        rules::StepScope scope;
        for (std::size_t j = 0; j < action.before.players.size(); ++j) {
            const auto& player = action.before.players[j];
            if (!player.dead &&
                rules::playerControllerId(action.before, j) == controller) {
                scope.actors.push_back(
                    resolvedEntityId(EntityKind::Player, player.id, j));
            }
        }
        if (replayMatches(level, action, rates, scope)) {
            return true;
        }
    }

    // Then scoped, for entries a concurrent schedule produced.
    //
    // An entry no longer holds a whole-world transition: it holds what one
    // action changed, and that action moved the entities it was answerable for
    // while others moved theirs. Replaying the whole world would step
    // bystanders that this action never touched and land somewhere else, so an
    // otherwise sound save would be rejected.
    //
    // This is weaker than the whole-world check - a single entity replayed
    // under a scope naming only itself is close to asking whether it could have
    // moved at all - and that is the price of concurrency. What it still
    // catches is an entry whose claimed outcome the rules would never produce
    // from its starting state, and the chain check around it still pins every
    // entry to the one before it and the last to the saved state.
    // Deliberately keyed off the delta rather than positions: an entry is
    // stored as a change replayed onto the running chain, so this is the same
    // actor set the scheduler committed.
    std::vector<EntityId> changed =
        StateDelta::between(action.before, action.after).changedEntityIds();
    if (changed.empty()) {
        // An empty scope means the whole world, which the branch above already
        // tried; an action that changed nothing is not a transition anyway.
        return false;
    }
    return replayMatches(
        level, action, rates, rules::StepScope { .actors = std::move(changed) });
}

void normalizeLegacyPlayers(GameState& state, const Level& level)
{
    if (state.players.empty()) {
        return;
    }
    const EntityId legacyController = resolvedEntityId(
        EntityKind::Player, state.players.front().id, 0);
    for (GameState::Player& player : state.players) {
        if (!player.character) {
            player.character = level.character();
        }
        if (player.controller == invalidEntityId) {
            // Before authored multi-hero levels, every player beyond the first
            // was a mirror copy and shared one input.
            player.controller = legacyController;
        }
    }
}

void normalizeLegacySnapshot(
    GameplaySession::Snapshot& snapshot, const Level& level)
{
    const GameState initial = rules::initialState(level);
    const auto upgradeWardrobes = [&initial](GameState& state) {
        if (state.wardrobes.empty() && !initial.wardrobes.empty()) {
            state.wardrobes = initial.wardrobes;
        }
    };
    const std::size_t mirrorCount = std::ranges::count_if(
        initial.movables, [](const GameState::Movable& unit) {
            return tileTypeIsMirror(unit.type);
        });
    const auto upgradeMirrors = [&](GameState& state) {
        if (mirrorCount == 0 ||
            state.movables.size() + mirrorCount != initial.movables.size() ||
            std::ranges::any_of(state.movables, [](const auto& unit) {
                return tileTypeIsMirror(unit.type);
            })) {
            return;
        }
        // Adding mirrors to the authored entity order shifts object, enemy,
        // and copied-hero ids. Apply the same mapping to every history state.
        const EntityId playerCount = static_cast<EntityId>(initial.players.size());
        const EntityId oldMovableCount = static_cast<EntityId>(state.movables.size());
        const auto remap = [&](EntityId id) -> EntityId {
            if (id == invalidEntityId || id <= playerCount) {
                return id;
            }
            const EntityId oldIndex = id - playerCount - 1;
            if (oldIndex < oldMovableCount) {
                EntityId index = 0;
                for (const auto& unit : initial.movables) {
                    if (!tileTypeIsMirror(unit.type) && index++ == oldIndex) {
                        return unit.id;
                    }
                }
            }
            return id + static_cast<EntityId>(mirrorCount);
        };
        for (auto& hero : state.players) {
            hero.id = remap(hero.id);
            hero.controller = remap(hero.controller);
        }
        for (auto& enemy : state.enemies) {
            enemy.id = remap(enemy.id);
        }
        std::vector<GameState::Movable> upgraded;
        upgraded.reserve(initial.movables.size());
        std::size_t oldIndex = 0;
        for (const auto& authored : initial.movables) {
            if (tileTypeIsMirror(authored.type)) {
                auto mirror = authored;
                mirror.quarterTurns = rules::mirrorQuarterTurnsAt(state, mirror.cell);
                upgraded.push_back(mirror);
            } else {
                auto unit = state.movables[oldIndex++];
                unit.id = remap(unit.id);
                upgraded.push_back(unit);
            }
        }
        state.movables = std::move(upgraded);
        state.turnedMirrors.clear();
    };
    upgradeMirrors(snapshot.state);
    upgradeWardrobes(snapshot.state);
    normalizeLegacyPlayers(snapshot.state, level);
    for (GameplaySession::Action& action : snapshot.undoStack) {
        upgradeMirrors(action.before);
        upgradeMirrors(action.after);
        upgradeWardrobes(action.before);
        upgradeWardrobes(action.after);
        normalizeLegacyPlayers(action.before, level);
        normalizeLegacyPlayers(action.after, level);
    }
}

std::vector<GridPosition3> witchSwapDestinations(const ActionPlan& action)
{
    std::vector<GridPosition3> destinations;
    const std::size_t playerCount = std::min(
        action.before.players.size(), action.after.players.size());
    const std::size_t movableCount = std::min(
        action.before.movables.size(), action.after.movables.size());
    const std::size_t enemyCount = std::min(
        action.before.enemies.size(), action.after.enemies.size());
    for (std::size_t playerIndex = 0;
         playerIndex < playerCount;
         ++playerIndex) {
        const GameState::Player& beforePlayer =
            action.before.players[playerIndex];
        const GameState::Player& afterPlayer =
            action.after.players[playerIndex];
        if (beforePlayer.character != CharacterType::Witch ||
            beforePlayer.cell == afterPlayer.cell) {
            continue;
        }
        bool swapped = false;
        for (std::size_t movableIndex = 0;
             movableIndex < movableCount;
             ++movableIndex) {
            if (action.before.movables[movableIndex].cell != afterPlayer.cell ||
                action.after.movables[movableIndex].cell != beforePlayer.cell) {
                continue;
            }
            swapped = true;
            break;
        }
        for (std::size_t enemyIndex = 0;
             !swapped && enemyIndex < enemyCount;
             ++enemyIndex) {
            if (action.before.enemies[enemyIndex].cell == afterPlayer.cell &&
                action.after.enemies[enemyIndex].cell == beforePlayer.cell) {
                swapped = true;
            }
        }
        if (!swapped) {
            continue;
        }
        for (GridPosition3 endpoint : {
                 beforePlayer.cell, afterPlayer.cell }) {
            if (std::ranges::find(destinations, endpoint) ==
                destinations.end()) {
                destinations.push_back(endpoint);
            }
        }
    }
    return destinations;
}

} // namespace

void GameplaySession::reset(const Level& level)
{
    robotPatrolPauses_.clear();
    robotPatrolActions_.clear();
    failedAmbientAttempt_.valid = false;
    undoBaseState_ = rules::initialState(level);
    scheduler_.reset(undoBaseState_, stepDurationSeconds_);
    pendingCommands_.clear();
    readingLectern_.reset();
    readingHero_ = invalidEntityId;
    waitingForMoveRelease_ = false;
    undoHistory_.clear();
    undoGroups_.clear();
    nextCausalGroup_ = 1;
    completedActionCount_ = 0;
    playerMoveCount_ = 0;
    activeHeroController_ = undoBaseState_.players.empty()
        ? invalidEntityId
        : rules::playerControllerId(undoBaseState_, 0);
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
    witchSwapSequence_ = 0;
    lastWitchSwapDestinations_.clear();
    autoMotionPaused_ = false;
    inputLog_.clear();
}

void GameplaySession::resetToState(
    const GameState& state, EntityId activeHeroController)
{
    robotPatrolPauses_.clear();
    robotPatrolActions_.clear();
    failedAmbientAttempt_.valid = false;
    undoBaseState_ = state;
    scheduler_.reset(undoBaseState_, stepDurationSeconds_);
    pendingCommands_.clear();
    readingLectern_.reset();
    readingHero_ = invalidEntityId;
    waitingForMoveRelease_ = false;
    undoHistory_.clear();
    undoGroups_.clear();
    nextCausalGroup_ = 1;
    completedActionCount_ = 0;
    playerMoveCount_ = 0;
    activeHeroController_ = activeHeroController;
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
    witchSwapSequence_ = 0;
    lastWitchSwapDestinations_.clear();
    autoMotionPaused_ = false;
    inputLog_.clear();
}

void GameplaySession::setStepDurationSeconds(float durationSeconds)
{
    if (stepDurationSeconds_ != durationSeconds) {
        ++planningPolicyRevision_;
    }
    stepDurationSeconds_ = durationSeconds;
    scheduler_.setStepDurationSeconds(durationSeconds);
}

const GameplaySession::Action& GameplaySession::activeAction() const
{
    static const Action idle {};
    const ActionScheduler::InFlight* action = scheduler_.oldest();
    return action == nullptr ? idle : action->plan;
}

const std::vector<GameState>& GameplaySession::activeActionLegs() const
{
    static const std::vector<GameState> none {};
    const ActionScheduler::InFlight* action = scheduler_.oldest();
    return action == nullptr ? none : action->legs;
}

float GameplaySession::activeActionDuration() const
{
    return activeAction().durationSeconds;
}

float GameplaySession::activeActionElapsedSeconds() const
{
    const ActionScheduler::InFlight* action = scheduler_.oldest();
    if (action == nullptr) {
        return 0.0f;
    }
    // The scheduler lets elapsed run past the duration so that it can order
    // commits by how far each overshot, and lets it sit below zero while an
    // action is deferred. A sampling time is neither.
    return std::clamp(
        action->elapsedSeconds,
        0.0f,
        std::max(action->plan.durationSeconds, 0.0f));
}

GameState GameplaySession::projectedState() const
{
    return projectedStateView();
}

const GameState& GameplaySession::planningState() const
{
    return scheduler_.stateAtCurrentProgressView();
}

const std::optional<rules::MirrorActivationPreview>&
GameplaySession::activationPreview(const Level& level, bool projected) const
{
    if (previewLevel_ != &level) {
        previewLevel_ = &level;
        previewRevisions_ = {};
    }
    const std::size_t index = projected ? 1 : 0;
    if (previewRevisions_[index] != scheduler_.stateRevision()) {
        const GameState& source = projected ? projectedStateView() : state();
        activationPreviews_[index] = rules::anyPlayerDead(source)
            ? std::nullopt : rules::previewActivation(level, source);
        invalidMirrorPreviews_[index] = rules::anyPlayerDead(source)
            ? rules::InvalidMirrorPreview {}
            : rules::previewInvalidMirrors(level, source);
        previewRevisions_[index] = scheduler_.stateRevision();
    }
    return activationPreviews_[index];
}

const std::vector<rules::MirrorBeamSegment>&
GameplaySession::invalidMirrorBeams(const Level& level, bool projected) const
{
    return invalidMirrorPreview(level, projected).beamSegments;
}

const rules::InvalidMirrorPreview&
GameplaySession::invalidMirrorPreview(const Level& level, bool projected) const
{
    (void)activationPreview(level, projected);
    return invalidMirrorPreviews_[projected ? 1 : 0];
}

std::size_t GameplaySession::continuationCausalGroup(
    const Action& action) const
{
    const std::vector<EntityId> continued =
        StateDelta::between(action.before, action.after).changedEntityIds();
    for (std::size_t i = undoHistory_.size(); i-- > 0;) {
        if (StateDelta::between(
                undoHistory_[i].before,
                undoHistory_[i].after)
                .changesAny(continued)) {
            return undoGroups_[i];
        }
    }
    return 0;
}

const ActionScheduler::InFlight* GameplaySession::findInFlight(
    std::size_t actionId) const
{
    const auto found = std::ranges::find(
        scheduler_.inFlight(), actionId, &ActionScheduler::InFlight::id);
    return found == scheduler_.inFlight().end() ? nullptr : &*found;
}

float GameplaySession::timeToNextCompletion() const
{
    bool any = false;
    float soonest = 0.0f;
    for (const ActionScheduler::InFlight& action : scheduler_.inFlight()) {
        const float remaining = std::max(
            action.plan.durationSeconds - action.elapsedSeconds, 0.0f);
        soonest = any ? std::min(soonest, remaining) : remaining;
        any = true;
    }
    return soonest;
}

std::optional<float> GameplaySession::timeToNextRobotPatrol() const
{
    if (autoMotionPaused_ || readingLectern_ || stepRates_.robot <= 0 ||
        rules::anyPlayerDead(state())) return std::nullopt;
    std::optional<float> soonest;
    for (const auto& pause : robotPatrolPauses_) {
        const auto robot = std::ranges::find_if(state().movables, [&](const auto& movable) {
            const auto index = static_cast<std::size_t>(&movable - state().movables.data());
            return resolvedEntityId(EntityKind::Movable, movable.id, index) == pause.robot &&
                !movable.dead && !movable.fallen;
        });
        if (robot == state().movables.end() || pause.remainingSeconds <= 0.0f) continue;
        soonest = soonest ? std::min(*soonest, pause.remainingSeconds) : pause.remainingSeconds;
    }
    return soonest;
}

bool GameplaySession::anyActionComplete() const
{
    return std::ranges::any_of(
        scheduler_.inFlight(),
        [](const ActionScheduler::InFlight& action) {
            // Deferred actions have not started; see `commitFinished`.
            return action.elapsedSeconds >= 0.0f &&
                action.elapsedSeconds >= action.plan.durationSeconds;
        });
}

GameplaySession::Snapshot GameplaySession::snapshot() const
{
    Snapshot result {
        .state = state(),
        .undoStack = undoHistory_,
        .playerMoveCount = playerMoveCount_,
        .automaticMotionPaused = autoMotionPaused_,
        .activeHeroController = activeHeroController_,
    };

    // A completed action may have been planned against the current leg of an
    // older action that is still in flight. A direct follower behind a slide
    // is the important case: committed state still has the block at the start
    // of its slide while the follower has entered that released cell. Preserve
    // enough completed slide progress in the checkpoint copy to keep its
    // history linear and replayable. Live state and history remain untouched;
    // an in-flight action with no later completed entry is still dropped.
    const auto rebaseResultFrom = [&](std::size_t index) {
        for (std::size_t i = index; i < result.undoStack.size(); ++i) {
            Action& entry = result.undoStack[i];
            const StateDelta delta =
                StateDelta::between(entry.before, entry.after);
            const int moved =
                entry.playerMoveCountAfter - entry.playerMoveCountBefore;
            entry.before = i == 0
                ? undoBaseState_
                : result.undoStack[i - 1].after;
            entry.after = entry.before;
            delta.applyTo(entry.after);
            entry.playerMoveCountBefore = i == 0
                ? 0
                : result.undoStack[i - 1].playerMoveCountAfter;
            entry.playerMoveCountAfter =
                entry.playerMoveCountBefore + moved;
        }
    };

    bool incorporatedProgress = false;
    for (const ActionScheduler::InFlight& action : scheduler_.inFlight()) {
        if (action.causalGroup == 0) {
            continue;
        }
        const auto group = std::ranges::find(
            undoGroups_, action.causalGroup);
        if (group == undoGroups_.end()) {
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(
            std::distance(undoGroups_.begin(), group));
        if (index + 1 >= result.undoStack.size()) {
            continue;
        }
        const StateDelta progress = StateDelta::between(
            action.plan.before,
            action.stateAtCurrentProgress());
        if (progress.empty()) {
            continue;
        }
        progress.applyTo(result.undoStack[index].after);
        rebaseResultFrom(index + 1);
        incorporatedProgress = true;
    }
    if (incorporatedProgress) {
        result.state = result.undoStack.back().after;
    }

    for (Action& action : result.undoStack) {
        action.durationSeconds = config::stepDurationSeconds;
        action.reversed = false;
        action.facingDirection.reset();
    }
    return result;
}

bool GameplaySession::restore(const Level& level, const Snapshot& snapshot)
{
    if (snapshot.playerMoveCount < 0) {
        return false;
    }

    Snapshot normalized = snapshot;
    normalizeLegacySnapshot(normalized, level);

    GameState expectedState = rules::initialState(level);
    int expectedMoveCount = 0;
    for (const Action& action : normalized.undoStack) {
        if (action.reversed ||
            action.playerMoveCountBefore < 0 ||
            action.playerMoveCountAfter < 0 ||
            !(action.before == expectedState) ||
            action.playerMoveCountBefore != expectedMoveCount ||
            !matchesForwardTransition(level, action, stepRates_)) {
            return false;
        }
        expectedState = action.after;
        expectedMoveCount = action.playerMoveCountAfter;
    }
    if (!(normalized.state == expectedState) ||
        normalized.playerMoveCount != expectedMoveCount) {
        return false;
    }

    scheduler_.reset(normalized.state, stepDurationSeconds_);
    robotPatrolPauses_.clear();
    robotPatrolActions_.clear();
    failedAmbientAttempt_.valid = false;
    pendingCommands_.clear();
    readingLectern_.reset();
    readingHero_ = invalidEntityId;
    waitingForMoveRelease_ = false;
    // The stack was just validated against a replay from here, so it is exactly
    // the anchor the chain is built on.
    undoBaseState_ = rules::initialState(level);
    undoHistory_ = normalized.undoStack;
    // Nothing is in flight after a restore, so every group is closed and no
    // later action can fold into one of these. Distinct ids say exactly that.
    undoGroups_.clear();
    undoGroups_.reserve(undoHistory_.size());
    for (std::size_t i = 0; i < undoHistory_.size(); ++i) {
        undoGroups_.push_back(nextCausalGroup_++);
    }
    completedActionCount_ = 0;
    playerMoveCount_ = normalized.playerMoveCount;
    const bool activeControllerExists = std::ranges::any_of(
        normalized.state.players,
        [&](const GameState::Player& player) {
            const std::size_t index = static_cast<std::size_t>(
                &player - normalized.state.players.data());
            return rules::playerControllerId(normalized.state, index) ==
                normalized.activeHeroController;
        });
    activeHeroController_ = activeControllerExists
        ? normalized.activeHeroController
        : normalized.state.players.empty()
            ? invalidEntityId
            : rules::playerControllerId(normalized.state, 0);
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
    witchSwapSequence_ = 0;
    lastWitchSwapDestinations_.clear();
    autoMotionPaused_ = normalized.automaticMotionPaused;
    inputLog_.clear();
    return true;
}

void GameplaySession::enqueue(Command command)
{
    if ((readingLectern_ && command.type != CommandType::Move) || waitingForMoveRelease_) return;
    command.queuedAtSeconds = scheduler_.clockSeconds();
    // Full queue drops the oldest rather than refusing the newest: the most
    // recent input is the one the player still means.
    while (pendingCommands_.size() >= maxQueuedCommands) {
        pendingCommands_.pop_front();
    }
    pendingCommands_.push_back(command);
}

void GameplaySession::queueMove(MoveDirection direction)
{
    enqueue({
        .type = CommandType::Move,
        .direction = direction,
        .controller = activeHeroController_,
    });
}

void GameplaySession::queueActivate()
{
    enqueue({ .type = CommandType::Activate });
}

void GameplaySession::queueUndo()
{
    enqueue({ .type = CommandType::Undo });
}

void GameplaySession::queueRestart()
{
    enqueue({ .type = CommandType::Restart });
}

std::optional<CharacterType> GameplaySession::activeHeroCharacter() const
{
    for (std::size_t index = 0; index < state().players.size(); ++index) {
        const auto& player = state().players[index];
        if (!player.dead && rules::playerControllerId(state(), index) == activeHeroController_) {
            return player.character;
        }
    }
    return std::nullopt;
}

void GameplaySession::cycleActiveHero()
{
    if (readingLectern_) return;
    const GameState progress = planningState();
    std::vector<EntityId> controllers;
    for (std::size_t i = 0; i < progress.players.size(); ++i) {
        const EntityId controller = rules::playerControllerId(progress, i);
        if (std::ranges::find(controllers, controller) == controllers.end()) {
            controllers.push_back(controller);
        }
    }
    if (controllers.size() < 2) {
        return;
    }
    const auto current = std::ranges::find(
        controllers, activeHeroController_);
    activeHeroController_ = current == controllers.end() ||
            std::next(current) == controllers.end()
        ? controllers.front()
        : *std::next(current);
    inputLog_.push_back(PlayerInput::CycleHero);
}

GameplaySession::StartOutcome GameplaySession::runCommand(
    const Level& level, const Command& command, const Controls& controls)
{
    // Anything but undo is ignored while a player is dead, and dropped rather
    // than held: the world cannot move again until the death is taken back.
    if (rules::anyPlayerDead(planningState())) {
        return command.type == CommandType::Undo
            ? tryStartUndoMove(level)
            : StartOutcome::Impossible;
    }

    switch (command.type) {
    case CommandType::Undo:
        return tryStartUndoMove(level);
    case CommandType::Restart:
        return tryStartRestart(level);
    case CommandType::Activate:
        return tryStartActivationAction(level);
    case CommandType::Move:
        break;
    }

    const std::optional<MoveDirection> perpendicular =
        command.direction == MoveDirection::Up ||
            command.direction == MoveDirection::Down
        ? controls.horizontalMove
        : controls.verticalMove;
    return tryStartHeldDirection(
        level, command.direction, perpendicular, command.controller);
}

bool GameplaySession::tryStartNextAction(const Level& level, const Controls& controls)
{
    if (readingLectern_ && moving()) return false;
    if (waitingForMoveRelease_) {
        if (controls.verticalMove || controls.horizontalMove) return false;
        waitingForMoveRelease_ = false;
    }
    // Concurrent admission is decided per action from entity ownership, cell
    // paths and world signal transitions.
    //
    // Undo and restart keep their own `moving()` checks. Those are not about
    // cells - they rewrite the whole world, so they cannot share it with an
    // action that planned against the old one.

    // Queued commands first, and before ambient motion below. That ordering is
    // what keeps a belt from starving a player: a rider releases its
    // reservation at the end of every step and immediately takes another, so a
    // queued action waiting on a cell in the belt's path would otherwise be
    // shut out for as long as the belt runs.
    while (!pendingCommands_.empty()) {
        const Command command = pendingCommands_.front();
        pendingCommands_.pop_front();
        if (isStale(command)) {
            continue;
        }

        const auto readingBeforeCommand = readingLectern_;
        switch (runCommand(level, command, controls)) {
        case StartOutcome::Started:
            return true;
        case StartOutcome::Impossible:
            if (readingLectern_ && readingLectern_ != readingBeforeCommand) return false;
            continue;
        case StartOutcome::Refused:
            // Back where it came from, ahead of anything queued behind it, and
            // stop draining - the commands behind it are the player's later
            // intentions and must not overtake it.
            pendingCommands_.push_front(command);
            return false;
        }
    }

    const GameState& current = planningState();
    if (rules::anyPlayerDead(current)) {
        return !readingLectern_ && controls.undoHeld &&
            tryStartUndoMove(level) == StartOutcome::Started;
    }

    if (tryStartHeldMove(level, controls) == StartOutcome::Started) {
        return true;
    }

    return !readingLectern_ && !autoMotionPaused_ &&
        rules::hasPendingMotion(level, current) &&
        tryStartAmbientMotion(level) == StartOutcome::Started;
}

void GameplaySession::advanceActiveAction(float dt)
{
    scheduler_.advanceClock(dt);
    for (auto& pause : robotPatrolPauses_) {
        pause.remainingSeconds = std::max(0.0f, pause.remainingSeconds - std::max(dt, 0.0f));
    }
    if (std::erase_if(robotPatrolPauses_, [](const auto& pause) {
            return pause.remainingSeconds <= 0.0f;
        }) != 0) {
        // The board may be unchanged throughout a dwell, but its motor can
        // now act again; do not reuse the cached failed ambient admission.
        failedAmbientAttempt_.valid = false;
    }
}

void GameplaySession::completeActiveAction()
{
    // The scheduler applies each finished action's delta - only what that
    // action changed, rather than its `after` wholesale. With one action in
    // flight the two are identical; once actions overlap, assigning a whole
    // state would erase whatever the others had already committed, because
    // `after` is a snapshot of the world as it stood when this action started.
    // Undo goes through the same path: an inverted action's endpoints are
    // swapped, so its delta is the inverse delta.
    //
    // What stays here is the bookkeeping only the session knows about.
    for (const ActionScheduler::InFlight& finished : scheduler_.commitFinished()) {
        recordCompletion(finished.plan, finished.causalGroup);
        const auto patrol = std::ranges::find(robotPatrolActions_, finished.id,
            &RobotPatrolAction::actionId);
        if (patrol != robotPatrolActions_.end()) {
            for (const EntityId id : patrol->robots) {
                const auto robot = std::ranges::find_if(state().movables, [&](const auto& movable) {
                    const auto index = static_cast<std::size_t>(&movable - state().movables.data());
                    return resolvedEntityId(EntityKind::Movable, movable.id, index) == id &&
                        !movable.dead && !movable.fallen;
                });
                if (robot != state().movables.end()) {
                    std::erase_if(robotPatrolPauses_, [id](const auto& pause) { return pause.robot == id; });
                    robotPatrolPauses_.push_back({ id, config::robotPatrolPauseSeconds });
                }
            }
            robotPatrolActions_.erase(patrol);
            failedAmbientAttempt_.valid = false;
        }
    }
}

std::vector<GameplaySession::TurretShotEvent>
GameplaySession::takeReadyTurretShots()
{
    std::vector<TurretShotEvent> ready;
    // Only cue flags change here; the in-flight vector is never resized.
    for (const auto& current : scheduler_.inFlight()) {
        ActionScheduler::InFlight* action = scheduler_.find(current.id);
        if (action == nullptr || action->elapsedSeconds < 0.0f) {
            continue;
        }
        for (plans::TurretShotCue& cue : action->turretShots) {
            if (cue.emitted) {
                continue;
            }
            const float triggerSeconds = turretShotTriggerSeconds(*action, cue);
            if (action->elapsedSeconds < triggerSeconds) {
                continue;
            }
            cue.emitted = true;
            // Stationary volleys retain the old wind-up to the end of their
            // leg. A reaction released at a movement boundary begins now.
            const float legEndSeconds = action->legBoundarySeconds(cue.legIndex + 1);
            ready.push_back({
                .shot = cue.shot,
                .impactDelaySeconds = std::max(
                    legEndSeconds - action->elapsedSeconds,
                    0.0f),
            });
        }
    }
    return ready;
}

const GameState& GameplaySession::undoBaseState() const
{
    return undoHistory_.empty() ? undoBaseState_ : undoHistory_.back().after;
}

void GameplaySession::setActionSoundCues(
    std::size_t actionId, std::vector<GameplaySoundCue> cues)
{
    if (auto* action = scheduler_.find(actionId)) {
        action->soundCues = std::move(cues);
    }
}

std::vector<GameplaySound> GameplaySession::takeReadySounds()
{
    std::vector<GameplaySound> ready;
    for (const auto& current : scheduler_.inFlight()) {
        auto* action = scheduler_.find(current.id);
        if (action->plan.reversed || action->elapsedSeconds < 0.0f) {
            continue;
        }
        for (auto& cue : action->soundCues) {
            if (!cue.emitted && action->elapsedSeconds >= cue.triggerSeconds) {
                cue.emitted = true;
                ready.push_back(cue.sound);
            }
        }
    }
    return ready;
}

void GameplaySession::rebaseUndoFrom(std::size_t index)
{
    for (std::size_t i = index; i < undoHistory_.size(); ++i) {
        Action& entry = undoHistory_[i];
        // Read the change out before overwriting the endpoints it is derived
        // from.
        const StateDelta delta = StateDelta::between(entry.before, entry.after);
        const int moved =
            entry.playerMoveCountAfter - entry.playerMoveCountBefore;

        entry.before = i == 0 ? undoBaseState_ : undoHistory_[i - 1].after;
        entry.after = entry.before;
        delta.applyTo(entry.after);
        entry.playerMoveCountBefore =
            i == 0 ? 0 : undoHistory_[i - 1].playerMoveCountAfter;
        entry.playerMoveCountAfter = entry.playerMoveCountBefore + moved;
    }
}

void GameplaySession::recordCompletion(
    const Action& action, std::size_t causalGroup)
{
    ++completedActionCount_;
    // The running total moves by what this action did, not to what it predicted
    // the total would be. A plan captures the total when it is made, and under
    // concurrency an ambient action planned alongside a player's step would
    // carry the count from before that step and drag it back down on commit.
    playerMoveCount_ +=
        action.playerMoveCountAfter - action.playerMoveCountBefore;

    if (action.reversed) {
        if (!undoHistory_.empty()) {
            undoHistory_.pop_back();
            undoGroups_.pop_back();
        }
        return;
    }

    const StateDelta delta = StateDelta::between(action.before, action.after);

    // A consequence folds into the entry its cause opened. Undo is a player's
    // idea, not the scheduler's: one input happened, so one undo puts back
    // everything that followed from it.
    const auto existing = causalGroup == 0
        ? undoGroups_.end()
        : std::ranges::find(undoGroups_, causalGroup);
    if (existing == undoGroups_.end()) {
        Action entry = action;
        entry.before = undoBaseState();
        entry.after = entry.before;
        delta.applyTo(entry.after);
        entry.playerMoveCountBefore = undoHistory_.empty()
            ? 0
            : undoHistory_.back().playerMoveCountAfter;
        entry.playerMoveCountAfter = entry.playerMoveCountBefore +
            (action.playerMoveCountAfter - action.playerMoveCountBefore);

        undoHistory_.push_back(std::move(entry));
        undoGroups_.push_back(
            causalGroup == 0 ? nextCausalGroup_++ : causalGroup);
        return;
    }

    // The changes compose: the consequence was planned from the state its cause
    // produced, so replaying its delta onto the cause's endpoint gives the
    // transition the player actually asked for.
    const std::size_t index =
        static_cast<std::size_t>(std::distance(undoGroups_.begin(), existing));
    Action& folded = undoHistory_[index];
    folded.presentation = concatenateTimelines(
        std::move(folded.presentation),
        action.presentation,
        folded.durationSeconds);
    folded.durationSeconds += action.durationSeconds;
    delta.applyTo(folded.after);
    folded.playerMoveCountAfter +=
        action.playerMoveCountAfter - action.playerMoveCountBefore;
    folded.playerPushing = folded.playerPushing || action.playerPushing;
    folded.playerPulling = folded.playerPulling || action.playerPulling;
    // Anything that committed between the cause and this consequence sits after
    // it in the stack and was chained to the endpoint that just moved.
    rebaseUndoFrom(index + 1);
}

float GameplaySession::activeActionRemainingSeconds() const
{
    return std::max(
        activeActionDuration() - activeActionElapsedSeconds(), 0.0f);
}

bool GameplaySession::activeActionComplete() const
{
    const ActionScheduler::InFlight* action = scheduler_.oldest();
    return action != nullptr && action->elapsedSeconds >= 0.0f &&
        action->elapsedSeconds >= action->plan.durationSeconds;
}

GameplaySession::StartOutcome GameplaySession::tryStartHeldMove(
    const Level& level, const Controls& controls)
{
    if (controls.undoHeld && !readingLectern_) {
        return tryStartUndoMove(level);
    }

    // Only an impossible move falls through to the other axis. A refusal is
    // about timing rather than geometry, and letting a sideways step in
    // because the intended one is momentarily blocked would move the player
    // somewhere they did not ask to go.
    if (controls.verticalMove) {
        const auto readingBeforeMove = readingLectern_;
        const StartOutcome outcome = tryStartHeldDirection(
            level,
            *controls.verticalMove,
            controls.horizontalMove,
            activeHeroController_);
        if (outcome != StartOutcome::Impossible || readingLectern_ != readingBeforeMove) {
            return outcome;
        }
    }
    if (controls.horizontalMove) {
        return tryStartHeldDirection(
            level,
            *controls.horizontalMove,
            controls.verticalMove,
            activeHeroController_);
    }

    return StartOutcome::Impossible;
}

GameplaySession::StartOutcome GameplaySession::tryStartPlayerStep(
    const Level& level, MoveDirection input, EntityId controller)
{
    if (!level.lecterns().empty()) {
        const GameState& current = planningState();
        const GridPosition offset = rules::directionOffset(input);
        for (std::size_t i = 0; i < current.players.size(); ++i) {
            const auto& player = current.players[i];
            if (player.dead || rules::playerControllerId(current, i) != controller ||
                rules::isUnitLocked(level, current, player.cell)) continue;
            const GridPosition3 destination {
                player.cell.x + offset.x, player.cell.y + offset.y, player.cell.z,
            };
            if (level.lecternAt(destination)) {
                // Buffered bumps wait until every animation has committed,
                // so the reading box opens over a settled world.
                if (moving()) return StartOutcome::Refused;
                readingLectern_ = destination;
                readingHero_ = player.id;
                pendingCommands_.clear();
                return StartOutcome::Impossible;
            }
        }
    }
    const PlanningStamp stamp = planningStamp(level);
    auto& failed = failedPlayerAttempts_[static_cast<std::size_t>(input)];
    if (failed.valid && failed.stamp == stamp && failed.controller == controller) {
        return failed.outcome;
    }
    const StartOutcome outcome = planPlayerStep(level, input, controller);
    failed = { stamp, controller, outcome, outcome != StartOutcome::Started };
    return outcome;
}

void GameplaySession::dismissLectern()
{
    if (!readingLectern_) return;
    readingLectern_.reset();
    readingHero_ = invalidEntityId;
    pendingCommands_.clear();
    // A held direction must be released before it can reopen the book.
    waitingForMoveRelease_ = true;
}

GameplaySession::PlanningStamp GameplaySession::planningStamp(const Level& level) const
{
    // Refresh progress at mechanical leg boundaries before reading its generation.
    (void)planningState();
    return { &level, scheduler_.stateRevision(), scheduler_.progressGeneration(),
        planningPolicyRevision_, scheduler_.currentStep(), config::robotPatrolSecondsPerTile };
}

GameplaySession::StartOutcome GameplaySession::planPlayerStep(
    const Level& level, MoveDirection input, EntityId controller)
{
    const GameState& current = planningState();
    std::optional<plans::PlannedAction> step = plans::planPlayerStep(
        level,
        current,
        input,
        stepRates_,
        stepDurationSeconds_,
        controller);
    if (!step) {
        return StartOutcome::Impossible;
    }
    if (actionUsesMovingPlatformSupport(step->action)) {
        return StartOutcome::Refused;
    }

    // The plan settles the outcome; the session only knows the running move
    // total, so it fills that in. One step, so one move - the slide it starts
    // is a separate action and adds nothing to the count.
    const bool countsAsPlayerMove =
        plans::anyPlayerMoved(step->action.before, step->action.after);
    step->action.playerMoveCountBefore = playerMoveCount_;
    step->action.playerMoveCountAfter =
        playerMoveCount_ + (countsAsPlayerMove ? 1 : 0);

    const float stepDuration = step->action.durationSeconds;
    const int stepLegs = static_cast<int>(step->legs.size());
    const GameState afterStep = step->action.after;
    const std::vector<GridPosition3> witchSwapEndpoints =
        witchSwapDestinations(step->action);

    std::vector<ActionScheduler::Pending> batch;
    batch.push_back(makePending(
        level,
        step->action,
        step->pressurePlateActivityChanged,
        step->legs,
        step->turretShots,
        ActionDeferral {},
        step->portalTransits));

    // Whatever the step leaves travelling, planned from the state the step
    // produces and starting one step behind it. Every slider goes into one
    // action: two planned separately would each treat the other as scenery
    // standing still, and could be routed through the same cell at the same
    // instant.
    if (std::optional<plans::PlannedAction> slide = plans::planSlides(
            level,
            afterStep,
            withoutEntitiesInFlight(plans::slidingEntities(afterStep)),
            stepRates_,
            stepDurationSeconds_)) {
        slide->action.playerMoveCountBefore = step->action.playerMoveCountAfter;
        slide->action.playerMoveCountAfter = step->action.playerMoveCountAfter;
        batch.push_back(makePending(
            level,
            slide->action,
            slide->pressurePlateActivityChanged,
            slide->legs,
            slide->turretShots,
            ActionDeferral { .steps = stepLegs, .seconds = stepDuration },
            slide->portalTransits));
    }

    if (!actionAdmissionAllows(batch)) {
        return StartOutcome::Impossible;
    }
    if (std::ranges::any_of(batch, [this](const auto& pending) {
            return actionUsesMovingPlatformSupport(pending.plan);
        })) {
        return StartOutcome::Refused;
    }

    // Allocated up front so the consequence carries the same group as its
    // cause and folds into its undo entry rather than opening one of its own.
    const std::size_t group = nextCausalGroup_++;
    const bool wasPaused = autoMotionPaused_;
    autoMotionPaused_ = false;
    if (scheduler_.tryStartAll(std::move(batch), group)) {
        autoMotionPaused_ = wasPaused;
        return StartOutcome::Refused;
    }
    if (readingLectern_) {
        const auto& destination = scheduler_.projectedStateView();
        const auto hero = std::ranges::find(destination.players, readingHero_, &GameState::Player::id);
        if (hero == destination.players.end() || hero->dead ||
            hero->cell.z != readingLectern_->z ||
            std::abs(hero->cell.x - readingLectern_->x) +
                std::abs(hero->cell.y - readingLectern_->y) != 1) {
            // Close as the admitted move starts, without suppressing held
            // movement. A blocked move or a distant mirror copy cannot close it.
            readingLectern_.reset();
            readingHero_ = invalidEntityId;
        }
    }
    if (!witchSwapEndpoints.empty()) {
        lastWitchSwapDestinations_ = witchSwapEndpoints;
        ++witchSwapSequence_;
    }
    inputLog_.push_back(
        input == MoveDirection::Up ? PlayerInput::Up
        : input == MoveDirection::Down ? PlayerInput::Down
        : input == MoveDirection::Left ? PlayerInput::Left
                                       : PlayerInput::Right);
    return StartOutcome::Started;
}

std::vector<EntityId> GameplaySession::withoutEntitiesInFlight(
    std::vector<EntityId> candidates) const
{
    std::vector<EntityId> ids;
    for (const ActionScheduler::InFlight& action : scheduler_.inFlight()) {
        StateDelta::between(action.plan.before, action.plan.after)
            .appendChangedEntityIds(ids);
    }
    std::erase_if(candidates, [&ids](EntityId id) {
        return std::ranges::find(ids, id) != ids.end();
    });
    return candidates;
}

GameplaySession::StartOutcome GameplaySession::tryStartAmbientMotion(
    const Level& level)
{
    const PlanningStamp stamp = planningStamp(level);
    if (failedAmbientAttempt_.valid && failedAmbientAttempt_.stamp == stamp) {
        return failedAmbientAttempt_.outcome;
    }
    const StartOutcome outcome = planAmbientMotion(level);
    failedAmbientAttempt_ = { stamp, invalidEntityId, outcome,
        outcome != StartOutcome::Started };
    return outcome;
}

bool GameplaySession::robotHasRiderInFlight(EntityId robot) const
{
    for (const auto& action : scheduler_.inFlight()) {
        // Include the destination stack: a boarding hero still travels
        // from the ledge when the dwell expires and must retain support.
        if (actionMovesPlatformRider(action.plan, robot)) return true;
    }
    return false;
}

bool GameplaySession::actionUsesMovingPlatformSupport(const Action& action) const
{
    for (const auto& moving : scheduler_.inFlight()) {
        const auto count = std::min(moving.plan.before.movables.size(),
            moving.plan.after.movables.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& before = moving.plan.before.movables[index];
            const auto& after = moving.plan.after.movables[index];
            if (!tileTypeIsMovablePlatform(before.type) ||
                (before.cell.x == after.cell.x && before.cell.y == after.cell.y)) continue;
            const EntityId robot = resolvedEntityId(EntityKind::Movable, before.id, index);
            // A new plan must not board the old top of a platform already on
            // its way elsewhere. Its base and the hero's top-level path use
            // different cells, so ordinary path reservations cannot catch it.
            if (actionMovesPlatformRider(action, robot)) return true;
        }
    }
    return false;
}

GameplaySession::StartOutcome GameplaySession::planAmbientMotion(const Level& level)
{
    const GameState& current = planningState();
    if (std::optional<plans::PlannedAction> volley = plans::planTurretVolley(
            level,
            current,
            withoutEntitiesInFlight(
                rules::mutuallyFacingTurrets(level, current)),
            stepRates_,
            stepDurationSeconds_)) {
        volley->action.playerMoveCountBefore = playerMoveCount_;
        volley->action.playerMoveCountAfter = playerMoveCount_;
        if (!actionAdmissionAllows(volley->action)) {
            return StartOutcome::Impossible;
        }
        return beginAction(
                   level,
                   volley->action,
                   volley->pressurePlateActivityChanged,
                   std::move(volley->legs),
                   std::move(volley->turretShots),
                   0,
                   {},
                   std::move(volley->portalTransits))
            ? StartOutcome::Started
            : StartOutcome::Refused;
    }

    // Momentum before belts, matching the order the rules resolve intents in:
    // a slide overrides the belt under it, and an entity only becomes a rider
    // once it has stopped.
    if (std::optional<plans::PlannedAction> slide = plans::planSlides(
            level,
            current,
            withoutEntitiesInFlight(plans::slidingEntities(current)),
            stepRates_,
            stepDurationSeconds_)) {
        slide->action.playerMoveCountBefore = playerMoveCount_;
        slide->action.playerMoveCountAfter = playerMoveCount_;
        if (!actionAdmissionAllows(slide->action)) {
            return StartOutcome::Impossible;
        }
        const std::size_t group = continuationCausalGroup(slide->action);
        return beginAction(
                   level,
                   slide->action,
                   slide->pressurePlateActivityChanged,
                   std::move(slide->legs),
                   std::move(slide->turretShots),
                   group,
                   {},
                   std::move(slide->portalTransits))
            ? StartOutcome::Started
            : StartOutcome::Refused;
    }

    auto conveyorActors = withoutEntitiesInFlight(plans::conveyorRiders(level, current));
    std::erase_if(conveyorActors, [&](EntityId id) {
        return std::ranges::any_of(scheduler_.inFlight(), [&](const auto& action) {
            return actionMovesPlatformRider(action.plan, id);
        });
    });
    const auto streamActors = plans::waterStreamRiders(level, current);
    // Belts and currents run on separate clocks. Keep each surface's riders
    // together so a following platform can enter the cell its leader vacates.
    for (const bool stream : { false, true }) {
        auto actors = conveyorActors;
        std::erase_if(actors, [&](EntityId id) {
            return (std::ranges::find(streamActors, id) != streamActors.end()) != stream;
        });
        if (std::optional<plans::PlannedAction> ride = plans::planConveyorRides(
                level,
                current,
                std::move(actors),
                stepRates_,
                stepDurationSeconds_ *
                    (stream ? config::waterStreamMovementDurationMultiplier : 1.0f))) {
            ride->action.playerMoveCountBefore = playerMoveCount_;
            ride->action.playerMoveCountAfter = playerMoveCount_;
            if (!actionAdmissionAllows(ride->action)) {
                return StartOutcome::Impossible;
            }
            return beginAction(
                       level,
                       ride->action,
                       ride->pressurePlateActivityChanged,
                       std::move(ride->legs),
                       std::move(ride->turretShots),
                       0,
                       {},
                       std::move(ride->portalTransits))
                ? StartOutcome::Started
                : StartOutcome::Refused;
        }
    }

    auto robotActors = withoutEntitiesInFlight(plans::patrollingRobots(current));
    std::erase_if(robotActors, [&](EntityId id) {
        return robotHasRiderInFlight(id) || std::ranges::any_of(robotPatrolPauses_, [id](const auto& pause) {
            return pause.robot == id && pause.remainingSeconds > 0.0f;
        });
    });
    if (std::optional<plans::PlannedAction> patrol = plans::planRobotPatrols(
            level,
            current,
            robotActors,
            stepRates_,
            config::robotPatrolSecondsPerTile)) {
        patrol->action.playerMoveCountBefore = playerMoveCount_;
        patrol->action.playerMoveCountAfter = playerMoveCount_;
        if (!actionAdmissionAllows(patrol->action)) {
            return StartOutcome::Impossible;
        }
        return beginAction(
                   level,
                   patrol->action,
                   patrol->pressurePlateActivityChanged,
                   std::move(patrol->legs),
                   std::move(patrol->turretShots),
                   0,
                   {},
                   std::move(patrol->portalTransits),
                   std::move(robotActors))
            ? StartOutcome::Started
            : StartOutcome::Refused;
    }

    return StartOutcome::Impossible;
}

GameplaySession::StartOutcome GameplaySession::tryStartActivationAction(
    const Level& level)
{
    const PlanningStamp stamp = planningStamp(level);
    if (failedActivationAttempt_.valid && failedActivationAttempt_.stamp == stamp) {
        return failedActivationAttempt_.outcome;
    }
    const StartOutcome outcome = planActivationAction(level);
    failedActivationAttempt_ = { stamp, invalidEntityId, outcome,
        outcome != StartOutcome::Started };
    return outcome;
}

GameplaySession::StartOutcome GameplaySession::planActivationAction(const Level& level)
{
    const GameState& current = planningState();
    std::optional<rules::MirrorActivationPreview> activation =
        rules::previewActivation(level, current);
    if (!activation) {
        return StartOutcome::Impossible;
    }

    lastMirrorSwapDestinations_.clear();
    lastMirrorSwapDestinations_.reserve(activation->entities.size());
    for (const rules::MirrorEntityPreview& entity : activation->entities) {
        lastMirrorSwapDestinations_.push_back(entity.destination);
    }
    Action action = plans::fromMirrorPreview(current, *activation);
    action.playerMoveCountBefore = playerMoveCount_;
    action.playerMoveCountAfter = playerMoveCount_;
    if (!actionAdmissionAllows(action)) {
        lastMirrorSwapDestinations_.clear();
        return StartOutcome::Impossible;
    }
    if (!beginAction(level, action, activation->pressurePlateActivityChanged)) {
        lastMirrorSwapDestinations_.clear();
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = false;
    if (!activation->entities.empty()) {
        ++mirrorActivationSequence_;
    }
    inputLog_.push_back(PlayerInput::Interact);
    return StartOutcome::Started;
}

GameplaySession::StartOutcome GameplaySession::tryStartUndoMove(const Level& level)
{
    if (undoHistory_.empty()) {
        return StartOutcome::Impossible;
    }
    // Undo is only permitted when nothing is in flight. That keeps the undo
    // stack a linear sequence of invertible whole-world transitions, rather
    // than the DAG that overlapping actions would make of it.
    //
    // Refused rather than impossible: the history is there and the player is
    // entitled to it, just not this instant. A queued undo waits for the world
    // to go quiet, and goes stale if it never does.
    if (moving()) {
        return StartOutcome::Refused;
    }

    Action action = plans::inverted(undoHistory_.back());
    action.durationSeconds = action.presentation.empty()
        ? stepDurationSeconds_
        : action.presentation.durationSeconds;
    action.facingDirection = plans::firstPlayerMovementDirection(
        action.after, action.before);
    if (!actionAdmissionAllows(action)) {
        return StartOutcome::Impossible;
    }
    if (!beginAction(level, action, rules::pressurePlateActivityChanged(
            level, action.before, action.after))) {
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = true;
    robotPatrolPauses_.clear();
    robotPatrolActions_.clear();
    inputLog_.push_back(PlayerInput::Undo);
    return StartOutcome::Started;
}

GameplaySession::StartOutcome GameplaySession::tryStartRestart(
    const Level& level)
{
    // Same reasoning as undo: a restart rewrites the whole world, so it cannot
    // share it with an action that planned against the old one.
    if (moving()) {
        return StartOutcome::Refused;
    }

    std::optional<Action> action =
        plans::restart(level, state(), stepDurationSeconds_);
    if (!action) {
        return StartOutcome::Impossible;
    }

    action->playerMoveCountBefore = playerMoveCount_;
    action->playerMoveCountAfter = 0;
    if (!actionAdmissionAllows(*action)) {
        return StartOutcome::Impossible;
    }
    if (!beginAction(level, *action, rules::pressurePlateActivityChanged(
            level, action->before, action->after))) {
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = false;
    // A restart begins a fresh attempt; the log describes that attempt only.
    robotPatrolPauses_.clear();
    robotPatrolActions_.clear();
    inputLog_.clear();
    return StartOutcome::Started;
}

GameplaySession::StartOutcome GameplaySession::tryStartHeldDirection(
    const Level& level,
    MoveDirection direction,
    std::optional<MoveDirection> queuedDirection,
    EntityId controller)
{
    const StartOutcome outcome = tryStartPlayerStep(
        level, direction, controller);
    if (outcome != StartOutcome::Started) {
        return outcome;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection, controller)) {
        enqueue({
            .type = CommandType::Move,
            .direction = *queuedDirection,
            .controller = controller,
        });
    }

    return StartOutcome::Started;
}

bool GameplaySession::isStale(const Command& command) const
{
    // The scheduler's clock runs only while something is in flight, so a
    // command entered into an idle world never ages out from under the player.
    return scheduler_.clockSeconds() - command.queuedAtSeconds >
        commandStalenessSeconds;
}

bool GameplaySession::hasPendingMove(
    MoveDirection direction, EntityId controller) const
{
    return std::ranges::any_of(pendingCommands_, [=](const Command& command) {
        return command.type == CommandType::Move &&
            command.direction == direction && command.controller == controller;
    });
}

void GameplaySession::setActiveActionPresentation(
    ActionPresentationTimeline presentation)
{
    if (const ActionScheduler::InFlight* action = scheduler_.oldest()) {
        setActionPresentation(action->id, std::move(presentation));
    }
}

void GameplaySession::setActiveActionDuration(float durationSeconds)
{
    if (const ActionScheduler::InFlight* action = scheduler_.oldest()) {
        setActionDuration(action->id, durationSeconds);
    }
}

void GameplaySession::setActionPresentation(
    std::size_t actionId, ActionPresentationTimeline presentation)
{
    ActionScheduler::InFlight* action = scheduler_.find(actionId);
    if (action == nullptr) {
        return;
    }
    action->plan.presentation = std::move(presentation);
    // Initial/rest animation tracks can make a timeline structurally non-empty
    // without giving it any playback time. Keep the mechanic's planned duration
    // in that case; otherwise state-only actions (such as a turret volley) would
    // commit immediately and their delayed effects would outlive the action.
    if (action->plan.presentation.durationSeconds > 0.0f) {
        action->plan.durationSeconds =
            action->plan.presentation.durationSeconds;
        action->elapsedSeconds =
            std::min(action->elapsedSeconds, action->plan.durationSeconds);
    }
}

void GameplaySession::setActionDuration(
    std::size_t actionId, float durationSeconds)
{
    ActionScheduler::InFlight* action = scheduler_.find(actionId);
    if (action == nullptr) {
        return;
    }
    action->plan.durationSeconds = std::max(durationSeconds, 0.0f);
    action->elapsedSeconds =
        std::min(action->elapsedSeconds, action->plan.durationSeconds);
}

bool GameplaySession::beginAction(
    const Level& level,
    const Action& action,
    bool pressurePlateActivityChanged,
    std::vector<GameState> legs,
    std::vector<plans::TurretShotCue> turretShots,
    std::size_t causalGroup,
    ActionDeferral deferral,
    std::vector<plans::PlannedAction::PortalCue> portalTransits,
    std::vector<EntityId> robotPatrolActors)
{
    if (actionUsesMovingPlatformSupport(action)) return false;
    ActionScheduler::Pending pending = makePending(level, action,
        pressurePlateActivityChanged, legs, turretShots, deferral, portalTransits);
    const auto admission = scheduler_.tryStart(
        action,
        pending.reservations,
        std::move(legs),
        causalGroup,
        deferral,
        std::move(turretShots),
        std::move(portalTransits),
        std::move(pending.legDurationsSeconds));
    const auto* started = std::get_if<ActionScheduler::Started>(&admission);
    if (!started) return false;
    if (!robotPatrolActors.empty()) {
        const auto changed = StateDelta::between(action.before, action.after).changedEntityIds();
        std::erase_if(robotPatrolActors, [&](EntityId id) {
            return std::ranges::find(changed, id) == changed.end();
        });
        robotPatrolActions_.push_back({ started->id, std::move(robotPatrolActors) });
    }
    return true;
}

bool GameplaySession::actionAdmissionAllows(const Action& action) const
{
    if (!actionAdmissionPolicy_) {
        return true;
    }

    GameState projected = projectedState();
    StateDelta::between(action.before, action.after).applyTo(projected);
    return actionAdmissionPolicy_(projected);
}

bool GameplaySession::actionAdmissionAllows(
    const std::vector<ActionScheduler::Pending>& actions) const
{
    if (!actionAdmissionPolicy_) {
        return true;
    }

    GameState projected = projectedState();
    for (const ActionScheduler::Pending& pending : actions) {
        StateDelta::between(pending.plan.before, pending.plan.after)
            .applyTo(projected);
    }
    return actionAdmissionPolicy_(projected);
}

ActionScheduler::Pending GameplaySession::makePending(
    const Level& level,
    const Action& action,
    bool pressurePlateActivityChanged,
    const std::vector<GameState>& legs,
    const std::vector<plans::TurretShotCue>& turretShots,
    ActionDeferral deferral,
    const std::vector<plans::PlannedAction::PortalCue>& portalTransits) const
{
    // Two different needs, deliberately not conflated.
    //
    // Movement claims are derived from the cells an action's entities pass
    // through, so they need at least one leg or the action claims no cells.
    //
    // The presentation reads legs to animate a chain tile by tile, and a
    // single-step action must carry none - handing it a synthetic leg sends it
    // down the chain-aware path, which pairs entities positionally between the
    // legs and cannot cope with an action that adds a player. Mirror activation
    // does exactly that.
    //
    // So the claim is computed from a local copy and the stored legs are left
    // as the caller meant them.
    const plans::PlannedAction claimed {
        .action = action,
        .legs = legs.empty() ? std::vector<GameState> { action.after } : legs,
        .portalTransits = portalTransits,
        .pressurePlateActivityChanged = pressurePlateActivityChanged,
    };
    ActionReservations reservations = plans::reservationsFor(claimed);
    const GameState* previous = &action.before;
    bool waterMomentum = false;
    for (const GameState& leg : claimed.legs) {
        // A slower companion may determine this leg's shared clock while a
        // free stone still animates ahead at skipping speed. Inspect stones
        // directly rather than relying on the shared duration weight.
        const std::size_t count = std::min(previous->movables.size(), leg.movables.size());
        for (std::size_t index = 0; index < count; ++index) {
            const auto& from = previous->movables[index];
            const auto& to = leg.movables[index];
            if (rules::lilyPadDrivenByRock(level, *previous, leg, index)) {
                waterMomentum = true;
            }
            if (from.type != TileType::RoundRock || from.fallen || from.dead ||
                (!from.sliding && !to.sliding)) continue;
            waterMomentum = waterMomentum ||
                tileTypeIsWater(level.supportingTileAt(from.cell).value_or(TileType::Air)) ||
                tileTypeIsWater(level.supportingTileAt(to.cell).value_or(TileType::Air));
        }
        previous = &leg;
    }
    if (waterMomentum) {
        // Reservation steps use the normal world clock. Skipping stones can
        // arrive before a claim begins; coasting leaves can leave after it
        // expires. Keep their paths reserved until the whole action commits.
        for (Reservation& reservation : reservations.cells) {
            reservation.firstStep = 0;
            reservation.lastStep.reset();
        }
    }
    return ActionScheduler::Pending {
        .plan = action,
        .reservations = std::move(reservations),
        .legs = legs,
        .legDurationsSeconds = waterMomentum
            ? plans::legDurations(&level, action.before, legs, action.durationSeconds)
            : std::vector<float> {},
        .turretShots = turretShots,
        .portalTransits = portalTransits,
        .deferral = deferral,
    };
}

} // namespace sokoban
