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
        return action.mechanicalDurationSeconds *
            static_cast<float>(cue.legIndex) /
            static_cast<float>(action.legs.size());
    }

    // Rule resolution observes the destination state, but presentation must
    // not reveal that reaction while the entity is still travelling there.
    return action.mechanicalDurationSeconds *
        static_cast<float>(cue.legIndex + 1) /
        static_cast<float>(action.legs.size());
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
        const GameState first =
            rules::scopedStep(level, action.before, input, rates, scope);
        if (first == action.before) {
            return false;
        }
        // A move counts once, judged on the input-driven first step, however
        // many steps the slide it started runs for.
        const int expectedMoveCount = action.playerMoveCountBefore +
            (input && plans::anyPlayerMoved(action.before, first) ? 1 : 0);
        if (action.playerMoveCountAfter != expectedMoveCount) {
            return false;
        }
        // Saves written before slides were chained hold one step per action,
        // so a single step still has to be accepted. Newer saves hold the whole
        // chain, which is what replaying the plan produces.
        if (first == action.after) {
            return true;
        }

        GameState current = first;
        while (plans::anySlideMomentum(current) &&
            !(current == action.after)) {
            GameState next =
                rules::scopedStep(level, current, std::nullopt, rates, scope);
            if (next == current) {
                break;
            }
            current = std::move(next);
        }
        return current == action.after;
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
            rules::activateMirrors(level, action.before)) {
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
    normalizeLegacyPlayers(snapshot.state, level);
    for (GameplaySession::Action& action : snapshot.undoStack) {
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
    undoBaseState_ = rules::initialState(level);
    scheduler_.reset(undoBaseState_, stepDurationSeconds_);
    pendingCommands_.clear();
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
}

void GameplaySession::setStepDurationSeconds(float durationSeconds)
{
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
    GameState projected = state();
    for (const ActionScheduler::InFlight& action : scheduler_.inFlight()) {
        StateDelta::between(action.plan.before, action.plan.after)
            .applyTo(projected);
    }
    return projected;
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
    pendingCommands_.clear();
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
    return true;
}

void GameplaySession::enqueue(Command command)
{
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

void GameplaySession::queueMirror()
{
    enqueue({ .type = CommandType::Mirror });
}

void GameplaySession::queueUndo()
{
    enqueue({ .type = CommandType::Undo });
}

void GameplaySession::queueRestart()
{
    enqueue({ .type = CommandType::Restart });
}

void GameplaySession::cycleActiveHero()
{
    std::vector<EntityId> controllers;
    for (std::size_t i = 0; i < state().players.size(); ++i) {
        const EntityId controller = rules::playerControllerId(state(), i);
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
}

GameplaySession::StartOutcome GameplaySession::runCommand(
    const Level& level, const Command& command, const Controls& controls)
{
    // Anything but undo is ignored while a player is dead, and dropped rather
    // than held: the world cannot move again until the death is taken back.
    if (rules::anyPlayerDead(state())) {
        return command.type == CommandType::Undo
            ? tryStartUndoMove()
            : StartOutcome::Impossible;
    }

    switch (command.type) {
    case CommandType::Undo:
        return tryStartUndoMove();
    case CommandType::Restart:
        return tryStartRestart(level);
    case CommandType::Mirror:
        return tryStartMirrorAction(level);
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
    // No one-at-a-time guard any more: what may run alongside what is the
    // reservation table's judgement, made per action against the cells it
    // actually needs, rather than a blanket refusal to have two.
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

        switch (runCommand(level, command, controls)) {
        case StartOutcome::Started:
            return true;
        case StartOutcome::Impossible:
            continue;
        case StartOutcome::Refused:
            // Back where it came from, ahead of anything queued behind it, and
            // stop draining - the commands behind it are the player's later
            // intentions and must not overtake it.
            pendingCommands_.push_front(command);
            return false;
        }
    }

    if (rules::anyPlayerDead(state())) {
        return controls.undoHeld &&
            tryStartUndoMove() == StartOutcome::Started;
    }

    if (tryStartHeldMove(level, controls) == StartOutcome::Started) {
        return true;
    }

    return !autoMotionPaused_ &&
        rules::hasPendingMotion(level, state()) &&
        tryStartAmbientMotion(level) == StartOutcome::Started;
}

void GameplaySession::advanceActiveAction(float dt)
{
    scheduler_.advanceClock(dt);
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
    }
}

std::vector<GameplaySession::TurretShotEvent>
GameplaySession::takeReadyTurretShots()
{
    std::vector<TurretShotEvent> ready;
    std::vector<std::size_t> actionIds;
    actionIds.reserve(scheduler_.inFlight().size());
    for (const ActionScheduler::InFlight& action : scheduler_.inFlight()) {
        actionIds.push_back(action.id);
    }

    for (const std::size_t actionId : actionIds) {
        ActionScheduler::InFlight* action = scheduler_.find(actionId);
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
            const float legEndSeconds = action->mechanicalDurationSeconds *
                static_cast<float>(cue.legIndex + 1) /
                static_cast<float>(std::max<std::size_t>(action->legs.size(), 1));
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
    if (controls.undoHeld) {
        return tryStartUndoMove();
    }

    // Only an impossible move falls through to the other axis. A refusal is
    // about timing rather than geometry, and letting a sideways step in
    // because the intended one is momentarily blocked would move the player
    // somewhere they did not ask to go.
    if (controls.verticalMove) {
        const StartOutcome outcome = tryStartHeldDirection(
            level,
            *controls.verticalMove,
            controls.horizontalMove,
            activeHeroController_);
        if (outcome != StartOutcome::Impossible) {
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
    std::optional<plans::PlannedAction> step = plans::planPlayerStep(
        level,
        state(),
        input,
        stepRates_,
        stepDurationSeconds_,
        controller);
    if (!step) {
        return StartOutcome::Impossible;
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
    batch.push_back(
        makePending(
            step->action,
            step->legs,
            step->turretShots,
            ActionDeferral {}));

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
            slide->action,
            slide->legs,
            slide->turretShots,
            ActionDeferral { .steps = stepLegs, .seconds = stepDuration }));
    }

    if (!actionAdmissionAllows(batch)) {
        return StartOutcome::Impossible;
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
    if (!witchSwapEndpoints.empty()) {
        lastWitchSwapDestinations_ = witchSwapEndpoints;
        ++witchSwapSequence_;
    }
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
    if (std::optional<plans::PlannedAction> volley = plans::planTurretVolley(
            level,
            state(),
            withoutEntitiesInFlight(
                rules::mutuallyFacingTurrets(level, state())),
            stepRates_,
            stepDurationSeconds_)) {
        volley->action.playerMoveCountBefore = playerMoveCount_;
        volley->action.playerMoveCountAfter = playerMoveCount_;
        if (!actionAdmissionAllows(volley->action)) {
            return StartOutcome::Impossible;
        }
        return beginAction(
                   volley->action,
                   std::move(volley->legs),
                   std::move(volley->turretShots))
            ? StartOutcome::Started
            : StartOutcome::Refused;
    }

    // Momentum before belts, matching the order the rules resolve intents in:
    // a slide overrides the belt under it, and an entity only becomes a rider
    // once it has stopped.
    if (std::optional<plans::PlannedAction> slide = plans::planSlides(
            level,
            state(),
            withoutEntitiesInFlight(plans::slidingEntities(state())),
            stepRates_,
            stepDurationSeconds_)) {
        slide->action.playerMoveCountBefore = playerMoveCount_;
        slide->action.playerMoveCountAfter = playerMoveCount_;
        if (!actionAdmissionAllows(slide->action)) {
            return StartOutcome::Impossible;
        }
        return beginAction(
                   slide->action,
                   std::move(slide->legs),
                   std::move(slide->turretShots))
            ? StartOutcome::Started
            : StartOutcome::Refused;
    }

    if (std::optional<plans::PlannedAction> ride = plans::planConveyorRides(
            level,
            state(),
            withoutEntitiesInFlight(plans::conveyorRiders(level, state())),
            stepRates_,
            stepDurationSeconds_)) {
        ride->action.playerMoveCountBefore = playerMoveCount_;
        ride->action.playerMoveCountAfter = playerMoveCount_;
        if (!actionAdmissionAllows(ride->action)) {
            return StartOutcome::Impossible;
        }
        return beginAction(
                   ride->action,
                   std::move(ride->legs),
                   std::move(ride->turretShots))
            ? StartOutcome::Started
            : StartOutcome::Refused;
    }

    return StartOutcome::Impossible;
}

GameplaySession::StartOutcome GameplaySession::tryStartMirrorAction(
    const Level& level)
{
    std::optional<rules::MirrorActivationPreview> activation =
        rules::previewMirrorActivation(level, state());
    if (!activation) {
        return StartOutcome::Impossible;
    }

    lastMirrorSwapDestinations_.clear();
    lastMirrorSwapDestinations_.reserve(activation->entities.size());
    for (const rules::MirrorEntityPreview& entity : activation->entities) {
        lastMirrorSwapDestinations_.push_back(entity.destination);
    }
    Action action = plans::fromMirrorPreview(state(), *activation);
    action.playerMoveCountBefore = playerMoveCount_;
    action.playerMoveCountAfter = playerMoveCount_;
    if (!actionAdmissionAllows(action)) {
        lastMirrorSwapDestinations_.clear();
        return StartOutcome::Impossible;
    }
    if (!beginAction(action)) {
        lastMirrorSwapDestinations_.clear();
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = false;
    ++mirrorActivationSequence_;
    return StartOutcome::Started;
}

GameplaySession::StartOutcome GameplaySession::tryStartUndoMove()
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
    if (!beginAction(action)) {
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = true;
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
    if (!beginAction(*action)) {
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = false;
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
    const Action& action,
    std::vector<GameState> legs,
    std::vector<plans::TurretShotCue> turretShots,
    std::size_t causalGroup,
    ActionDeferral deferral)
{
    const ActionReservations claims =
        makePending(action, legs, turretShots, deferral).reservations;
    return std::holds_alternative<ActionScheduler::Started>(
        scheduler_.tryStart(
            action,
            claims,
            std::move(legs),
            causalGroup,
            deferral,
            std::move(turretShots)));
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
    const Action& action,
    const std::vector<GameState>& legs,
    const std::vector<plans::TurretShotCue>& turretShots,
    ActionDeferral deferral) const
{
    // Two different needs, deliberately not conflated.
    //
    // Reservations are derived from the cells an action's entities pass
    // through, so they need at least one leg or the action claims nothing at
    // all and conflicts with nothing.
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
    };
    return ActionScheduler::Pending {
        .plan = action,
        .reservations = plans::reservationsFor(claimed),
        .legs = legs,
        .turretShots = turretShots,
        .deferral = deferral,
    };
}

} // namespace sokoban
