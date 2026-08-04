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
        // wrong: a plan that does not touch an entity would still claim the cell
        // that entity is resting on, open-ended. Since every plan is computed
        // from the same whole state, every plan claimed every stationary
        // entity's cell, so no two plans could ever be concurrent - the
        // reservation table would reject a player step on the far side of the
        // board from a sliding block.
        //
        // An entity another action depends on staying put is not this action's
        // to hold. Nothing needs it to be: an outcome is settled when its plan
        // is made and is meant not to change, so there is nothing to protect.
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
        if (existedBefore) {
            track.id = resolvedEntityId(kind, before[index].id, index);
            track.cells.push_back(before[index].cell);
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
        }
        tracks.push_back(std::move(track));
    }
}

[[nodiscard]] bool sameCell(GridPosition3 a, GridPosition3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
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
        // The last instant at which the entity is standing in each cell - the
        // instant it leaves it, in other words. The claim runs from the start of
        // the action up to and including that instant.
        struct Span {
            GridPosition3 cell {};
            int lastStep = 0;
        };
        std::vector<Span> spans;
        const auto claim = [&spans](GridPosition3 at, int step) {
            for (Span& span : spans) {
                if (sameCell(span.cell, at)) {
                    span.lastStep = std::max(span.lastStep, step);
                    return;
                }
            }
            spans.push_back({ .cell = at, .lastStep = step });
        };

        for (std::size_t i = 0; i < track.cells.size(); ++i) {
            claim(track.cells[i], track.firstInstant + static_cast<int>(i));
        }

        // Every cell on the path is held from the action's own start, not from
        // the instant the entity arrives. A cell it is going to occupy is as
        // much its own as one it occupies now - the outcome was settled when the
        // action began, and anything that walked in ahead of it would have to be
        // pushed aside by a plan that has already been made. What time still
        // buys is the trailing half: once the entity has left, the cell is free,
        // so something else may follow in behind a slide.
        //
        // `firstInstant` is zero for everything present when the action began.
        // It is later only for an entity the action adds part-way through, which
        // was standing nowhere at all before that.
        //
        // The cell it finishes on gets no end at all: it is still standing there
        // long after the action is over, which is precisely what a later action
        // needs to know.
        const GridPosition3 resting = track.cells.back();
        for (const Span& span : spans) {
            addReservation(result.cells, {
                .cell = span.cell,
                .firstStep = track.firstInstant,
                .lastStep = sameCell(span.cell, resting)
                    ? std::nullopt
                    : std::optional<int>(span.lastStep),
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
    int baseStep,
    std::size_t causalGroup)
{
    entries_.push_back({
        .actionId = actionId,
        .causalGroup = causalGroup,
        .reservations = { .cells = offsetBy(reservations.cells, baseStep) },
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
    const std::vector<Reservation> cells =
        offsetBy(reservations.cells, baseStep);

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
        if (exemptGroup != 0 && entry.causalGroup == exemptGroup) {
            continue;
        }
        // Two entities exchanging cells needs no separate check any more. Each
        // one's claim on its destination begins at its own instant 0, where the
        // other is still standing, so they overlap on both cells.
        if (const std::optional<Reservation> found =
                clash(cells, entry.reservations.cells)) {
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
