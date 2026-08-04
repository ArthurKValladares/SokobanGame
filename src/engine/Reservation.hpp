#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/EntityId.hpp"
#include "engine/Math.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace sokoban {

// A claim on one cell, held from the moment the action starts until the instant
// the entity leaves it.
//
// Space alone is not enough. A block that reaches a cell on step 7 and leaves
// it on step 8 has no further business there, and forbidding everything else
// from that cell for the rest of the slide would lock out most of the board.
// Time is what makes concurrency worth having - but only *behind* the entity.
//
// Ahead of it the claim runs from the action's own start. A cell the block will
// reach but has not yet is claimed all the same, so nothing else may enter its
// path. That is deliberate, not a gap: see "What the concurrency is for" in
// DESIGN-deterministic-actions.md. Exactly two kinds of concurrency are wanted,
// and holding the player out of a cell an ongoing action is going to occupy is
// the price of keeping this small.
//
// So for a new action beginning at step `S`, against a cell the block vacates
// at instant `i`: refused when `S <= i` (not there yet), admitted when `S > i`
// (already passed). Those are the two wanted cases and nothing more.
//
// Steps are relative to the action that produced the reservation;
// `ReservationTable` offsets them onto the shared clock when it admits one.
struct Reservation {
    GridPosition3 cell {};
    // The action's own base, for everything standing in the world when it
    // began. Later only for an entity the action *adds* part-way through - a
    // mirror clone - which has no business claiming a cell it was not yet
    // standing in.
    int firstStep = 0;
    // Inclusive. Unset means the entity comes to rest here and holds the cell
    // until something else moves it.
    //
    // This is what catches the case a bounded interval misses: a player who
    // steps onto a cell and simply stays there occupies it once, but is still
    // standing on it when a block arrives ten steps later.
    std::optional<int> lastStep;

    [[nodiscard]] bool overlaps(const Reservation& other) const;

    bool operator==(const Reservation&) const = default;
};

// Every cell an action claims, over the span it claims it for.
//
// There is only the one set. An earlier design also carried a *read* set - the
// cells a precomputed outcome depended on, chiefly whatever stopped a slide -
// and a set of boundary crossings to catch two entities swapping places. The
// claim rule above subsumes both. The destination is claimed from the start, so
// a plain claim-against-claim test refuses anything standing in it; and two
// entities exchanging cells now overlap, because each one's claim on its
// destination begins at its own instant 0, where the other is still sitting.
struct ActionReservations {
    std::vector<Reservation> cells;

    bool operator==(const ActionReservations&) const = default;
};

namespace plans {

// Everything the action claims, derived from the cells its entities pass
// through on each leg.
[[nodiscard]] ActionReservations reservationsFor(const PlannedAction& planned);

} // namespace plans

// The claims of every action currently in flight.
//
// Two actions conflict when their claims touch the same cell over an
// overlapping span.
class ReservationTable {
public:
    struct Conflict {
        // The action already in flight that the newcomer would disturb.
        std::size_t heldBy = 0;
        GridPosition3 cell {};
        int step = 0;

        bool operator==(const Conflict&) const = default;
    };

    // `baseStep` is where the action's own step 0 falls on the shared clock.
    // `causalGroup` is the player action it traces back to; zero means none.
    void admit(
        std::size_t actionId,
        const ActionReservations& reservations,
        int baseStep,
        std::size_t causalGroup = 0);
    void release(std::size_t actionId);
    void clear();

    // `exemptGroup`, when non-zero, skips actions belonging to that causal
    // group.
    //
    // A consequence always collides with its own cause, and the collision is an
    // artifact rather than a real one. The cause claims its entities' final
    // cells open-ended - they are still standing there when it ends - and the
    // consequence is precisely the continuation that moves those same entities
    // out of them. Since it was planned from the state its cause produces, it
    // cannot disagree with it: there is nothing for the table to protect.
    //
    // Only the group is exempt. A third action still sees the cause's claims and
    // is held off until it commits, which is the conservative reading.
    [[nodiscard]] std::optional<Conflict> conflict(
        const ActionReservations& reservations,
        int baseStep,
        std::size_t exemptGroup = 0) const;

    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::size_t actionId = 0;
        std::size_t causalGroup = 0;
        ActionReservations reservations;
    };

    std::vector<Entry> entries_;
};

} // namespace sokoban
