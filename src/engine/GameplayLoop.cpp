#include "engine/GameplayLoop.hpp"

#include "engine/Rules.hpp"

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

// Gives an action that has just started its presentation timeline, and puts its
// entities at their starting positions.
//
// Keyed by id rather than acting on "the" active action: several actions can be
// started in one pass, and the newest is not the one the single-action setters
// target.
void startPresentation(
    GameplaySession& session,
    GameplayPresentation& presentation,
    std::size_t actionId)
{
    const ActionScheduler::InFlight* started = session.findInFlight(actionId);
    if (started == nullptr) {
        return;
    }

    if (started->plan.presentation.empty()) {
        GameplaySession::Action source = started->plan;
        if (source.reversed) {
            std::swap(source.before, source.after);
            source.reversed = false;
            source.durationSeconds = session.stepDurationSeconds();
        }
        session.setActionPresentation(
            actionId,
            presentation.buildActionPresentation(source, started->legs));
        // Re-fetched after every mutation: the setters write through the
        // scheduler, and holding a pointer across them invites a stale read.
        started = session.findInFlight(actionId);
    }
    if (started->plan.reversed) {
        session.setActionDuration(
            actionId, presentation.reverseDuration(started->plan));
        started = session.findInFlight(actionId);
    }
    presentation.beginAction(started->plan, session.state());
    presentation.seekAction(started->plan, 0.0f);
}

// Samples every action in flight at its own elapsed time. Each one clears and
// sets only the entities its own timeline names, so they compose.
void seekAllInFlight(
    const GameplaySession& session, GameplayPresentation& presentation)
{
    for (const ActionScheduler::InFlight& action : session.inFlight()) {
        presentation.seekAction(
            action.plan,
            std::min(
                action.elapsedSeconds,
                std::max(action.plan.durationSeconds, 0.0f)));
    }
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
    if (input.mirrorPressed) {
        session.queueMirror();
    }
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

    presentation.advanceAnimations(dt, session.state());
    float remainingTime = dt;
    UpdateResult result;
    std::size_t observedMirrorActivation =
        session.mirrorActivationSequence();

    // The old loop had to finish one action before it could start the next,
    // because there was only ever one. It does not any more: admit whatever can
    // be admitted, advance everything, commit whatever finished.
    //
    // What is left of the loop is catch-up, not sequencing. A frame longer than
    // an action still has to run the world forward more than once, and it must
    // stop at each completion rather than step over it - an action committed
    // late would have whatever follows it planned against a state that never
    // existed.
    while (remainingTime > 0.0f) {
        const GameplaySession::Controls controls {
            .undoHeld = input.undoDown,
            .verticalMove = heldVertical(input),
            .horizontalMove = heldHorizontal(input),
        };
        // Admits one action today; the same call admits every non-conflicting
        // action once the reservation gate opens.
        while (session.tryStartNextAction(level, controls)) {
            if (session.mirrorActivationSequence() !=
                observedMirrorActivation) {
                observedMirrorActivation = session.mirrorActivationSequence();
                result.mirrorActivated = true;
                result.mirrorSwapDestinations =
                    session.lastMirrorSwapDestinations();
            }
            startPresentation(
                session, presentation, session.inFlight().back().id);
        }
        if (!session.moving()) {
            return result;
        }

        const float step = std::min(
            remainingTime, session.timeToNextCompletion());
        remainingTime -= step;
        session.advanceActiveAction(step);
        seekAllInFlight(session, presentation);
        if (!session.anyActionComplete()) {
            continue;
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
