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
// **Steps number instants, not intervals.** Step `i` is the moment step `i`
// begins, and a claim covers the instants at which the entity is standing in
// the cell. An entity that leaves a cell during step `i` holds it through
// instant `i` and no longer; one that arrives during step `i` holds its
// destination from instant `i + 1`.
//
// This is what lets a push work. The block vacates the pushed-from cell during
// the same step the player enters it, so the block holds it at instant `i` and
// the player from instant `i + 1` - no overlap, and the two run concurrently.
// Numbering by interval instead (claiming both the origin and destination for
// the whole of step `i`) makes every handoff look like a collision, which is
// neither true to what happens nor extensible to the other mechanics that hand
// cells over.
//
// The one thing instants alone do not catch is two entities swapping places
// through each other; `Traversal` below covers that.
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

// One entity crossing one cell boundary during one step.
//
// Occupancy at instants permits a handoff, which is right, but on its own it
// would also permit two entities to exchange cells in the same step: each is
// where the other was, so no instant is ever shared. They would pass straight
// through one another. Traversals are compared separately for exactly that.
struct Traversal {
    GridPosition3 from {};
    GridPosition3 to {};
    // The step during which the move happens: from the entity's position at
    // instant `step` to its position at instant `step + 1`.
    int step = 0;

    bool operator==(const Traversal&) const = default;
};

// What an action claims.
//
// `writes` are the instants at which it occupies cells. `reads` are cells whose
// contents its precomputed outcome depended on - chiefly whatever stopped a
// slide, since the block would have travelled further had that cell been empty.
// `moves` are the boundary crossings, which catch head-on swaps that occupancy
// alone would let through.
//
// The read set is deliberately conservative. Deriving it exactly would mean
// instrumenting `rules::step` to report every cell it consulted; approximating
// it can only reject concurrency that would in fact have been safe, never
// admit concurrency that is not.
struct ActionReservations {
    std::vector<Reservation> writes;
    std::vector<Reservation> reads;
    std::vector<Traversal> moves;

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
