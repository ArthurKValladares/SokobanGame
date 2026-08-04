#include "engine/ActionScheduler.hpp"

#include "engine/StateDelta.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sokoban {

namespace {

// The entities a plan writes - which is to say the ones it is answerable for.
[[nodiscard]] std::vector<EntityId> writtenEntities(const ActionPlan& plan)
{
    const StateDelta delta = StateDelta::between(plan.before, plan.after);
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

} // namespace

std::optional<ActionScheduler::Rejection> ActionScheduler::ownershipConflict(
    const ActionPlan& plan, std::size_t causalGroup) const
{
    const std::vector<EntityId> wanted = writtenEntities(plan);
    for (const InFlight& action : inFlight_) {
        if (causalGroup != 0 && action.causalGroup == causalGroup) {
            continue;
        }
        for (const EntityId owned : writtenEntities(action.plan)) {
            if (std::ranges::find(wanted, owned) != wanted.end()) {
                return Rejection {
                    .blockedBy = action.id,
                    // Deliberately unset. The refusal is about an entity rather
                    // than a place, and naming a cell would suggest the cell
                    // was the problem.
                    .cell = {},
                    .step = currentStep(),
                };
            }
        }
    }
    return std::nullopt;
}

void ActionScheduler::reset(GameState state, float stepDurationSeconds)
{
    admissionStats_ = {};
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

void ActionScheduler::setStepDurationSeconds(float seconds)
{
    stepDurationSeconds_ = std::max(seconds, 0.0001f);
}

const ActionScheduler::InFlight* ActionScheduler::oldest() const
{
    return inFlight_.empty() ? nullptr : &inFlight_.front();
}

ActionScheduler::InFlight* ActionScheduler::find(std::size_t id)
{
    const auto found = std::ranges::find(inFlight_, id, &InFlight::id);
    return found == inFlight_.end() ? nullptr : &*found;
}

std::variant<ActionScheduler::Started, ActionScheduler::Rejection>
ActionScheduler::tryStart(
    const ActionPlan& plan,
    const ActionReservations& reservations,
    std::vector<GameState> legs,
    std::size_t causalGroup,
    ActionDeferral deferral)
{
    // Rounded down deliberately. An action starting part-way through a step
    // gets claims that begin fractionally early, which can only make the check
    // stricter - the opposite mistake would let two actions slip past each
    // other by a fraction of a step.
    // Ownership first, and it is the check that matters. An entity another
    // action is already moving is spoken for, whatever the cells say.
    if (const std::optional<Rejection> owned =
            ownershipConflict(plan, causalGroup)) {
        ++admissionStats_.refusedByOwnership;
        return *owned;
    }

    const int baseStep = currentStep() + std::max(deferral.steps, 0);
    if (const std::optional<ReservationTable::Conflict> conflict =
            reservations_.conflict(reservations, baseStep, causalGroup)) {
        ++admissionStats_.refusedByReservation;
        return Rejection {
            .blockedBy = conflict->heldBy,
            .cell = conflict->cell,
            .step = conflict->step,
        };
    }

    ++admissionStats_.admitted;
    const std::size_t id = nextId_++;
    reservations_.admit(id, reservations, baseStep, causalGroup);
    inFlight_.push_back({
        .id = id,
        .plan = plan,
        .legs = std::move(legs),
        // Counts up to zero first. The action is admitted and holds its claims
        // from now, so nothing can take the cells out from under it, but it
        // does not run until its cause has finished.
        .elapsedSeconds = -std::max(deferral.seconds, 0.0f),
        .baseStep = baseStep,
        .causalGroup = causalGroup,
    });
    return Started { .id = id };
}

std::optional<ActionScheduler::Rejection> ActionScheduler::tryStartAll(
    std::vector<Pending> batch, std::size_t causalGroup)
{
    // Checked in full before anything is admitted, so a refusal late in the
    // batch cannot leave the earlier members running.
    for (const Pending& pending : batch) {
        if (const std::optional<Rejection> owned =
                ownershipConflict(pending.plan, causalGroup)) {
            ++admissionStats_.refusedByOwnership;
            return *owned;
        }
        const int baseStep = currentStep() + std::max(pending.deferral.steps, 0);
        if (const std::optional<ReservationTable::Conflict> conflict =
                reservations_.conflict(
                    pending.reservations, baseStep, causalGroup)) {
            ++admissionStats_.refusedByReservation;
            return Rejection {
                .blockedBy = conflict->heldBy,
                .cell = conflict->cell,
                .step = conflict->step,
            };
        }
    }

    for (Pending& pending : batch) {
        const std::variant<Started, Rejection> started = tryStart(
            pending.plan,
            pending.reservations,
            std::move(pending.legs),
            causalGroup,
            pending.deferral);
        // Unreachable: every member was just checked against the same table,
        // and admitting a member of this group cannot refuse a later one - the
        // group is exempt from itself.
        if (const Rejection* rejection = std::get_if<Rejection>(&started)) {
            return *rejection;
        }
    }
    return std::nullopt;
}

void ActionScheduler::advanceClock(float deltaSeconds)
{
    if (inFlight_.empty()) {
        return;
    }
    const float step = std::max(deltaSeconds, 0.0f);
    clockSeconds_ += step;
    // Deliberately not clamped to the duration. How far an action ran past its
    // end is what orders the commits below, and clamping would erase it.
    // Readers that want a sampling time clamp on the way out.
    for (InFlight& action : inFlight_) {
        action.elapsedSeconds += step;
    }
}

std::vector<ActionScheduler::InFlight> ActionScheduler::commitFinished()
{
    struct Finished {
        std::size_t id = 0;
        float overshoot = 0.0f;
    };
    std::vector<Finished> finished;
    for (const InFlight& action : inFlight_) {
        // A deferred action has not started, so it cannot have finished -
        // including an instantaneous one, whose duration it would otherwise
        // already satisfy while still counting up to its start.
        if (action.elapsedSeconds >= 0.0f &&
            action.elapsedSeconds >= action.plan.durationSeconds) {
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

    std::vector<InFlight> completed;
    completed.reserve(finished.size());
    for (const Finished& entry : finished) {
        const auto found = std::ranges::find(inFlight_, entry.id, &InFlight::id);
        if (found == inFlight_.end()) {
            continue;
        }
        // Only what this action changed. Anything else in flight has been
        // writing its own entities, and a whole-state assignment would undo it.
        StateDelta::between(found->plan.before, found->plan.after)
            .applyTo(state_);
        reservations_.release(entry.id);
        completed.push_back(std::move(*found));
        inFlight_.erase(found);
    }
    return completed;
}

std::vector<ActionScheduler::InFlight> ActionScheduler::advance(
    float deltaSeconds)
{
    advanceClock(deltaSeconds);
    return commitFinished();
}

} // namespace sokoban
