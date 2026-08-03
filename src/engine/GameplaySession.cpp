#include "engine/GameplaySession.hpp"

#include "engine/StateDelta.hpp"

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
        const GameState after = rules::step(level, action.before, input, rates);
        const int expectedMoveCount = action.playerMoveCountBefore +
            (input && plans::anyPlayerMoved(action.before, after) ? 1 : 0);
        return !(after == action.before) &&
            after == action.after &&
            action.playerMoveCountAfter == expectedMoveCount;
    });
}

} // namespace

void GameplaySession::reset(const Level& level)
{
    state_ = rules::initialState(level);
    pendingCommands_.clear();
    undoHistory_.clear();
    moveHistory_.clear();
    activeAction_ = {};
    moveElapsed_ = 0.0f;
    playerMoveCount_ = 0;
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
    moving_ = false;
    autoMotionPaused_ = false;
}

GameplaySession::Snapshot GameplaySession::snapshot() const
{
    Snapshot result {
        .state = state_,
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

    state_ = snapshot.state;
    pendingCommands_.clear();
    undoHistory_ = snapshot.undoStack;
    moveHistory_.clear();
    activeAction_ = {};
    moveElapsed_ = 0.0f;
    playerMoveCount_ = snapshot.playerMoveCount;
    mirrorActivationSequence_ = 0;
    lastMirrorSwapDestinations_.clear();
    moving_ = false;
    autoMotionPaused_ = snapshot.automaticMotionPaused;
    return true;
}

void GameplaySession::queueMove(MoveDirection direction)
{
    pendingCommands_.push_back({ .type = CommandType::Move, .direction = direction });
}

void GameplaySession::queueMirror()
{
    pendingCommands_.push_back({ .type = CommandType::Mirror });
}

void GameplaySession::queueUndo()
{
    pendingCommands_.push_back({ .type = CommandType::Undo });
}

void GameplaySession::queueRestart()
{
    pendingCommands_.push_back({ .type = CommandType::Restart });
}

bool GameplaySession::tryStartNextAction(const Level& level, const Controls& controls)
{
    if (moving_) {
        return false;
    }

    if (rules::anyPlayerDead(state_)) {
        while (!pendingCommands_.empty()) {
            const Command command = pendingCommands_.front();
            pendingCommands_.pop_front();
            if (command.type == CommandType::Undo && tryStartUndoMove()) {
                return true;
            }
        }

        return controls.undoHeld && tryStartUndoMove();
    }

    while (!pendingCommands_.empty()) {
        const Command command = pendingCommands_.front();
        pendingCommands_.pop_front();

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
        rules::hasPendingMotion(level, state_) &&
        tryStartWorldStep(level, std::nullopt);
}

void GameplaySession::advanceActiveAction(float dt)
{
    if (!moving_) {
        return;
    }

    moveElapsed_ = std::min(
        moveElapsed_ + std::max(dt, 0.0f),
        std::max(activeAction_.durationSeconds, 0.0f));
}

void GameplaySession::completeActiveAction()
{
    if (!moving_) {
        return;
    }

    // Only what this action changed, rather than assigning its `after`
    // wholesale. With one action in flight the two are identical; once actions
    // can overlap, assigning a whole state would erase whatever the other
    // in-flight actions had already committed, because `after` is a snapshot of
    // the world as it stood when this action started. Undo goes through the
    // same path: an inverted action's endpoints are swapped, so its delta is
    // the inverse delta.
    StateDelta::between(activeAction_.before, activeAction_.after)
        .applyTo(state_);
    moveHistory_.push_back(activeAction_);
    playerMoveCount_ = activeAction_.playerMoveCountAfter;
    if (activeAction_.reversed) {
        if (!undoHistory_.empty()) {
            undoHistory_.pop_back();
        }
    } else {
        undoHistory_.push_back(activeAction_);
    }
    moving_ = false;
    moveElapsed_ = 0.0f;
}

float GameplaySession::activeActionRemainingSeconds() const
{
    return std::max(activeAction_.durationSeconds - moveElapsed_, 0.0f);
}

bool GameplaySession::activeActionComplete() const
{
    return moving_ &&
        (activeAction_.durationSeconds <= 0.0f || moveElapsed_ >= activeAction_.durationSeconds);
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
    std::optional<Action> action = plans::worldStep(
        level, state_, playerInput, stepRates_, stepDurationSeconds_);
    if (!action) {
        return false;
    }

    // The plan settles the outcome; the session only knows the running move
    // total, so it fills that in.
    const bool countsAsPlayerMove = playerInput.has_value() &&
        plans::anyPlayerMoved(action->before, action->after);
    action->playerMoveCountBefore = playerMoveCount_;
    action->playerMoveCountAfter =
        playerMoveCount_ + (countsAsPlayerMove ? 1 : 0);

    if (playerInput) {
        autoMotionPaused_ = false;
    }
    beginAction(std::move(*action));
    return true;
}

bool GameplaySession::tryStartMirrorAction(const Level& level)
{
    std::optional<rules::MirrorActivationPreview> activation =
        rules::previewMirrorActivation(level, state_);
    if (!activation) {
        return false;
    }

    autoMotionPaused_ = false;
    lastMirrorSwapDestinations_.clear();
    lastMirrorSwapDestinations_.reserve(activation->entities.size());
    for (const rules::MirrorEntityPreview& entity : activation->entities) {
        lastMirrorSwapDestinations_.push_back(entity.destination);
    }
    ++mirrorActivationSequence_;
    Action action = plans::fromMirrorPreview(state_, *activation);
    action.playerMoveCountBefore = playerMoveCount_;
    action.playerMoveCountAfter = playerMoveCount_;
    beginAction(std::move(action));
    return true;
}

bool GameplaySession::tryStartUndoMove()
{
    if (undoHistory_.empty()) {
        return false;
    }

    Action action = plans::inverted(undoHistory_.back());
    action.durationSeconds = action.presentation.empty()
        ? stepDurationSeconds_
        : action.presentation.durationSeconds;
    action.facingDirection = plans::firstPlayerMovementDirection(
        action.after, action.before);
    autoMotionPaused_ = true;
    beginAction(std::move(action));
    return true;
}

bool GameplaySession::tryStartRestart(const Level& level)
{
    std::optional<Action> action =
        plans::restart(level, state_, stepDurationSeconds_);
    if (!action) {
        return false;
    }

    action->playerMoveCountBefore = playerMoveCount_;
    action->playerMoveCountAfter = 0;
    autoMotionPaused_ = false;
    beginAction(std::move(*action));
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

bool GameplaySession::hasPendingMove(MoveDirection direction) const
{
    return std::ranges::any_of(pendingCommands_, [direction](const Command& command) {
        return command.type == CommandType::Move && command.direction == direction;
    });
}

void GameplaySession::setActiveActionPresentation(
    ActionPresentationTimeline presentation)
{
    if (!moving_) {
        return;
    }
    activeAction_.presentation = std::move(presentation);
    if (!activeAction_.presentation.empty()) {
        activeAction_.durationSeconds =
            activeAction_.presentation.durationSeconds;
        moveElapsed_ = std::min(moveElapsed_, activeAction_.durationSeconds);
    }
}

void GameplaySession::setActiveActionDuration(float durationSeconds)
{
    if (!moving_) {
        return;
    }
    activeAction_.durationSeconds = std::max(durationSeconds, 0.0f);
    moveElapsed_ = std::min(moveElapsed_, activeAction_.durationSeconds);
}

void GameplaySession::beginAction(Action action)
{
    activeAction_ = std::move(action);
    moveElapsed_ = 0.0f;
    moving_ = true;
}

} // namespace sokoban
