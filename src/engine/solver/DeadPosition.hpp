#pragma once

#include "engine/Level.hpp"
#include "engine/Rules.hpp"

#include <cstddef>
#include <vector>

namespace sokoban::solver::detail {

enum class DeadPositionReason {
    None,
    UnitCount,
    StaticMatching,
    FrozenCluster,
};

// Compatibility shell for the former pressure-plate dead-position analysis.
// Plates are no longer completion goals, and a plate needed to pass a Gate
// need not remain occupied afterward, so the old rock-to-plate proofs cannot
// safely reject search states. The type remains while solver statistics and
// diagnostics transition away from that optimization.
class DeadPositionIndex {
public:
    explicit DeadPositionIndex(const Level& level);

    [[nodiscard]] bool applicable() const
    {
        return false;
    }
    [[nodiscard]] bool staticAnalysisEnabled() const { return enabled_; }
    [[nodiscard]] bool multiRockFreezeAnalysisEnabled() const
    {
        return freezeAnalysisEnabled_;
    }
    [[nodiscard]] std::size_t deadCellCount() const { return deadCellCount_; }
    [[nodiscard]] bool isDeadCell(GridPosition3 cell) const;

    // Always None under Gate semantics; retained for API compatibility.
    [[nodiscard]] DeadPositionReason rejectionReason(
        const GameState& state) const;
    [[nodiscard]] bool rejects(const GameState& state) const
    {
        return rejectionReason(state) != DeadPositionReason::None;
    }

private:
    [[nodiscard]] bool inRange(GridPosition3 cell) const;
    [[nodiscard]] std::size_t index(GridPosition3 cell) const;
    [[nodiscard]] std::vector<bool> frozenInTwoByTwoBlocks(
        const std::vector<GridPosition3>& liveMovables) const;

    const Level& level_;
    bool enabled_ = false;
    bool freezeAnalysisEnabled_ = false;
    std::size_t deadCellCount_ = 0;
    std::vector<bool> supported_;
    std::vector<bool> canReachPlate_;
    std::vector<std::vector<bool>> reachableByPlate_;
};

} // namespace sokoban::solver::detail
