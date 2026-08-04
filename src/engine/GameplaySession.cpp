#include "engine/GameplaySession.hpp"

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

    constexpr std::array<std::optional<MoveDirection>, 5> inputs {
        std::nullopt,
        MoveDirection::Up,
        MoveDirection::Down,
        MoveDirection::Left,
        MoveDirection::Right,
    };
    return std::ranges::any_of(inputs, [&](std::optional<MoveDirection> input) {
        const GameState first = rules::step(level, action.before, input, rates);
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
            GameState next = rules::step(level, current, std::nullopt, rates);
            if (next == current) {
                break;
            }
            current = std::move(next);
        }
        return current == action.after;
    });
}

} // namespace

void GameplaySession::reset(const Level& level)
{
    scheduler_.reset(rules::initialState(level), stepDurationSeconds_);
    pendingCommands_.clear();
    undoHistory_.clear();
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
    // commits by how far each overshot. A sampling time never should.
    return std::min(
        action->elapsedSeconds, std::max(action->plan.durationSeconds, 0.0f));
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
            return action.plan.durationSeconds <= 0.0f ||
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
    undoHistory_ = snapshot.undoStack;
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

bool GameplaySession::tryStartNextAction(const Level& level, const Controls& controls)
{
    // One at a time, still. The scheduler and its reservations are wired in and
    // exercised on every action, but nothing yet admits a second one - that is
    // the step where behaviour actually changes, and the presentation is the
    // part that has to be ready for it.
    if (moving()) {
        return false;
    }

    if (rules::anyPlayerDead(state())) {
        while (!pendingCommands_.empty()) {
            const Command command = pendingCommands_.front();
            pendingCommands_.pop_front();
            if (isStale(command)) {
                continue;
            }
            if (command.type == CommandType::Undo && tryStartUndoMove()) {
                return true;
            }
        }

        return controls.undoHeld && tryStartUndoMove();
    }

    while (!pendingCommands_.empty()) {
        const Command command = pendingCommands_.front();
        pendingCommands_.pop_front();
        if (isStale(command)) {
            continue;
        }

        if (command.type == CommandType::Undo && tryStartUndoMove()) {
            return true;
        }

        if (command.type == CommandType::Restart && tryStartRestart(level)) {
            return true;
        }

        if (command.type == CommandType::Mirror && tryStartMirrorAction(level)) {
            return true;
        }

        const std::optional<MoveDirection> perpendicular =
            command.direction == MoveDirection::Up || command.direction == MoveDirection::Down
            ? controls.horizontalMove
            : controls.verticalMove;
        if (command.type == CommandType::Move &&
            tryStartHeldDirection(level, command.direction, perpendicular)) {
            return true;
        }
    }

    if (tryStartHeldMove(level, controls)) {
        return true;
    }

    return !autoMotionPaused_ &&
        rules::hasPendingMotion(level, state()) &&
        tryStartWorldStep(level, std::nullopt);
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
        recordCompletion(finished.plan);
    }
}

void GameplaySession::recordCompletion(const Action& action)
{
    moveHistory_.push_back(action);
    playerMoveCount_ = action.playerMoveCountAfter;
    if (action.reversed) {
        if (!undoHistory_.empty()) {
            undoHistory_.pop_back();
        }
    } else {
        undoHistory_.push_back(action);
    }
}

float GameplaySession::activeActionRemainingSeconds() const
{
    return std::max(
        activeActionDuration() - activeActionElapsedSeconds(), 0.0f);
}

bool GameplaySession::activeActionComplete() const
{
    const ActionScheduler::InFlight* action = scheduler_.oldest();
    return action != nullptr &&
        (action->plan.durationSeconds <= 0.0f ||
            action->elapsedSeconds >= action->plan.durationSeconds);
}

bool GameplaySession::tryStartHeldMove(const Level& level, const Controls& controls)
{
    if (controls.undoHeld) {
        return tryStartUndoMove();
    }

    if (controls.verticalMove && tryStartHeldDirection(level, *controls.verticalMove, controls.horizontalMove)) {
        return true;
    }
    if (controls.horizontalMove && tryStartHeldDirection(level, *controls.horizontalMove, controls.verticalMove)) {
        return true;
    }

    return false;
}

bool GameplaySession::tryStartWorldStep(const Level& level, std::optional<MoveDirection> playerInput)
{
    std::optional<plans::PlannedAction> planned = plans::worldStep(
        level, state(), playerInput, stepRates_, stepDurationSeconds_);
    if (!planned) {
        return false;
    }

    // The plan settles the outcome; the session only knows the running move
    // total, so it fills that in. The count is judged on the first leg, since
    // that is the only one the player drove - a long slide is still one move.
    const bool countsAsPlayerMove = playerInput.has_value() &&
        plans::anyPlayerMoved(planned->action.before, planned->legs.front());
    planned->action.playerMoveCountBefore = playerMoveCount_;
    planned->action.playerMoveCountAfter =
        playerMoveCount_ + (countsAsPlayerMove ? 1 : 0);

    const bool wasPaused = autoMotionPaused_;
    if (playerInput) {
        autoMotionPaused_ = false;
    }
    if (!beginAction(std::move(planned->action), std::move(planned->legs))) {
        autoMotionPaused_ = wasPaused;
        return false;
    }
    return true;
}

bool GameplaySession::tryStartMirrorAction(const Level& level)
{
    std::optional<rules::MirrorActivationPreview> activation =
        rules::previewMirrorActivation(level, state());
    if (!activation) {
        return false;
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
        return false;
    }
    autoMotionPaused_ = false;
    ++mirrorActivationSequence_;
    return true;
}

bool GameplaySession::tryStartUndoMove()
{
    // Undo is only permitted when nothing is in flight. That keeps the undo
    // stack a linear sequence of invertible whole-world transitions, rather
    // than the DAG that overlapping actions would make of it.
    if (undoHistory_.empty() || moving()) {
        return false;
    }

    Action action = plans::inverted(undoHistory_.back());
    action.durationSeconds = action.presentation.empty()
        ? stepDurationSeconds_
        : action.presentation.durationSeconds;
    action.facingDirection = plans::firstPlayerMovementDirection(
        action.after, action.before);
    if (!beginAction(std::move(action))) {
        return false;
    }
    autoMotionPaused_ = true;
    return true;
}

bool GameplaySession::tryStartRestart(const Level& level)
{
    // Same reasoning as undo: a restart rewrites the whole world, so it cannot
    // share it with an action that planned against the old one.
    if (moving()) {
        return false;
    }

    std::optional<Action> action =
        plans::restart(level, state(), stepDurationSeconds_);
    if (!action) {
        return false;
    }

    action->playerMoveCountBefore = playerMoveCount_;
    action->playerMoveCountAfter = 0;
    if (!beginAction(std::move(*action))) {
        return false;
    }
    autoMotionPaused_ = false;
    return true;
}

bool GameplaySession::tryStartHeldDirection(
    const Level& level,
    MoveDirection direction,
    std::optional<MoveDirection> queuedDirection)
{
    if (!tryStartWorldStep(level, direction)) {
        return false;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection)) {
        queueMove(*queuedDirection);
    }

    return true;
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

bool GameplaySession::beginAction(Action action, std::vector<GameState> legs)
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
    const ActionReservations claims = plans::reservationsFor(claimed);
    return std::holds_alternative<ActionScheduler::Started>(
        scheduler_.tryStart(action, claims, std::move(legs)));
}

} // namespace sokoban
