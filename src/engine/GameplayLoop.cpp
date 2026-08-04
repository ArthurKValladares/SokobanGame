#include "engine/GameplayLoop.hpp"

#include "engine/Rules.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

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
    // A deferred action holds its claims but has not begun, and its entities
    // are still being driven by the action that caused it. Seeking it to zero
    // would snap them to where that action is going to leave them, cutting its
    // animation short. It takes them over when its own clock reaches zero.
    if (started->elapsedSeconds >= 0.0f) {
        presentation.seekAction(started->plan, 0.0f);
    }
}

// The highest action id in flight, used as a watermark for "already has a
// timeline". Ids only ever increase, so anything above it is new.
std::size_t highestInFlightId(const GameplaySession& session)
{
    std::size_t highest = 0;
    for (const ActionScheduler::InFlight& action : session.inFlight()) {
        highest = std::max(highest, action.id);
    }
    return highest;
}

// Installs a timeline on every action admitted since the watermark.
//
// One call to `tryStartNextAction` can start more than one action - a push and
// the slide it sets off are planned together - so taking the newest and
// assuming it is the only one would leave the others without a timeline, which
// makes them instantaneous and commits them on the frame they started.
void startNewPresentations(
    GameplaySession& session,
    GameplayPresentation& presentation,
    std::size_t& watermark)
{
    std::vector<std::size_t> fresh;
    for (const ActionScheduler::InFlight& action : session.inFlight()) {
        if (action.id > watermark) {
            fresh.push_back(action.id);
        }
    }
    // Collected first: `startPresentation` writes through the session, and the
    // in-flight vector must not be iterated across a mutation.
    for (const std::size_t id : fresh) {
        watermark = std::max(watermark, id);
        startPresentation(session, presentation, id);
    }
}

// Samples every action in flight at its own elapsed time. Each one clears and
// sets only the entities its own timeline names, so they compose.
void seekAllInFlight(
    const GameplaySession& session, GameplayPresentation& presentation)
{
    for (const ActionScheduler::InFlight& action : session.inFlight()) {
        // Not started yet; see `startPresentation`.
        if (action.elapsedSeconds < 0.0f) {
            continue;
        }
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
    // Anything already running arrived with a timeline in an earlier frame.
    std::size_t presentedThrough = highestInFlightId(session);

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
        while (session.tryStartNextAction(level, controls)) {
            if (session.mirrorActivationSequence() !=
                observedMirrorActivation) {
                observedMirrorActivation = session.mirrorActivationSequence();
                result.mirrorActivated = true;
                result.mirrorSwapDestinations =
                    session.lastMirrorSwapDestinations();
            }
            startNewPresentations(session, presentation, presentedThrough);
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
