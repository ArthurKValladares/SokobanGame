#include "engine/ActionScheduler.hpp"

#include "engine/StateDelta.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sokoban {

void ActionScheduler::reset(GameState state, float stepDurationSeconds)
{
    state_ = std::move(state);
    inFlight_.clear();
    reservations_.clear();
    clockSeconds_ = 0.0f;
    stepDurationSeconds_ = std::max(stepDurationSeconds, 0.0001f);
    nextId_ = 1;
}

int ActionScheduler::currentStep() const
{
    return static_cast<int>(
        std::floor(clockSeconds_ / stepDurationSeconds_));
}

std::variant<ActionScheduler::Started, ActionScheduler::Rejection>
ActionScheduler::tryStart(
    const ActionPlan& plan, const ActionReservations& reservations)
{
    // Rounded down deliberately. An action starting part-way through a step
    // gets claims that begin fractionally early, which can only make the check
    // stricter - the opposite mistake would let two actions slip past each
    // other by a fraction of a step.
    const int baseStep = currentStep();
    if (const std::optional<ReservationTable::Conflict> conflict =
            reservations_.conflict(reservations, baseStep)) {
        return Rejection {
            .blockedBy = conflict->heldBy,
            .cell = conflict->cell,
            .step = conflict->step,
        };
    }

    const std::size_t id = nextId_++;
    reservations_.admit(id, reservations, baseStep);
    inFlight_.push_back({
        .id = id,
        .plan = plan,
        .elapsedSeconds = 0.0f,
        .baseStep = baseStep,
    });
    return Started { .id = id };
}

std::vector<std::size_t> ActionScheduler::advance(float deltaSeconds)
{
    const float step = std::max(deltaSeconds, 0.0f);
    clockSeconds_ += step;

    struct Finished {
        std::size_t id = 0;
        float overshoot = 0.0f;
    };
    std::vector<Finished> finished;

    for (InFlight& action : inFlight_) {
        action.elapsedSeconds += step;
        if (action.elapsedSeconds >= action.plan.durationSeconds) {
            finished.push_back({
                .id = action.id,
                // How far past its end it ran, so the one that finished
                // earliest commits first.
                .overshoot = action.elapsedSeconds - action.plan.durationSeconds,
            });
        }
    }

    // Largest overshoot finished furthest in the past. Ties break on id, which
    // is admission order, so the commit order is reproducible.
    std::ranges::sort(finished, [](const Finished& a, const Finished& b) {
        if (a.overshoot != b.overshoot) {
            return a.overshoot > b.overshoot;
        }
        return a.id < b.id;
    });

    std::vector<std::size_t> completed;
    completed.reserve(finished.size());
    for (const Finished& entry : finished) {
        const auto found = std::ranges::find_if(
            inFlight_,
            [&entry](const InFlight& action) { return action.id == entry.id; });
        if (found == inFlight_.end()) {
            continue;
        }
        // Only what this action changed. Anything else in flight has been
        // writing its own entities, and a whole-state assignment would undo it.
        StateDelta::between(found->plan.before, found->plan.after)
            .applyTo(state_);
        reservations_.release(entry.id);
        inFlight_.erase(found);
        completed.push_back(entry.id);
    }
    return completed;
}

} // namespace sokoban
