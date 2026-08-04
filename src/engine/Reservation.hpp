#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/EntityId.hpp"
#include "engine/Math.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace sokoban {

// A claim on one cell over a span of world steps.
//
// Space alone is not enough. If a sliding block reaches a cell on step 7 and
// the player crosses that same cell on step 2, they never meet, and forbidding
// the player's move would lock out most of the board for the length of every
// slide. Time is what makes concurrency worth having.
//
// Steps are relative to the action that produced the reservation;
// `ReservationTable` offsets them onto the shared clock when it admits one.
struct Reservation {
    GridPosition3 cell {};
    int firstStep = 0;
    // Inclusive. Unset means the entity comes to rest here and holds the cell
    // until something else moves it.
    //
    // This is what catches the case a bounded interval misses: a player who
    // steps onto a cell and simply stays there writes it once, but is still
    // standing on it when a block arrives ten steps later.
    std::optional<int> lastStep;

    [[nodiscard]] bool overlaps(const Reservation& other) const;

    bool operator==(const Reservation&) const = default;
};

// What an action claims.
//
// `writes` are cells it occupies or vacates. `reads` are cells whose contents
// its precomputed outcome depended on - chiefly whatever stopped a slide,
// since the block would have travelled further had that cell been empty.
//
// The read set is deliberately conservative. Deriving it exactly would mean
// instrumenting `rules::step` to report every cell it consulted; approximating
// it can only reject concurrency that would in fact have been safe, never
// admit concurrency that is not.
struct ActionReservations {
    std::vector<Reservation> writes;
    std::vector<Reservation> reads;

    bool operator==(const ActionReservations&) const = default;
};

namespace plans {

// Everything the action claims, derived from the cells its entities pass
// through on each leg.
[[nodiscard]] ActionReservations reservationsFor(const PlannedAction& planned);

} // namespace plans

// The claims of every action currently in flight.
//
// Two actions conflict when one's writes touch the other's reads or writes over
// an overlapping span. A write-only test is not sufficient: an entity that
// stops on a cell and stays there never writes it again, yet its presence is
// exactly what would invalidate another action's plan.
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
    void admit(
        std::size_t actionId,
        const ActionReservations& reservations,
        int baseStep);
    void release(std::size_t actionId);
    void clear();

    [[nodiscard]] std::optional<Conflict> conflict(
        const ActionReservations& reservations, int baseStep) const;

    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::size_t actionId = 0;
        ActionReservations reservations;
    };

    std::vector<Entry> entries_;
};

} // namespace sokoban
