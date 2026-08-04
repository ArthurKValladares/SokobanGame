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
        // The states the action passes through, one per world step. A chained
        // slide has several; everything else has one or none. The presentation
        // animates a chain tile by tile from these rather than interpolating
        // once from start to finish.
        //
        // Transient by design: a restored action already carries its built
        // timeline, so legs are never persisted.
        std::vector<GameState> legs;
        float elapsedSeconds = 0.0f;
        // Where this action's step 0 sits on the shared clock.
        int baseStep = 0;

        bool operator==(const InFlight&) const = default;
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
        const ActionPlan& plan,
        const ActionReservations& reservations,
        std::vector<GameState> legs = {});

    // Advancing and committing are separate calls because the caller does work
    // between them: `GameplaySession` samples the presentation at the new
    // elapsed time before any action is allowed to commit, and its history
    // bookkeeping happens on the commit itself.
    //
    // The clock only runs while something is in flight. Steps are a measure of
    // world motion, not of wall time, and command staleness is judged against
    // the same clock - a command entered while the world is idle should not age.
    void advanceClock(float deltaSeconds);

    // Commits and removes every action whose duration has elapsed, returning
    // the finished records in commit order. The records are returned whole
    // rather than as ids because the caller needs the plan itself for history.
    //
    // Completed actions commit as deltas, so two finishing in the same tick do
    // not overwrite each other. Their effects are disjoint by construction -
    // that is what admission guaranteed - but the order is made deterministic
    // anyway, because a non-deterministic commit order would be impossible to
    // reproduce in a bug report.
    std::vector<InFlight> commitFinished();

    // Both, in order.
    std::vector<InFlight> advance(float deltaSeconds);

    // The action that has been running longest, or null when idle. Callers that
    // still speak in terms of a single active action resolve to this one.
    [[nodiscard]] const InFlight* oldest() const;
    // Mutable because a presentation timeline is installed after the action has
    // started - `GameplayLoop` builds it from the plan the scheduler accepted.
    [[nodiscard]] InFlight* find(std::size_t id);

    void setStepDurationSeconds(float seconds);

    [[nodiscard]] const GameState& state() const { return state_; }
    // Direct replacement, for the paths that do not go through an action at
    // all: loading a screen, and restoring a save.
    void setState(GameState state) { state_ = std::move(state); }
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
