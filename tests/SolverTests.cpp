#include "TestHarness.hpp"

#include "engine/Level.hpp"
#include "engine/Solution.hpp"
#include "engine/solver/Solver.hpp"
#include "engine/solver/StateKey.hpp"

#include <exception>
#include <iostream>
#include <unordered_set>

namespace {

using namespace sokoban;

const Level::Definition pushAndPlateDefinition {
    .layers = {
        { ".....", "....." },
        { "C RPE", "     " },
    },
};

void testPackedStateKeyIncludesEveryDynamicField()
{
    TEST("packedStateKeyIncludesEveryDynamicField");
    GameState state {
        .players = { {
            .id = 11,
            .cell = { -2, 3, 4 },
            .character = CharacterType::Knight,
            .controller = 12,
            .sliding = MoveDirection::Left,
        } },
        .movables = { {
            .id = 21,
            .type = TileType::TurretNorth,
            .cell = { 5, -6, 7 },
            .sliding = MoveDirection::Right,
        } },
        .enemies = { {
            .id = 31,
            .cell = { 8, 9, -10 },
            .sliding = MoveDirection::Down,
        } },
    };
    constexpr EntityId activeController = 12;
    const auto key =
        solver::detail::makePackedStateKey(state, activeController);
    CHECK(key.wordCount() == 14);
    CHECK(key == solver::detail::makePackedStateKey(
        state, activeController));

    std::unordered_set<
        solver::detail::PackedStateKey,
        solver::detail::PackedStateKeyHash> keys;
    keys.emplace(key);
    CHECK(keys.contains(solver::detail::makePackedStateKey(
        state, activeController)));

    const auto checkChanged = [&](const GameState& changed) {
        CHECK(key != solver::detail::makePackedStateKey(
            changed, activeController));
    };
    CHECK(key != solver::detail::makePackedStateKey(
        state, activeController + 1));

    GameState changed = state;
    ++changed.players[0].id;
    checkChanged(changed);
    changed = state;
    ++changed.players[0].cell.x;
    checkChanged(changed);
    changed = state;
    ++changed.players[0].cell.y;
    checkChanged(changed);
    changed = state;
    ++changed.players[0].cell.z;
    checkChanged(changed);
    changed = state;
    changed.players[0].character = CharacterType::Witch;
    checkChanged(changed);
    changed = state;
    ++changed.players[0].controller;
    checkChanged(changed);
    changed = state;
    changed.players[0].dead = true;
    checkChanged(changed);
    changed = state;
    changed.players[0].drowned = true;
    checkChanged(changed);
    changed = state;
    changed.players[0].sliding = MoveDirection::Up;
    checkChanged(changed);

    changed = state;
    ++changed.movables[0].id;
    checkChanged(changed);
    changed = state;
    changed.movables[0].type = TileType::Rock;
    checkChanged(changed);
    changed = state;
    ++changed.movables[0].cell.x;
    checkChanged(changed);
    changed = state;
    ++changed.movables[0].cell.y;
    checkChanged(changed);
    changed = state;
    ++changed.movables[0].cell.z;
    checkChanged(changed);
    changed = state;
    changed.movables[0].fallen = true;
    checkChanged(changed);
    changed = state;
    changed.movables[0].dead = true;
    checkChanged(changed);
    changed = state;
    changed.movables[0].sliding = MoveDirection::Up;
    checkChanged(changed);

    changed = state;
    ++changed.enemies[0].id;
    checkChanged(changed);
    changed = state;
    ++changed.enemies[0].cell.x;
    checkChanged(changed);
    changed = state;
    ++changed.enemies[0].cell.y;
    checkChanged(changed);
    changed = state;
    ++changed.enemies[0].cell.z;
    checkChanged(changed);
    changed = state;
    changed.enemies[0].fallen = true;
    checkChanged(changed);
    changed = state;
    changed.enemies[0].dead = true;
    checkChanged(changed);
    changed = state;
    changed.enemies[0].sliding = MoveDirection::Up;
    checkChanged(changed);

    changed = state;
    changed.players.push_back(state.players[0]);
    checkChanged(changed);
    changed = state;
    changed.movables.push_back(state.movables[0]);
    checkChanged(changed);
    changed = state;
    changed.enemies.push_back(state.enemies[0]);
    checkChanged(changed);
}

void testSearchResultReplays()
{
    TEST("searchResultReplays");
    const Level level =
        Level::loadFromDefinition(pushAndPlateDefinition, "solver test");
    const solver::Result result = solver::solve(level, {
        .maxStates = 10'000,
    });

    CHECK(result.status == solver::Status::Solved);
    CHECK(result.solved());
    CHECK(!result.inputs.empty());
    CHECK_MESSAGE(
        solution::record(
            level, pushAndPlateDefinition, result.inputs, "solver test").solved,
        "solver output must replay through the recording driver");
    CHECK(result.statistics.generatedStates > 1);
    CHECK(result.statistics.expandedPositions > 0);
    CHECK(result.statistics.frontierPops > 0);
    CHECK(result.statistics.walkStates > 0);
    CHECK(result.statistics.significantMovesDiscovered > 0);
    CHECK(result.statistics.significantMovesTried > 0);
    CHECK(result.statistics.peakFrontier > 0);
    CHECK(result.statistics.peakWalkRegion > 0);
    CHECK(result.statistics.canonicalizationFloods > 0);
    CHECK(result.statistics.canonicalizationWalkStates > 0);
    CHECK(result.statistics.peakCanonicalWalkRegion > 0);
}

void testExhaustionIsDistinctFromStateLimit()
{
    TEST("exhaustionIsDistinctFromStateLimit");
    const Level::Definition blockedDefinition {
        .layers = {
            { "..." },
            { "C#E" },
        },
    };
    const Level blocked =
        Level::loadFromDefinition(blockedDefinition, "blocked solver test");
    const solver::Result exhausted = solver::solve(blocked);
    CHECK(exhausted.status == solver::Status::Exhausted);
    CHECK(!exhausted.solved());
    CHECK(exhausted.statistics.generatedStates == 1);
    CHECK(exhausted.statistics.expandedPositions == 1);
    CHECK(exhausted.statistics.unchangedInputs > 0);

    const Level searchable =
        Level::loadFromDefinition(pushAndPlateDefinition, "limited solver test");
    const solver::Result zeroLimit = solver::solve(searchable, {
        .maxStates = 0,
    });
    CHECK(zeroLimit.status == solver::Status::StateLimitReached);
    CHECK(zeroLimit.statistics.generatedStates == 0);

    const solver::Result limited = solver::solve(searchable, {
        .maxStates = 1,
    });
    CHECK(limited.status == solver::Status::StateLimitReached);
    CHECK(limited.statistics.generatedStates == 1);
    CHECK(limited.statistics.frontierPops == 1);
    CHECK(limited.statistics.expandedPositions == 0);

    const solver::Result tightlyLimited = solver::solve(searchable, {
        .maxStates = 2,
    });
    CHECK(tightlyLimited.status == solver::Status::StateLimitReached);
    CHECK(tightlyLimited.statistics.generatedStates == 2);
}

void testBestFirstReportsHeuristicWork()
{
    TEST("bestFirstReportsHeuristicWork");
    const Level level =
        Level::loadFromDefinition(pushAndPlateDefinition, "best-first test");
    const solver::Result result = solver::solve(level, {
        .maxStates = 10'000,
        .strategy = solver::Strategy::BestFirst,
    });

    CHECK(result.solved());
    CHECK(result.statistics.heuristicEvaluations > 0);
    CHECK(result.statistics.bestHeuristic.has_value());
}

void testWalkingVariantsAreCanonicalizedBeforeEnqueue()
{
    TEST("walkingVariantsAreCanonicalizedBeforeEnqueue");
    const Level::Definition cyclicDefinition {
        .layers = {
            {
                ".....",
                ".....",
                ".....",
                ".....",
                ".....",
            },
            {
                "#####",
                "#C  #",
                "# R #",
                "#   #",
                "#####",
            },
        },
    };
    const Level level =
        Level::loadFromDefinition(cyclicDefinition, "cyclic solver test");
    const solver::Result result = solver::solve(level, {
        .maxStates = 10'000,
    });

    CHECK(result.status == solver::Status::Exhausted);
    CHECK(result.statistics.generatedStates > 1);
    CHECK(result.statistics.expandedPositions ==
        result.statistics.frontierPops);
    CHECK(result.statistics.canonicalDuplicates > 0);
    CHECK(result.statistics.canonicalizationFloods >=
        result.statistics.generatedStates);
}

void testProgressCanCancelSearch()
{
    TEST("progressCanCancelSearch");
    const Level level =
        Level::loadFromDefinition(pushAndPlateDefinition, "cancel solver test");
    int callbacks = 0;
    solver::Progress observed;
    solver::Options options {
        .maxStates = 10'000,
        .progressInterval = 1,
        .progress = [&](const solver::Progress& progress) {
            ++callbacks;
            observed = progress;
            return false;
        },
    };
    const solver::Result result = solver::solve(level, options);

    CHECK(result.status == solver::Status::Cancelled);
    CHECK(callbacks == 1);
    CHECK(observed.statistics.generatedStates > 1);
    CHECK(observed.frontierSize > 0);
    CHECK(result.statistics == observed.statistics);
}

void testStatusNamesAreStable()
{
    TEST("statusNamesAreStable");
    CHECK(solver::statusName(solver::Status::Solved) == "solved");
    CHECK(solver::statusName(solver::Status::Exhausted) == "exhausted");
    CHECK(solver::statusName(solver::Status::StateLimitReached) ==
        "state limit reached");
    CHECK(solver::statusName(solver::Status::Cancelled) == "cancelled");
}

} // namespace

int main()
{
    try {
        testPackedStateKeyIncludesEveryDynamicField();
        testSearchResultReplays();
        testExhaustionIsDistinctFromStateLimit();
        testBestFirstReportsHeuristicWork();
        testWalkingVariantsAreCanonicalizedBeforeEnqueue();
        testProgressCanCancelSearch();
        testStatusNamesAreStable();
    } catch (const std::exception& error) {
        std::cerr << "SolverTests: unexpected exception: "
                  << error.what() << "\n";
        return 2;
    }
    if (failures == 0) {
        std::cout << "SolverTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SolverTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
