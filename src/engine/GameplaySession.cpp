#include "engine/GameplaySession.hpp"

#include <algorithm>
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

} // namespace

void GameplaySession::reset(const Level& level)
{
    state_ = rules::initialState(level);
    pendingCommands_.clear();
    moveHistory_.clear();
    undoCursor_.reset();
    activeAction_ = {};
    moveElapsed_ = 0.0f;
    moving_ = false;
    autoMotionPaused_ = false;
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

    Action action {
        .before = state_,
        .after = std::move(after),
        .durationSeconds = stepDurationSeconds_,
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
    undoCursor_.reset();
    beginAction(std::move(action));
    return true;
}

bool GameplaySession::tryStartUndoMove()
{
    if (moveHistory_.empty()) {
        return false;
    }

    if (!undoCursor_) {
        undoCursor_ = moveHistory_.size();
    }

    if (*undoCursor_ == 0) {
        return false;
    }

    --(*undoCursor_);
    Action action = invertAction(moveHistory_[*undoCursor_]);
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

    undoCursor_.reset();
    autoMotionPaused_ = false;
    beginAction({
        .before = state_,
        .after = std::move(restarted),
        .durationSeconds = stepDurationSeconds_,
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
    };
}

void GameplaySession::beginAction(Action action)
{
    activeAction_ = std::move(action);
    moveElapsed_ = 0.0f;
    moving_ = true;
}

} // namespace sokoban
