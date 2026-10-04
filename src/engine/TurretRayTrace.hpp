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
template<class CellQuery, class PortalQuery>
bool traceTurretRay(
    GridPosition3 turret,
    MoveDirection direction,
    std::optional<GridPosition3> target,
    CellQuery cellAt,
    PortalQuery portalAt,
    std::vector<TurretRaySegment>* segments)
{
    GridPosition3 segmentStart = turret;
    GridPosition startEdge {};
    GridPosition3 cell = turret;
    std::vector<std::pair<GridPosition3, MoveDirection>> visited;
    const auto finish = [&](GridPosition edge) {
        if (segments) {
            segments->push_back({ segmentStart, cell, startEdge, edge });
        }
    };
    while (true) {
        const auto key = std::pair { cell, direction };
        if (std::ranges::find(visited, key) != visited.end()) {
            finish({});
            return false;
        }
        visited.push_back(key);
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

} // namespace sokoban::rules
