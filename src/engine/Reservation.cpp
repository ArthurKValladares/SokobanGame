#include "engine/Reservation.hpp"

#include <algorithm>

namespace sokoban {
namespace {

// Where an entity sits after each leg, keyed so that a mirror activation
// appending players cannot shift one entity's track onto another.
struct EntityTrack {
    EntityId id = invalidEntityId;
    // The instant `cells[0]` describes. Zero for anything present when the
    // action began; later for an entity the action creates part-way through.
    int firstInstant = 0;
    // cells[i] is where it stands at instant `firstInstant + i`.
    std::vector<GridPosition3> cells;
    // Whether it ever carried slide momentum. Only a sliding entity can be
    // said to have been *stopped* by something; see the read set below.
    bool slid = false;
};

template <EntityKind kind, typename Entity>
void collectTracks(
    const std::vector<Entity>& before,
    const std::vector<std::vector<Entity>>& perLeg,
    std::vector<EntityTrack>& tracks)
{
    // Past the end of `before`, because an action may *add* entities: mirror
    // activation clones a player, and the clone's cell went unclaimed entirely
    // while this loop ran to `before.size()`. An unclaimed cell is one another
    // action is free to walk into.
    std::size_t count = before.size();
    for (const std::vector<Entity>& leg : perLeg) {
        count = std::max(count, leg.size());
    }

    for (std::size_t index = 0; index < count; ++index) {
        const bool existedBefore = index < before.size();
        // Only entities this action actually involves.
        //
        // Claiming every entity in the world was the original reading and it is
        // wrong: a plan that does not touch an entity would still write-claim
        // the cell that entity is resting on, open-ended. Since every plan is
        // computed from the same whole state, every plan claimed every
        // stationary entity's cell, so no two plans could ever be concurrent -
        // the reservation table would reject a player step on the far side of
        // the board from a sliding block.
        //
        // An entity another action depends on staying put is that action's
        // read set to declare, not this one's to hold.
        //
        // One the action creates is involved by definition.
        const bool involved = !existedBefore ||
            std::ranges::any_of(
                perLeg,
                [&](const std::vector<Entity>& leg) {
                    // Removed counts as involved: the action took it out of the
                    // world, which is as much a change as moving it.
                    return index >= leg.size() || !(leg[index] == before[index]);
                });
        if (!involved) {
            continue;
        }

        EntityTrack track;
        const auto noteMomentum = [&track](const Entity& entity) {
            if constexpr (requires { entity.sliding; }) {
                track.slid = track.slid || entity.sliding.has_value();
            }
        };

        if (existedBefore) {
            track.id = resolvedEntityId(kind, before[index].id, index);
            track.cells.push_back(before[index].cell);
            noteMomentum(before[index]);
        } else {
            // Claimed from the instant it appears rather than from the start of
            // the action. It did not exist before that, and claiming a cell it
            // was not standing in would refuse concurrency that is in fact
            // fine.
            const auto appears = std::ranges::find_if(
                perLeg,
                [index](const std::vector<Entity>& leg) {
                    return index < leg.size();
                });
            if (appears == perLeg.end()) {
                continue;
            }
            track.firstInstant =
                static_cast<int>(std::distance(perLeg.begin(), appears)) + 1;
            track.id = resolvedEntityId(kind, (*appears)[index].id, index);
        }

        for (std::size_t leg = 0; leg < perLeg.size(); ++leg) {
            // Instants before it exists are not its to claim.
            if (static_cast<int>(leg) + 1 < track.firstInstant) {
                continue;
            }
            // An entity that vanishes keeps its last known cell, which is the
            // conservative reading: the cell stays claimed.
            track.cells.push_back(
                index < perLeg[leg].size()
                    ? perLeg[leg][index].cell
                    : track.cells.back());
            if (index < perLeg[leg].size()) {
                noteMomentum(perLeg[leg][index]);
            }
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

        // `cells[i]` is where the entity stands at instant i, so that is the
        // only instant it holds that cell for. Claiming the destination too
        // would mean an entity held both ends of every move for the whole step,
        // and a push - where one entity leaves a cell exactly as another enters
        // it - could never be admitted.
        for (std::size_t i = 0; i < track.cells.size(); ++i) {
            claim(track.cells[i], track.firstInstant + static_cast<int>(i));
        }

        // The crossings themselves, so that two entities cannot trade places
        // through one another while sharing no instant.
        for (std::size_t i = 0; i + 1 < track.cells.size(); ++i) {
            if (!sameCell(track.cells[i], track.cells[i + 1])) {
                result.moves.push_back({
                    .from = track.cells[i],
                    .to = track.cells[i + 1],
                    .step = track.firstInstant + static_cast<int>(i),
                });
            }
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

        // Only a sliding entity gets an inferred stopping read.
        //
        // The cell an entity would have entered next is evidence about its
        // outcome only if it was still travelling. An entity that moved one
        // tile under input stopped because the input was one tile, not because
        // anything was in the way, and reading the cell beyond it is a claim on
        // a dependency that does not exist. That spurious read is enough to
        // refuse a push - the player's step would read exactly the cell the
        // block it just pushed is moving into.
        //
        // Momentum is the distinction, and it is right there in the state.
        // Deriving the rest of the read set properly means having the planners
        // declare what they consulted; this is the part that can be known from
        // the states alone.
        if (const std::optional<GridPosition3> blocker =
                track.slid ? blockingCell(track.cells) : std::nullopt) {
            // Claimed from the moment the entity comes to rest: had this cell
            // been clear then, the entity would have kept going.
            addReservation(result.reads, {
                .cell = *blocker,
                .firstStep =
                    track.firstInstant + static_cast<int>(track.cells.size()) - 1,
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

[[nodiscard]] std::vector<Traversal> offsetBy(
    const std::vector<Traversal>& moves, int baseStep)
{
    std::vector<Traversal> result;
    result.reserve(moves.size());
    for (const Traversal& move : moves) {
        result.push_back({
            .from = move.from,
            .to = move.to,
            .step = move.step + baseStep,
        });
    }
    return result;
}

} // namespace

void ReservationTable::admit(
    std::size_t actionId,
    const ActionReservations& reservations,
    int baseStep,
    std::size_t causalGroup)
{
    entries_.push_back({
        .actionId = actionId,
        .causalGroup = causalGroup,
        .reservations = {
            .writes = offsetBy(reservations.writes, baseStep),
            .reads = offsetBy(reservations.reads, baseStep),
            .moves = offsetBy(reservations.moves, baseStep),
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
    const ActionReservations& reservations,
    int baseStep,
    std::size_t exemptGroup) const
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

    const std::vector<Traversal> moves = offsetBy(reservations.moves, baseStep);

    // Two entities exchanging cells in one step share no instant, so occupancy
    // alone says they are fine. They would pass through each other.
    const auto swaps = [](
        const std::vector<Traversal>& left,
        const std::vector<Traversal>& right)
        -> std::optional<Reservation> {
        for (const Traversal& a : left) {
            for (const Traversal& b : right) {
                if (a.step == b.step && a.from == b.to && a.to == b.from) {
                    return Reservation {
                        .cell = a.to,
                        .firstStep = a.step,
                        .lastStep = std::nullopt,
                    };
                }
            }
        }
        return std::nullopt;
    };

    for (const Entry& entry : entries_) {
        if (exemptGroup != 0 && entry.causalGroup == exemptGroup) {
            continue;
        }
        std::optional<Reservation> found =
            clash(writes, entry.reservations.writes);
        if (!found) {
            found = clash(writes, entry.reservations.reads);
        }
        if (!found) {
            found = clash(reads, entry.reservations.writes);
        }
        if (!found) {
            found = swaps(moves, entry.reservations.moves);
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
