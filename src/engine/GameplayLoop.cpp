#include "engine\GameplayLoop.hpp"

#include "engine\Rules.hpp"

#include <algorithm>

namespace sokoban {
namespace {

std::optional<MoveDirection> pressedAxis(
    const GameplayLoop::ButtonState& negative,
    const GameplayLoop::ButtonState& positive,
    MoveDirection negativeDirection,
    MoveDirection positiveDirection)
{
    if (negative.pressed == positive.pressed) {
        return std::nullopt;
    }
    if ((negative.pressed && positive.down) ||
        (positive.pressed && negative.down)) {
        return std::nullopt;
    }
    return negative.pressed ? negativeDirection : positiveDirection;
}

std::optional<MoveDirection> heldAxis(
    const GameplayLoop::ButtonState& negative,
    const GameplayLoop::ButtonState& positive,
    MoveDirection negativeDirection,
    MoveDirection positiveDirection)
{
    if (negative.down == positive.down) {
        return std::nullopt;
    }
    return negative.down ? negativeDirection : positiveDirection;
}

} // namespace

GameplayLoop::UpdateResult GameplayLoop::update(
    const Level& level,
    GameplaySession& session,
    GameplayPresentation& presentation,
    const InputFrame& input,
    float dt,
    bool playingDraft)
{
    if (input.undoPressed) {
        session.queueUndo();
    }
    if (input.restartPressed) {
        session.queueRestart();
    }
    if (const std::optional<MoveDirection> vertical =
            pressedVertical(input)) {
        session.queueMove(*vertical);
    }
    if (const std::optional<MoveDirection> horizontal =
            pressedHorizontal(input)) {
        session.queueMove(*horizontal);
    }

    presentation.advanceAnimations(dt);
    float remainingTime = dt;
    UpdateResult result;
    while (remainingTime > 0.0f) {
        if (!session.moving()) {
            const GameplaySession::Controls controls {
                .undoHeld = input.undoDown,
                .verticalMove = heldVertical(input),
                .horizontalMove = heldHorizontal(input),
            };
            if (!session.tryStartNextAction(level, controls)) {
                return result;
            }
            presentation.beginAction(session.activeAction());
        }

        const float duration = session.activeActionDuration();
        if (duration > 0.0f) {
            const float step = std::min(
                remainingTime,
                session.activeActionRemainingSeconds());
            remainingTime -= step;
            session.advanceActiveAction(step);
            if (!session.activeActionComplete()) {
                continue;
            }
        }

        session.completeActiveAction();
        presentation.finishAction(session.state());
        if (rules::isAtUnlockedEnd(level, session.state())) {
            if (playingDraft) {
                result.draftSolved = true;
            } else {
                result.screenSolved = true;
                return result;
            }
        } else {
            result.stateCommitted = true;
        }
    }
    return result;
}

std::optional<MoveDirection> GameplayLoop::pressedVertical(
    const InputFrame& input)
{
    return pressedAxis(
        input.up, input.down, MoveDirection::Up, MoveDirection::Down);
}

std::optional<MoveDirection> GameplayLoop::pressedHorizontal(
    const InputFrame& input)
{
    return pressedAxis(
        input.left,
        input.right,
        MoveDirection::Left,
        MoveDirection::Right);
}

std::optional<MoveDirection> GameplayLoop::heldVertical(
    const InputFrame& input)
{
    return heldAxis(
        input.up, input.down, MoveDirection::Up, MoveDirection::Down);
}

std::optional<MoveDirection> GameplayLoop::heldHorizontal(
    const InputFrame& input)
{
    return heldAxis(
        input.left,
        input.right,
        MoveDirection::Left,
        MoveDirection::Right);
}

} // namespace sokoban
