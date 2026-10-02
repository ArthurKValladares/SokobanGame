#include "engine/solver/Heuristic.hpp"

#include "engine/TileTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <queue>
#include <utility>

namespace sokoban::solver::detail {
namespace {

constexpr int unreachable = std::numeric_limits<int>::max() / 4;

constexpr std::array directions {
    MoveDirection::Up,
    MoveDirection::Down,
    MoveDirection::Left,
    MoveDirection::Right,
};

struct MirrorRays {
    GridPosition first {};
    GridPosition second {};
};

struct MirrorHit {
    GridPosition3 cell {};
    GridPosition output {};
    int distance = 0;
};

TileType tileAt(const Level& level, GridPosition3 cell)
{
    return level.tileAt(
        static_cast<std::uint32_t>(cell.x),
        static_cast<std::uint32_t>(cell.y),
        static_cast<std::uint32_t>(cell.z));
}

std::optional<MirrorRays> mirrorRays(TileType tile)
{
    switch (tile) {
    case TileType::MirrorNorthWest:
        return MirrorRays { { 0, -1 }, { -1, 0 } };
    case TileType::MirrorNorthEast:
        return MirrorRays { { 0, -1 }, { 1, 0 } };
    case TileType::MirrorSouthWest:
        return MirrorRays { { 0, 1 }, { -1, 0 } };
    case TileType::MirrorSouthEast:
        return MirrorRays { { 0, 1 }, { 1, 0 } };
    default:
        return std::nullopt;
    }
}

GridPosition3 offset(GridPosition3 cell, GridPosition direction, int scale = 1)
{
    return {
        cell.x + direction.x * scale,
        cell.y + direction.y * scale,
        cell.z,
    };
}

std::optional<int> distanceAlongRay(
    GridPosition3 origin,
    GridPosition3 target,
    GridPosition ray)
{
    if (origin.z != target.z) {
        return std::nullopt;
    }
    const int dx = target.x - origin.x;
    const int dy = target.y - origin.y;
    if (ray.x != 0 && dy == 0 && dx * ray.x > 0) {
        return std::abs(dx);
    }
    if (ray.y != 0 && dx == 0 && dy * ray.y > 0) {
        return std::abs(dy);
    }
    return std::nullopt;
}

bool clearStaticRay(
    const Level& level,
    GridPosition3 mirror,
    GridPosition ray,
    int first,
    int last)
{
    for (int step = first; step <= last; ++step) {
        if (!rules::staticCellAllowsEntity(
                level, offset(mirror, ray, step))) {
            return false;
        }
    }
    return true;
}

std::vector<MirrorHit> nearestMirrors(
    const Level& level,
    GridPosition3 cell,
    const std::vector<GridPosition3>& used)
{
    std::vector<MirrorHit> nearest;
    int nearestDistance = 0;
    if (cell.z < 0 || cell.z >= static_cast<int>(level.depth())) {
        return nearest;
    }
    for (int y = 0; y < static_cast<int>(level.height()); ++y) {
        for (int x = 0; x < static_cast<int>(level.width()); ++x) {
            const GridPosition3 mirror { x, y, cell.z };
            if (std::ranges::find(used, mirror) != used.end()) {
                continue;
            }
            const std::optional<MirrorRays> rays =
                mirrorRays(tileAt(level, mirror));
            if (!rays) {
                continue;
            }
            const std::array pairs {
                std::pair { rays->first, rays->second },
                std::pair { rays->second, rays->first },
            };
            for (const auto& [input, output] : pairs) {
                const std::optional<int> distance =
                    distanceAlongRay(mirror, cell, input);
                if (!distance ||
                    !clearStaticRay(level, mirror, input, 1, *distance - 1)) {
                    continue;
                }
                if (nearest.empty() || *distance < nearestDistance) {
                    nearest = { { mirror, output, *distance } };
                    nearestDistance = *distance;
                } else if (*distance == nearestDistance) {
                    nearest.push_back({ mirror, output, *distance });
                }
            }
        }
    }
    return nearest;
}

std::optional<GridPosition3> relaxedMirrorDestination(
    const Level& level,
    GridPosition3 start,
    const std::vector<bool>& traversable,
    const auto& cellIndex)
{
    GridPosition3 current = start;
    std::vector<GridPosition3> used;
    const std::size_t maximumChain =
        static_cast<std::size_t>(level.width()) * level.height();
    while (used.size() < maximumChain) {
        const std::vector<MirrorHit> hits =
            nearestMirrors(level, current, used);
        if (hits.empty()) {
            if (used.empty() || !traversable[cellIndex(current)]) {
                return std::nullopt;
            }
            return current;
        }
        // Movable units do not branch when two mirrors are equally near; the
        // production activation rejects the whole transaction in that case.
        if (hits.size() != 1) {
            return std::nullopt;
        }
        const MirrorHit& hit = hits.front();
        if (!clearStaticRay(
                level, hit.cell, hit.output, 1, hit.distance)) {
            return std::nullopt;
        }
        used.push_back(hit.cell);
        current = offset(hit.cell, hit.output, hit.distance);
    }
    return std::nullopt;
}

int minimumAssignment(const std::vector<std::vector<int>>& costs)
{
    const std::size_t rows = costs.size();
    if (rows == 0) {
        return 0;
    }
    const std::size_t columns = costs.front().size();
    if (columns < rows) {
        return unreachable;
    }

    // Rectangular Hungarian algorithm: every End row receives a distinct
    // living hero column, while surplus heroes remain unused.
    std::vector<int> rowPotential(rows + 1);
    std::vector<int> columnPotential(columns + 1);
    std::vector<std::size_t> columnMatch(columns + 1);
    std::vector<std::size_t> previous(columns + 1);
    for (std::size_t row = 1; row <= rows; ++row) {
        columnMatch[0] = row;
        std::size_t column = 0;
        std::vector<int> minimum(columns + 1, unreachable);
        std::vector<bool> used(columns + 1);
        do {
            used[column] = true;
            const std::size_t matchedRow = columnMatch[column];
            int delta = unreachable;
            std::size_t nextColumn = 0;
            for (std::size_t candidate = 1;
                 candidate <= columns;
                 ++candidate) {
                if (used[candidate]) {
                    continue;
                }
                const int reduced = costs[matchedRow - 1][candidate - 1] -
                    rowPotential[matchedRow] - columnPotential[candidate];
                if (reduced < minimum[candidate]) {
                    minimum[candidate] = reduced;
                    previous[candidate] = column;
                }
                if (minimum[candidate] < delta) {
                    delta = minimum[candidate];
                    nextColumn = candidate;
                }
            }
            for (std::size_t candidate = 0;
                 candidate <= columns;
                 ++candidate) {
                if (used[candidate]) {
                    rowPotential[columnMatch[candidate]] += delta;
                    columnPotential[candidate] -= delta;
                } else {
                    minimum[candidate] -= delta;
                }
            }
            column = nextColumn;
        } while (columnMatch[column] != 0);
        do {
            const std::size_t next = previous[column];
            columnMatch[column] = columnMatch[next];
            column = next;
        } while (column != 0);
    }
    return -columnPotential[0];
}

} // namespace

RelaxedHeuristic::RelaxedHeuristic(const Level& level)
    : level_(level)
{
    cellCount_ = static_cast<std::size_t>(level.width()) * level.height() *
        (static_cast<std::size_t>(level.depth()) + 1);
    unreachableCost_ = static_cast<int>(cellCount_) + 64;
    traversable_.resize(cellCount_);
    for (int z = 0; z <= static_cast<int>(level.depth()); ++z) {
        for (int y = 0; y < static_cast<int>(level.height()); ++y) {
            for (int x = 0; x < static_cast<int>(level.width()); ++x) {
                const GridPosition3 cell { x, y, z };
                const std::optional<TileType> support =
                    level.supportingTileAt(cell);
                traversable_[index(cell)] =
                    rules::staticCellAllowsEntity(level, cell) && support &&
                    (tileTypeSupportsEntity(*support) ||
                        *support == TileType::Water);
                traversableCellCount_ += traversable_[index(cell)] ? 1 : 0;
            }
        }
    }

    // A platform can hold a hero above any of its stops and carry it from one
    // to another. Optimistically every stop is reachable from every other in
    // one move, whatever the platform's phase or what blocks its route.
    std::vector<std::vector<std::size_t>> platformRiderCells;
    for (const Level::Elevator& elevator : level.elevators()) {
        std::vector<std::size_t>& riders = platformRiderCells.emplace_back();
        for (const int stop : elevator.levels) {
            const GridPosition3 rider {
                elevator.cell.x, elevator.cell.y, stop + 1,
            };
            if (!inRange(rider) ||
                !rules::staticCellAllowsEntity(level, rider)) {
                continue;
            }
            if (!traversable_[index(rider)]) {
                traversable_[index(rider)] = true;
                ++traversableCellCount_;
            }
            riders.push_back(index(rider));
        }
    }
    for (const Level::MinecartRoute& route : level.minecartRoutes()) {
        std::vector<std::size_t>& riders = platformRiderCells.emplace_back();
        for (const GridPosition3 stop : route.stops) {
            // Carts are occupiable rail cells. Keep the legacy cell above the
            // cart too so older levels remain admissible to the heuristic.
            for (const GridPosition3 rider : {
                     stop,
                     GridPosition3 { stop.x, stop.y, stop.z + 1 },
                 }) {
                if (!inRange(rider) ||
                    !rules::staticCellAllowsEntity(level, rider)) {
                    continue;
                }
                if (!traversable_[index(rider)]) {
                    traversable_[index(rider)] = true;
                    ++traversableCellCount_;
                }
                riders.push_back(index(rider));
            }
        }
    }

    std::vector<std::vector<std::size_t>> edges(cellCount_);
    for (const std::vector<std::size_t>& riders : platformRiderCells) {
        for (const std::size_t from : riders) {
            for (const std::size_t to : riders) {
                if (from != to) {
                    edges[from].push_back(to);
                }
            }
        }
    }
    for (int z = 0; z <= static_cast<int>(level.depth()); ++z) {
        for (int y = 0; y < static_cast<int>(level.height()); ++y) {
            for (int x = 0; x < static_cast<int>(level.width()); ++x) {
                const GridPosition3 source { x, y, z };
                if (!traversable_[index(source)]) {
                    continue;
                }
                for (const MoveDirection direction : directions) {
                    const GridPosition delta =
                        rules::directionOffset(direction);
                    const GridPosition3 destination = offset(source, delta);
                    if (inRange(destination) &&
                        traversable_[index(destination)]) {
                        edges[index(source)].push_back(index(destination));
                    }
                }
                const std::optional<GridPosition3> reflected =
                    relaxedMirrorDestination(
                        level,
                        source,
                        traversable_,
                        [&](GridPosition3 cell) { return index(cell); });
                if (reflected && *reflected != source &&
                    inRange(*reflected)) {
                    edges[index(source)].push_back(index(*reflected));
                    ++mirrorEdgeCount_;
                }
            }
        }
    }

    std::vector<std::vector<std::size_t>> reverseEdges(cellCount_);
    for (std::size_t source = 0; source < edges.size(); ++source) {
        std::ranges::sort(edges[source]);
        const auto uniqueEnd =
            std::ranges::unique(edges[source]).begin();
        edges[source].erase(uniqueEnd, edges[source].end());
        edgeCount_ += edges[source].size();
        for (const std::size_t destination : edges[source]) {
            reverseEdges[destination].push_back(source);
        }
    }

    distanceByEnd_.resize(level.ends().size());
    for (std::size_t end = 0; end < level.ends().size(); ++end) {
        std::vector<int>& distances = distanceByEnd_[end];
        distances.assign(cellCount_, unreachable);
        const GridPosition3 goal = level.ends()[end];
        if (!inRange(goal) || !traversable_[index(goal)]) {
            continue;
        }
        std::queue<std::size_t> pending;
        distances[index(goal)] = 0;
        pending.push(index(goal));
        while (!pending.empty()) {
            const std::size_t destination = pending.front();
            pending.pop();
            for (const std::size_t source : reverseEdges[destination]) {
                if (distances[source] != unreachable) {
                    continue;
                }
                distances[source] = distances[destination] + 1;
                pending.push(source);
            }
        }
    }
}

bool RelaxedHeuristic::inRange(GridPosition3 cell) const
{
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
        cell.x < static_cast<int>(level_.width()) &&
        cell.y < static_cast<int>(level_.height()) &&
        cell.z <= static_cast<int>(level_.depth());
}

std::size_t RelaxedHeuristic::index(GridPosition3 cell) const
{
    return (static_cast<std::size_t>(cell.z) * level_.height() +
               static_cast<std::size_t>(cell.y)) *
            level_.width() +
        static_cast<std::size_t>(cell.x);
}

int RelaxedHeuristic::distanceToEnd(
    std::size_t end, GridPosition3 cell) const
{
    if (!inRange(cell)) {
        return unreachableCost_;
    }
    const int distance = distanceByEnd_[end][index(cell)];
    return distance == unreachable ? unreachableCost_ : distance;
}

int RelaxedHeuristic::estimate(const GameState& state) const
{
    std::vector<GridPosition3> heroes;
    for (const GameState::Player& player : state.players) {
        if (!player.dead) {
            heroes.push_back(player.cell);
        }
    }
    std::vector<std::vector<int>> costs(
        level_.ends().size(),
        std::vector<int>(heroes.size(), unreachableCost_));
    for (std::size_t end = 0; end < level_.ends().size(); ++end) {
        for (std::size_t hero = 0; hero < heroes.size(); ++hero) {
            costs[end][hero] = distanceToEnd(end, heroes[hero]);
        }
    }

    const int missing = static_cast<int>(level_.ends().size()) -
        static_cast<int>(heroes.size());
    int cost = 8 * std::abs(missing);
    if (missing <= 0) {
        const int assignment = minimumAssignment(costs);
        cost += assignment == unreachable ? unreachableCost_ : assignment;
    } else {
        // Mirrors can create the missing heroes. Until then, allow the same
        // existing hero to stand in for multiple eventual copies so the
        // estimate remains useful without declaring the position impossible.
        for (std::size_t end = 0; end < level_.ends().size(); ++end) {
            int nearest = unreachableCost_;
            for (std::size_t hero = 0; hero < heroes.size(); ++hero) {
                nearest = std::min(nearest, costs[end][hero]);
            }
            cost += nearest;
        }
    }
    return cost;
}

} // namespace sokoban::solver::detail
