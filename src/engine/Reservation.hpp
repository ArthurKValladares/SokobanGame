#pragma once

#include "engine/ActionPlan.hpp"
#include "engine/Math.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace sokoban {

// A claim on one cell over an inclusive span of action-relative steps.
// A missing end means the claim remains held. `ReservationTable` offsets the
// span onto the shared clock when it admits the action.
struct Reservation {
    GridPosition3 cell {};
    int firstStep = 0;
    // Unset means the entity comes to rest here and holds the cell indefinitely.
    std::optional<int> lastStep;

    [[nodiscard]] bool overlaps(const Reservation& other) const;

    bool operator==(const Reservation&) const = default;
};

// Movement claims plus any exclusive claim on physical plate signals.
struct ActionReservations {
    std::vector<Reservation> cells;
    // A physical plate can change collision, support and reactions anywhere
    // on the board. Such actions hold the world signal state until commit;
    // unrelated actions may overlap only while those signals stay unchanged.
    bool pressurePlateActivityChanged = false;

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
// overlapping span, or either changes physical pressure-plate activity.
class ReservationTable {
public:
    struct Conflict {
        // The action already in flight that the newcomer would disturb.
        std::size_t heldBy = 0;
        // World signal conflicts have no single offending cell and use {}.
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
