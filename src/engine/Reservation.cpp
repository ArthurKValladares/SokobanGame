#include "engine/Reservation.hpp"

#include <algorithm>

namespace sokoban {
namespace {

// Where an entity sits after each leg, keyed so that a mirror activation
// appending players cannot shift one entity's track onto another.
struct EntityTrack {
    EntityId id = invalidEntityId;
    // cells[0] is where it started; cells[i + 1] is where leg i left it.
    std::vector<GridPosition3> cells;
};

template <EntityKind kind, typename Entity>
void collectTracks(
    const std::vector<Entity>& before,
    const std::vector<std::vector<Entity>>& perLeg,
    std::vector<EntityTrack>& tracks)
{
    for (std::size_t index = 0; index < before.size(); ++index) {
        EntityTrack track;
        track.id = resolvedEntityId(kind, before[index].id, index);
        track.cells.push_back(before[index].cell);
        for (const std::vector<Entity>& leg : perLeg) {
            // An entity that vanishes keeps its last known cell, which is the
            // conservative reading: the cell stays claimed.
            track.cells.push_back(
                index < leg.size() ? leg[index].cell : track.cells.back());
        }
        tracks.push_back(std::move(track));
    }
}

[[nodiscard]] bool sameCell(GridPosition3 a, GridPosition3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// The cell an entity would have moved into next had nothing been there. That
// cell's contents are why the entity stopped, so the outcome depended on them.
[[nodiscard]] std::optional<GridPosition3> blockingCell(
    const std::vector<GridPosition3>& cells)
{
    for (std::size_t i = cells.size(); i-- > 1;) {
        if (!sameCell(cells[i], cells[i - 1])) {
            const GridPosition3 last = cells.back();
            return GridPosition3 {
                last.x + (cells[i].x - cells[i - 1].x),
                last.y + (cells[i].y - cells[i - 1].y),
                last.z + (cells[i].z - cells[i - 1].z),
            };
        }
    }
    return std::nullopt;
}

void addReservation(
    std::vector<Reservation>& into, const Reservation& reservation)
{
    if (std::ranges::find(into, reservation) == into.end()) {
        into.push_back(reservation);
    }
}

} // namespace

bool Reservation::overlaps(const Reservation& other) const
{
    if (!sameCell(cell, other.cell)) {
        return false;
    }
    // Unset ends run forever, which is how a resting entity keeps its cell.
    const bool startsAfterOtherEnds =
        other.lastStep.has_value() && firstStep > *other.lastStep;
    const bool endsBeforeOtherStarts =
        lastStep.has_value() && *lastStep < other.firstStep;
    return !startsAfterOtherEnds && !endsBeforeOtherStarts;
}

namespace plans {

ActionReservations reservationsFor(const PlannedAction& planned)
{
    ActionReservations result;
    if (planned.legs.empty()) {
        return result;
    }

    std::vector<std::vector<GameState::Player>> playerLegs;
    std::vector<std::vector<GameState::Movable>> movableLegs;
    std::vector<std::vector<GameState::Enemy>> enemyLegs;
    playerLegs.reserve(planned.legs.size());
    movableLegs.reserve(planned.legs.size());
    enemyLegs.reserve(planned.legs.size());
    for (const GameState& leg : planned.legs) {
        playerLegs.push_back(leg.players);
        movableLegs.push_back(leg.movables);
        enemyLegs.push_back(leg.enemies);
    }

    std::vector<EntityTrack> tracks;
    collectTracks<EntityKind::Player>(
        planned.action.before.players, playerLegs, tracks);
    collectTracks<EntityKind::Movable>(
        planned.action.before.movables, movableLegs, tracks);
    collectTracks<EntityKind::Enemy>(
        planned.action.before.enemies, enemyLegs, tracks);

    for (const EntityTrack& track : tracks) {
        // During step i the entity travels from cells[i] to cells[i + 1], so it
        // is claimed to be in both for the length of that step. Steps are
        // numbered by leg, which is what the shared clock counts.
        struct Span {
            GridPosition3 cell {};
            int firstStep = 0;
            int lastStep = 0;
        };
        std::vector<Span> spans;
        const auto claim = [&spans](GridPosition3 at, int step) {
            for (Span& span : spans) {
                if (sameCell(span.cell, at)) {
                    span.firstStep = std::min(span.firstStep, step);
                    span.lastStep = std::max(span.lastStep, step);
                    return;
                }
            }
            spans.push_back({ .cell = at, .firstStep = step, .lastStep = step });
        };

        for (std::size_t i = 0; i + 1 < track.cells.size(); ++i) {
            const int step = static_cast<int>(i);
            claim(track.cells[i], step);
            claim(track.cells[i + 1], step);
        }

        // The cell it finishes on is claimed open-ended: it is still standing
        // there long after the action is over, which is precisely what a later
        // action needs to know.
        const GridPosition3 resting = track.cells.back();
        for (const Span& span : spans) {
            addReservation(result.writes, {
                .cell = span.cell,
                .firstStep = span.firstStep,
                .lastStep = sameCell(span.cell, resting)
                    ? std::nullopt
                    : std::optional<int>(span.lastStep),
            });
        }

        if (const std::optional<GridPosition3> blocker =
                blockingCell(track.cells)) {
            // Claimed from the moment the entity comes to rest: had this cell
            // been clear then, the entity would have kept going.
            addReservation(result.reads, {
                .cell = *blocker,
                .firstStep = static_cast<int>(track.cells.size()) - 1,
                .lastStep = std::nullopt,
            });
        }
    }
    return result;
}

} // namespace plans

namespace {

[[nodiscard]] std::vector<Reservation> offsetBy(
    const std::vector<Reservation>& reservations, int baseStep)
{
    std::vector<Reservation> result;
    result.reserve(reservations.size());
    for (const Reservation& reservation : reservations) {
        result.push_back({
            .cell = reservation.cell,
            .firstStep = reservation.firstStep + baseStep,
            .lastStep = reservation.lastStep
                ? std::optional<int>(*reservation.lastStep + baseStep)
                : std::nullopt,
        });
    }
    return result;
}

} // namespace

void ReservationTable::admit(
    std::size_t actionId,
    const ActionReservations& reservations,
    int baseStep)
{
    entries_.push_back({
        .actionId = actionId,
        .reservations = {
            .writes = offsetBy(reservations.writes, baseStep),
            .reads = offsetBy(reservations.reads, baseStep),
        },
    });
}

void ReservationTable::release(std::size_t actionId)
{
    std::erase_if(entries_, [actionId](const Entry& entry) {
        return entry.actionId == actionId;
    });
}

void ReservationTable::clear()
{
    entries_.clear();
}

std::optional<ReservationTable::Conflict> ReservationTable::conflict(
    const ActionReservations& reservations, int baseStep) const
{
    const std::vector<Reservation> writes =
        offsetBy(reservations.writes, baseStep);
    const std::vector<Reservation> reads =
        offsetBy(reservations.reads, baseStep);

    // A conflict is one side's write against the other's reads or writes.
    // Reads against reads are harmless: two actions may both depend on the same
    // wall staying put.
    const auto clash = [](
        const std::vector<Reservation>& left,
        const std::vector<Reservation>& right)
        -> std::optional<Reservation> {
        for (const Reservation& a : left) {
            for (const Reservation& b : right) {
                if (a.overlaps(b)) {
                    return Reservation {
                        .cell = a.cell,
                        .firstStep = std::max(a.firstStep, b.firstStep),
                        .lastStep = std::nullopt,
                    };
                }
            }
        }
        return std::nullopt;
    };

    for (const Entry& entry : entries_) {
        std::optional<Reservation> found =
            clash(writes, entry.reservations.writes);
        if (!found) {
            found = clash(writes, entry.reservations.reads);
        }
        if (!found) {
            found = clash(reads, entry.reservations.writes);
        }
        if (found) {
            return Conflict {
                .heldBy = entry.actionId,
                .cell = found->cell,
                .step = found->firstStep,
            };
        }
    }
    return std::nullopt;
}

} // namespace sokoban
