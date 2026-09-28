#pragma once

#include "engine/Solution.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace sokoban::solver {

enum class Strategy {
    BreadthFirst,
    BestFirst,
};

enum class Status {
    Solved,
    Exhausted,
    StateLimitReached,
    Cancelled,
};

// Search work is counted independently of elapsed time so results can be
// compared across machines and used later to grade generated levels.
struct Statistics {
    // Search nodes retained, including the initial state. This is the value
    // bounded by Options::maxStates.
    std::size_t generatedStates = 0;
    // Entries removed from the frontier. A pop can still be rejected by the
    // state limit.
    std::size_t frontierPops = 0;
    // Unique canonical positions whose significant successors were searched.
    std::size_t expandedPositions = 0;
    // Raw successor states rejected because they had already been observed.
    std::size_t queuedDuplicates = 0;
    // Successors rejected before enqueueing because their walking regions
    // canonicalized to a position already retained by the search.
    std::size_t canonicalDuplicates = 0;
    // Cost of canonicalizing successors before they consume frontier space.
    std::size_t canonicalizationFloods = 0;
    std::size_t canonicalizationWalkStates = 0;
    // Conservative pressure-plate feasibility pruning. Unit-count checks
    // apply everywhere; static dead cells are feature-gated to ordinary push
    // mechanics.
    bool staticDeadPositionAnalysisEnabled = false;
    std::size_t staticDeadCells = 0;
    std::size_t deadPositionChecks = 0;
    std::size_t deadPositionPrunes = 0;
    // Ordinary walking configurations visited while constructing macro moves.
    std::size_t walkStates = 0;
    // State-changing moves discovered during walking-region floods and then
    // actually simulated as candidate successors.
    std::size_t significantMovesDiscovered = 0;
    std::size_t significantMovesTried = 0;
    // Driver applications that could not settle within its safety limit.
    std::size_t settleFailures = 0;
    // Candidate inputs pruned because at least one hero died.
    std::size_t playerDeathPrunes = 0;
    // Directional inputs that settled without changing the state.
    std::size_t unchangedInputs = 0;
    std::size_t peakFrontier = 0;
    std::size_t peakWalkRegion = 0;
    std::size_t peakCanonicalWalkRegion = 0;
    std::size_t heuristicEvaluations = 0;
    std::optional<int> bestHeuristic;

    bool operator==(const Statistics&) const = default;
};

struct Progress {
    Statistics statistics;
    std::size_t frontierSize = 0;
};

// Return false to stop the search with Status::Cancelled. The callback runs
// after an expansion whenever at least progressInterval additional states have
// been generated.
using ProgressCallback = std::function<bool(const Progress&)>;

struct Options {
    std::size_t maxStates = 2'000'000;
    Strategy strategy = Strategy::BreadthFirst;
    std::size_t progressInterval = 0;
    ProgressCallback progress;
};

struct Result {
    Status status = Status::Exhausted;
    std::vector<solution::Input> inputs;
    Statistics statistics;

    [[nodiscard]] bool solved() const { return status == Status::Solved; }
};

[[nodiscard]] Result solve(const Level& level, const Options& options = {});
[[nodiscard]] std::string_view statusName(Status status);

} // namespace sokoban::solver
