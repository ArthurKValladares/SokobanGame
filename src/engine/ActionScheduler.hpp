#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/Reservation.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace sokoban {

// Several actions in flight at once, each running to its own clock.
//
// This is what buys responsiveness: the player keeps moving while a pushed
// block is still travelling. It is only safe because an action's outcome was
// settled before it started and its claims are checked against everything else
// running, so two actions can never be resolving the same cell at the same
// time.
//
// The scheduler executes plans; it does not make them. Deciding *what* to plan,
// and in what order, stays with the session - which is also where the rule that
// queued player commands are admitted ahead of ambient belt motion belongs,
// since only the session knows which is which.
class ActionScheduler {
public:
    struct InFlight {
        std::size_t id = 0;
        ActionPlan plan;
        float elapsedSeconds = 0.0f;
        // Where this action's step 0 sits on the shared clock.
        int baseStep = 0;
    };

    // Why an action could not start. Carries enough to point at the offending
    // cell, which is what a rejection would need to explain itself.
    struct Rejection {
        std::size_t blockedBy = 0;
        GridPosition3 cell {};
        int step = 0;

        bool operator==(const Rejection&) const = default;
    };

    struct Started {
        std::size_t id = 0;
    };

    void reset(GameState state, float stepDurationSeconds);

    // Admits the plan when nothing already running would be disturbed by it.
    //
    // The plan is taken as-is: it was computed against the state at this
    // moment, and if it does not conflict then nothing in flight can change any
    // cell it depended on, so its outcome still holds when it finishes.
    [[nodiscard]] std::variant<Started, Rejection> tryStart(
        const ActionPlan& plan, const ActionReservations& reservations);

    // Advances every action, committing those that finish. Returns their ids in
    // completion order.
    //
    // Completed actions commit as deltas, so two finishing in the same tick do
    // not overwrite each other. Their effects are disjoint by construction -
    // that is what admission guaranteed - but the order is made deterministic
    // anyway, because a non-deterministic commit order would be impossible to
    // reproduce in a bug report.
    std::vector<std::size_t> advance(float deltaSeconds);

    [[nodiscard]] const GameState& state() const { return state_; }
    [[nodiscard]] bool idle() const { return inFlight_.empty(); }
    [[nodiscard]] const std::vector<InFlight>& inFlight() const
    {
        return inFlight_;
    }
    // Whole world steps elapsed. Reservations are placed against this, so every
    // action shares one timeline rather than each counting from its own start.
    [[nodiscard]] int currentStep() const;
    [[nodiscard]] float clockSeconds() const { return clockSeconds_; }

private:
    GameState state_;
    std::vector<InFlight> inFlight_;
    ReservationTable reservations_;
    float clockSeconds_ = 0.0f;
    float stepDurationSeconds_ = config::stepDurationSeconds;
    std::size_t nextId_ = 1;
};

} // namespace sokoban
