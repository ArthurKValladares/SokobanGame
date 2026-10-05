#pragma once

#include "engine/Rules.hpp"

#include <algorithm>
#include <utility>
#include <cstdint>

namespace sokoban::rules {

enum class TurretRayCell : uint8_t { Open, Occupied, Blocked };

// The rules and editor use the same traversal, including front-edge portal
// crossings and loop detection. Queries let unfinished drafts supply their
// authored terrain and occupants without constructing a playable Level.
// A blocked ray ends at the near edge of terrain, or at an occupant's center.
template<class CellQuery, class PortalQuery, class SegmentSink>
bool traceTurretRayWithSegments(
    GridPosition3 turret,
    MoveDirection direction,
    std::optional<GridPosition3> target,
    CellQuery cellAt,
    PortalQuery portalAt,
    SegmentSink appendSegment)
{
    GridPosition3 segmentStart = turret;
    GridPosition startEdge {};
    GridPosition3 cell = turret;
    // Brent's cycle detector retains one checkpoint instead of allocating a
    // visited list for every sightline query. Traversal is deterministic while
    // the board is unchanged, so repeating (cell, direction) proves a loop.
    auto checkpoint = std::pair { cell, direction };
    std::size_t power = 1;
    std::size_t distance = 0;
    const auto finish = [&](GridPosition edge) {
        appendSegment(TurretRaySegment { segmentStart, cell, startEdge, edge });
    };
    while (true) {
        const auto key = std::pair { cell, direction };
        if (distance != 0 && key == checkpoint) {
            finish({});
            return false;
        }
        if (distance == power) {
            checkpoint = key;
            power *= 2;
            distance = 0;
        }
        ++distance;
        if (const auto crossing = portalAt(cell, direction)) {
            finish(directionOffset(direction));
            cell = crossing->exit;
            direction = rotateDirection(direction, crossing->quarterTurns);
            segmentStart = cell;
            startEdge = { -crossing->direction.x, -crossing->direction.y };
        } else {
            cell = movementTarget(cell, direction);
        }
        const TurretRayCell status = cellAt(cell);
        if (status == TurretRayCell::Blocked) {
            const GridPosition offset = directionOffset(direction);
            finish({ -offset.x, -offset.y });
            return false;
        }
        if (cell == target) {
            finish({});
            return true;
        }
        if (status == TurretRayCell::Occupied) {
            finish({});
            return false;
        }
    }
}

template<class CellQuery, class PortalQuery>
bool traceTurretRay(
    GridPosition3 turret,
    MoveDirection direction,
    std::optional<GridPosition3> target,
    CellQuery cellAt,
    PortalQuery portalAt,
    std::vector<TurretRaySegment>* segments)
{
    return traceTurretRayWithSegments(turret, direction, target, cellAt, portalAt,
        [segments](const TurretRaySegment& segment) {
            if (segments) {
                segments->push_back(segment);
            }
        });
}

} // namespace sokoban::rules
