#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/Reservation.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace sokoban {

// How far ahead of this moment a plan begins.
//
// A slide planned alongside the push that caused it does not start now: the
// block is still being pushed. Both plans are made from the same instant -
// which is what the guarantee requires, and why the slide's destination is
// settled before the block has moved a tile - but the second one runs a step
// later.
//
// The two fields say the same thing in the two units the scheduler keeps.
// `steps` places the plan's claims on the shared step clock, so it is checked
// against where everything else will be rather than where it is. `seconds`
// holds its own clock back so it does not animate early; the caller supplies it
// rather than deriving it from `steps`, because it knows the actual duration of
// the action being waited on.
//
// At namespace scope rather than nested in `ActionScheduler` because it is a
// default argument of one of that class's own members, and a nested type is not
// complete enough to value-initialize there.
struct ActionDeferral {
    int steps = 0;
    float seconds = 0.0f;

    bool operator==(const ActionDeferral&) const = default;
};

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
        // Negative while the action is deferred - it has been admitted and
        // holds its claims, but has not begun. Zero is the moment it starts.
        // Readers that want a sampling time clamp to [0, duration]; the raw
        // value is what orders commits and what counts down to a start.
        float elapsedSeconds = 0.0f;
        // Where this action's step 0 sits on the shared clock.
        int baseStep = 0;
        // Which player action this one traces back to.
        //
        // Splitting a push from the slide it starts is a scheduling decision -
        // it is what releases the player after their own tile. It is not a
        // decision about history: from the player's side one input happened,
        // and undoing it should put back everything that followed. Actions
        // sharing a group fold into one undo entry when they commit.
        //
        // Transient, like `legs`. By the time a session is saved every action
        // has committed and every group is closed, so nothing here is persisted
        // and the save format is untouched.
        std::size_t causalGroup = 0;

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

    // One plan waiting to be admitted, with everything the scheduler needs to
    // run it.
    struct Pending {
        ActionPlan plan;
        ActionReservations reservations;
        std::vector<GameState> legs;
        ActionDeferral deferral;
    };

    // Why admissions were refused, so it can be measured rather than assumed.
    //
    // The two counters answer a question the design has carried as an open risk
    // from the start: whether the space-time reservation machinery earns its
    // complexity. Entity ownership is the simple rule - an entity another
    // action is already moving is off limits. Reservations are the elaborate
    // one, and they exist only to catch actions whose entities are disjoint but
    // whose paths cross. If `refusedByReservation` stays near zero in real
    // play while ownership carries every refusal, the elaborate half is dead
    // weight and can go.
    struct AdmissionStats {
        std::size_t admitted = 0;
        std::size_t refusedByOwnership = 0;
        std::size_t refusedByReservation = 0;

        bool operator==(const AdmissionStats&) const = default;
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
        std::vector<GameState> legs = {},
        std::size_t causalGroup = 0,
        ActionDeferral deferral = {});

    // Admits a whole causal group, or none of it.
    //
    // A push and the slide it sets off have to succeed or fail together. Taking
    // the push and then finding the slide refused would leave the block with
    // momentum and no action to spend it, and the promise the push made - that
    // where this ends up is settled now - broken by the back door.
    //
    // Members are checked against everything else in flight but not against
    // each other: they were planned as one, the consequence from the state its
    // cause produces, so their claims describe one coherent sequence. That is
    // the same reasoning the table's group exemption rests on.
    //
    // Null on success; the first rejection otherwise, with nothing admitted.
    [[nodiscard]] std::optional<Rejection> tryStartAll(
        std::vector<Pending> batch, std::size_t causalGroup);

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
    [[nodiscard]] const AdmissionStats& admissionStats() const
    {
        return admissionStats_;
    }

private:
    // Whether this plan would move an entity some action in flight already
    // owns.
    //
    // This is the rule the whole design rests on, stated directly: an action's
    // outcome is settled when it starts, so the entities it will move are
    // spoken for until it lands. Nothing else may plan for them.
    //
    // The reservation table cannot express it. Claims are about cells at
    // instants, and by the time a second action is admitted the table believes
    // a sliding block has long since left the cell it is claimed on - while
    // authoritative state, which is what planning reads, still has it sitting
    // there. Both are right on their own terms and the contradiction is
    // invisible to either.
    //
    // A causal group is exempt from itself: a consequence exists precisely to
    // take over the entities its cause was moving.
    [[nodiscard]] std::optional<Rejection> ownershipConflict(
        const ActionPlan& plan, std::size_t causalGroup) const;

    GameState state_;
    std::vector<InFlight> inFlight_;
    ReservationTable reservations_;
    float clockSeconds_ = 0.0f;
    float stepDurationSeconds_ = config::stepDurationSeconds;
    std::size_t nextId_ = 1;
    AdmissionStats admissionStats_;
};

} // namespace sokoban
