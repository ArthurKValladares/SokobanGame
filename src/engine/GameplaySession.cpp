#include "engine/GameplaySession.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace sokoban {
namespace {

std::optional<MoveDirection> movementDirection(GridPosition3 from, GridPosition3 to)
{
    const int deltaX = to.x - from.x;
    const int deltaY = to.y - from.y;
    if (deltaX < 0) {
        return MoveDirection::Left;
    }
    if (deltaX > 0) {
        return MoveDirection::Right;
    }
    if (deltaY < 0) {
        return MoveDirection::Up;
    }
    if (deltaY > 0) {
        return MoveDirection::Down;
    }
    return std::nullopt;
}

bool matchesForwardTransition(
    const Level& level,
    const GameplaySession::Action& action,
    const rules::StepRates& rates)
{
    const GameState initial = rules::initialState(level);
    if (!action.before.playerDead && !(action.before == initial) &&
        action.after == initial && action.playerMoveCountAfter == 0) {
        return true;
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
            (input && !(action.before.player == after.player) ? 1 : 0);
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
    moving_ = false;
    autoMotionPaused_ = snapshot.automaticMotionPaused;
    return true;
}

void GameplaySession::queueMove(MoveDirection direction)
{
    pendingCommands_.push_back({ .type = CommandType::Move, .direction = direction });
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

    if (state_.playerDead) {
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

    state_ = activeAction_.after;
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
    GameState after = rules::step(level, state_, playerInput, stepRates_);
    if (after == state_) {
        return false;
    }
    const bool countsAsPlayerMove = playerInput.has_value() &&
        !(state_.player == after.player);

    Action action {
        .before = state_,
        .after = std::move(after),
        .durationSeconds = stepDurationSeconds_,
        .playerMoveCountBefore = playerMoveCount_,
        .playerMoveCountAfter = playerMoveCount_ + (countsAsPlayerMove ? 1 : 0),
        .facingDirection = playerInput,
    };

    if (playerInput && !(action.before.player == action.after.player)) {
        const GridPosition3 pushCell = rules::movementTarget(action.before.player, *playerInput);
        for (std::size_t i = 0; i < action.before.movables.size() && i < action.after.movables.size(); ++i) {
            if (action.before.movables[i].cell == pushCell &&
                !(action.after.movables[i].cell == pushCell)) {
                action.playerPushing = true;
                break;
            }
        }
    }

    if (playerInput) {
        autoMotionPaused_ = false;
    } else {
        action.facingDirection = movementDirection(action.before.player, action.after.player);
    }
    beginAction(std::move(action));
    return true;
}

bool GameplaySession::tryStartUndoMove()
{
    if (undoHistory_.empty()) {
        return false;
    }

    Action action = invertAction(undoHistory_.back());
    action.durationSeconds = stepDurationSeconds_;
    action.facingDirection = movementDirection(action.after.player, action.before.player);
    autoMotionPaused_ = true;
    beginAction(std::move(action));
    return true;
}

bool GameplaySession::tryStartRestart(const Level& level)
{
    if (state_.playerDead) {
        return false;
    }

    GameState restarted = rules::initialState(level);
    if (state_ == restarted) {
        return false;
    }

    autoMotionPaused_ = false;
    beginAction({
        .before = state_,
        .after = std::move(restarted),
        .durationSeconds = stepDurationSeconds_,
        .playerMoveCountBefore = playerMoveCount_,
        .playerMoveCountAfter = 0,
    });
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

GameplaySession::Action GameplaySession::invertAction(const Action& action) const
{
    return {
        .before = action.after,
        .after = action.before,
        .playerPushing = action.playerPushing,
        .reversed = true,
        .playerMoveCountBefore = action.playerMoveCountAfter,
        .playerMoveCountAfter = action.playerMoveCountBefore,
    };
}

void GameplaySession::beginAction(Action action)
{
    activeAction_ = std::move(action);
    moveElapsed_ = 0.0f;
    moving_ = true;
}

} // namespace sokoban
