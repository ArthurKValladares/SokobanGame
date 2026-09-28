#pragma once

#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <vector>

namespace sokoban::solver::detail {

// Optimistic feature-aware cost model used to guide best-first search. Rock
// movement is evaluated on an otherwise empty board, water may already be
// bridged, and mirror activations move one unit independently. Those relaxed
// assumptions keep the graph cheap while still representing authored floor
// geometry and mirror transport that Manhattan distance cannot see.
class RelaxedHeuristic {
public:
    explicit RelaxedHeuristic(const Level& level);

    [[nodiscard]] int estimate(const GameState& state) const;
    [[nodiscard]] std::size_t traversableCellCount() const
    {
        return traversableCellCount_;
    }
    [[nodiscard]] std::size_t edgeCount() const { return edgeCount_; }
    [[nodiscard]] std::size_t mirrorEdgeCount() const
    {
        return mirrorEdgeCount_;
    }

private:
    [[nodiscard]] bool inRange(GridPosition3 cell) const;
    [[nodiscard]] std::size_t index(GridPosition3 cell) const;
    [[nodiscard]] int distanceToPlate(
        std::size_t plate, GridPosition3 cell) const;

    const Level& level_;
    std::size_t cellCount_ = 0;
    std::size_t traversableCellCount_ = 0;
    std::size_t edgeCount_ = 0;
    std::size_t mirrorEdgeCount_ = 0;
    int unreachableCost_ = 0;
    std::vector<bool> traversable_;
    std::vector<std::vector<int>> distanceByPlate_;
};

} // namespace sokoban::solver::detail
