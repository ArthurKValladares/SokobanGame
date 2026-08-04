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

// The entities an entry changed, which is the scope it must have been resolved
// under.
//
// Deliberately keyed off the delta rather than positions: an entry is stored as
// a change replayed onto the running chain, so this is the same set the
// scheduler committed.
[[nodiscard]] std::vector<EntityId> changedEntities(
    const GameState& before, const GameState& after)
{
    const StateDelta delta = StateDelta::between(before, after);
    std::vector<EntityId> ids;
    ids.reserve(
        delta.players.size() + delta.movables.size() + delta.enemies.size());
    for (const StateDelta::Change<GameState::Player>& change : delta.players) {
        ids.push_back(change.id);
    }
    for (const StateDelta::Change<GameState::Movable>& change : delta.movables) {
        ids.push_back(change.id);
    }
    for (const StateDelta::Change<GameState::Enemy>& change : delta.enemies) {
        ids.push_back(change.id);
    }
    return ids;
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
    std::vector<EntityId> changed =
        changedEntities(action.before, action.after);
    if (changed.empty()) {
        // An empty scope means the whole world, which the branch above already
        // tried; an action that changed nothing is not a transition anyway.
        return false;
    }
    return replayMatches(
        level, action, rates, rules::StepScope { .actors = std::move(changed) });
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
    moveHistory_.clear();
    playerMoveCount_ = 0;
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
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

    GameState expectedState = rules::initialState(level);
    int expectedMoveCount = 0;
    for (const Action& action : snapshot.undoStack) {
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
    if (!(snapshot.state == expectedState) ||
        snapshot.playerMoveCount != expectedMoveCount) {
        return false;
    }

    scheduler_.reset(snapshot.state, stepDurationSeconds_);
    pendingCommands_.clear();
    // The stack was just validated against a replay from here, so it is exactly
    // the anchor the chain is built on.
    undoBaseState_ = rules::initialState(level);
    undoHistory_ = snapshot.undoStack;
    // Nothing is in flight after a restore, so every group is closed and no
    // later action can fold into one of these. Distinct ids say exactly that.
    undoGroups_.clear();
    undoGroups_.reserve(undoHistory_.size());
    for (std::size_t i = 0; i < undoHistory_.size(); ++i) {
        undoGroups_.push_back(nextCausalGroup_++);
    }
    moveHistory_.clear();
    playerMoveCount_ = snapshot.playerMoveCount;
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
    autoMotionPaused_ = snapshot.automaticMotionPaused;
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
    enqueue({ .type = CommandType::Move, .direction = direction });
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
    return tryStartHeldDirection(level, command.direction, perpendicular);
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
    moveHistory_.push_back(action);
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
            level, *controls.verticalMove, controls.horizontalMove);
        if (outcome != StartOutcome::Impossible) {
            return outcome;
        }
    }
    if (controls.horizontalMove) {
        return tryStartHeldDirection(
            level, *controls.horizontalMove, controls.verticalMove);
    }

    return StartOutcome::Impossible;
}

GameplaySession::StartOutcome GameplaySession::tryStartPlayerStep(
    const Level& level, MoveDirection input)
{
    std::optional<plans::PlannedAction> step = plans::planPlayerStep(
        level, state(), input, stepRates_, stepDurationSeconds_);
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

    std::vector<ActionScheduler::Pending> batch;
    batch.push_back(
        makePending(step->action, step->legs, ActionDeferral {}));

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
            ActionDeferral { .steps = stepLegs, .seconds = stepDuration }));
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
    return StartOutcome::Started;
}

std::vector<EntityId> GameplaySession::withoutEntitiesInFlight(
    std::vector<EntityId> candidates) const
{
    std::vector<EntityId> ids;
    for (const ActionScheduler::InFlight& action : scheduler_.inFlight()) {
        const StateDelta delta =
            StateDelta::between(action.plan.before, action.plan.after);
        for (const StateDelta::Change<GameState::Player>& change :
                delta.players) {
            ids.push_back(change.id);
        }
        for (const StateDelta::Change<GameState::Movable>& change :
                delta.movables) {
            ids.push_back(change.id);
        }
        for (const StateDelta::Change<GameState::Enemy>& change :
                delta.enemies) {
            ids.push_back(change.id);
        }
    }
    std::erase_if(candidates, [&ids](EntityId id) {
        return std::ranges::find(ids, id) != ids.end();
    });
    return candidates;
}

GameplaySession::StartOutcome GameplaySession::tryStartAmbientMotion(
    const Level& level)
{
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
        return beginAction(std::move(slide->action), std::move(slide->legs))
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
        return beginAction(std::move(ride->action), std::move(ride->legs))
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
    if (!beginAction(std::move(action))) {
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
    if (!beginAction(std::move(action))) {
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
    if (!beginAction(std::move(*action))) {
        return StartOutcome::Refused;
    }
    autoMotionPaused_ = false;
    return StartOutcome::Started;
}

GameplaySession::StartOutcome GameplaySession::tryStartHeldDirection(
    const Level& level,
    MoveDirection direction,
    std::optional<MoveDirection> queuedDirection)
{
    const StartOutcome outcome = tryStartPlayerStep(level, direction);
    if (outcome != StartOutcome::Started) {
        return outcome;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection)) {
        queueMove(*queuedDirection);
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

bool GameplaySession::hasPendingMove(MoveDirection direction) const
{
    return std::ranges::any_of(pendingCommands_, [direction](const Command& command) {
        return command.type == CommandType::Move && command.direction == direction;
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
    if (!action->plan.presentation.empty()) {
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
    Action action,
    std::vector<GameState> legs,
    std::size_t causalGroup,
    ActionDeferral deferral)
{
    const ActionReservations claims =
        makePending(action, legs, deferral).reservations;
    return std::holds_alternative<ActionScheduler::Started>(
        scheduler_.tryStart(
            action, claims, std::move(legs), causalGroup, deferral));
}

ActionScheduler::Pending GameplaySession::makePending(
    const Action& action,
    const std::vector<GameState>& legs,
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
        .deferral = deferral,
    };
}

} // namespace sokoban
