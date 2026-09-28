#pragma once

#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <vector>

namespace sokoban::solver::detail {

// Conservative Sokoban dead-position analysis. Unit-count feasibility applies
// to every feature set. Static cell analysis is enabled only when movable
// units change cells through ordinary pushes; levels containing pulls,
// teleports, aura movement, mirrors, ice, conveyors, ladders, water, enemies,
// or drops deliberately skip that stronger proof.
class DeadPositionIndex {
public:
    explicit DeadPositionIndex(const Level& level);

    [[nodiscard]] bool applicable() const
    {
        return !level_.pressurePlates().empty();
    }
    [[nodiscard]] bool staticAnalysisEnabled() const { return enabled_; }
    [[nodiscard]] std::size_t deadCellCount() const { return deadCellCount_; }
    [[nodiscard]] bool isDeadCell(GridPosition3 cell) const;

    // True only when the remaining live movable units cannot cover every
    // pressure plate. Static analysis requires a complete rock-to-plate
    // matching when enabled; the basic fallen/dead-unit cardinality proof
    // applies to every feature set. Extra, intentionally unused rocks remain
    // harmless.
    [[nodiscard]] bool rejects(const GameState& state) const;

private:
    [[nodiscard]] bool inRange(GridPosition3 cell) const;
    [[nodiscard]] std::size_t index(GridPosition3 cell) const;

    const Level& level_;
    bool enabled_ = false;
    std::size_t deadCellCount_ = 0;
    std::vector<bool> supported_;
    std::vector<bool> canReachPlate_;
    std::vector<std::vector<bool>> reachableByPlate_;
};

} // namespace sokoban::solver::detail
