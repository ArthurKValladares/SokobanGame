#include "TestHarness.hpp"

#include "engine/Level.hpp"
#include "engine/Solution.hpp"
#include "engine/solver/DeadPosition.hpp"
#include "engine/solver/Heuristic.hpp"
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

const Level::Definition classicDeadPositionDefinition {
    .layers = {
        {
            "......",
            "......",
            "......",
            "......",
            "......",
        },
        {
            "######",
            "#C   #",
            "# R P#",
            "#    #",
            "######",
        },
    },
};

const Level::Definition assignmentDeadlockDefinition {
    .layers = {
        {
            ".........",
            ".........",
            ".........",
            ".........",
            ".........",
        },
        {
            "#########",
            "#C RRP###",
            "#########",
            "#    P###",
            "#########",
        },
    },
};

const Level::Definition frozenClusterDefinition {
    .layers = {
        {
            ".........",
            ".........",
            ".........",
            ".........",
            ".........",
            ".........",
            ".........",
        },
        {
            "#########",
            "#Q      #",
            "#  RR   #",
            "#  RR   #",
            "#    PP #",
            "# E  PP #",
            "#########",
        },
    },
};

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

const Level::Definition mirrorHeuristicDefinition {
    .layers = {
        {
            ".......",
            ".......",
            ".......",
            ".......",
            ".......",
            ".......",
            ".......",
        },
        {
            "#######",
            "#  P  #",
            "#     #",
            "#R 1  #",
            "#     #",
            "#C  E #",
            "#######",
        },
    },
};

const Level::Definition assignmentHeuristicDefinition {
    .layers = {
        {
            ".........",
            ".........",
            ".........",
            ".........",
            ".........",
        },
        {
            "#########",
            "#PRP  R #",
            "#       #",
            "#C    E #",
            "#########",
        },
    },
};

void testRelaxedHeuristicUsesMirrorTransport()
{
    TEST("relaxedHeuristicUsesMirrorTransport");
    const Level level = Level::loadFromDefinition(
        mirrorHeuristicDefinition, "mirror heuristic test");
    const solver::detail::RelaxedHeuristic heuristic(level);

    CHECK(heuristic.traversableCellCount() > 0);
    CHECK(heuristic.edgeCount() > 0);
    CHECK(heuristic.mirrorEdgeCount() > 0);
    // The rock and plate are four Manhattan cells apart, but one mirror
    // activation moves the rock directly onto the plate.
    CHECK(heuristic.estimate(rules::initialState(level)) == 9);

    const Level assignmentLevel = Level::loadFromDefinition(
        assignmentHeuristicDefinition, "assignment heuristic test");
    const solver::detail::RelaxedHeuristic assignment(assignmentLevel);
    // The left rock is one push from either plate. A nearest-unit sum would
    // use it twice and return two; distinct assignment correctly reserves the
    // right rock for the second plate, for four pushes plus two goal bonuses.
    CHECK(assignment.estimate(rules::initialState(assignmentLevel)) == 20);
}

void testDeadPositionAnalysisIsConservativeAndFeatureAware()
{
    TEST("deadPositionAnalysisIsConservativeAndFeatureAware");
    const Level level = Level::loadFromDefinition(
        classicDeadPositionDefinition, "dead-position test");
    const solver::detail::DeadPositionIndex deadPositions(level);

    CHECK(deadPositions.staticAnalysisEnabled());
    CHECK(deadPositions.deadCellCount() > 0);
    CHECK(deadPositions.isDeadCell({ 1, 1, 1 }));
    CHECK(!deadPositions.isDeadCell({ 2, 2, 1 }));
    CHECK(!deadPositions.isDeadCell({ 4, 2, 1 }));

    GameState state = rules::initialState(level);
    CHECK(!deadPositions.rejects(state));
    state.movables[0].cell = { 1, 1, 1 };
    CHECK(deadPositions.rejects(state));

    // One unusable extra rock is harmless when another rock can still cover
    // the one plate.
    state.movables.push_back({
        .id = 99,
        .type = TileType::Rock,
        .cell = { 3, 2, 1 },
    });
    CHECK(!deadPositions.rejects(state));
    state.movables[1].cell = { 1, 1, 1 };
    state.movables[0].cell = { 4, 2, 1 };
    CHECK(!deadPositions.rejects(state));

    const Level assignmentLevel = Level::loadFromDefinition(
        assignmentDeadlockDefinition, "assignment deadlock test");
    const solver::detail::DeadPositionIndex assignmentDeadPositions(
        assignmentLevel);
    const GameState assignmentState = rules::initialState(assignmentLevel);
    CHECK(assignmentDeadPositions.staticAnalysisEnabled());
    CHECK(assignmentState.movables.size() ==
        assignmentLevel.pressurePlates().size());
    CHECK(!assignmentDeadPositions.isDeadCell(
        assignmentState.movables[0].cell));
    CHECK(!assignmentDeadPositions.isDeadCell(
        assignmentState.movables[1].cell));
    CHECK(assignmentDeadPositions.rejects(assignmentState));

    Level::Definition mirrored = classicDeadPositionDefinition;
    mirrored.layers[1][1][3] = '1';
    const Level mirroredLevel =
        Level::loadFromDefinition(mirrored, "mirror dead-position test");
    const solver::detail::DeadPositionIndex mirroredDeadPositions(
        mirroredLevel);
    CHECK(!mirroredDeadPositions.staticAnalysisEnabled());
    GameState mirroredState = rules::initialState(mirroredLevel);
    CHECK(!mirroredDeadPositions.rejects(mirroredState));
    mirroredState.movables[0].fallen = true;
    CHECK(mirroredDeadPositions.rejects(mirroredState));

    Level::Definition bard = classicDeadPositionDefinition;
    bard.layers[1][1][1] = 'B';
    CHECK(!solver::detail::DeadPositionIndex(
        Level::loadFromDefinition(bard, "bard dead-position test"))
        .staticAnalysisEnabled());

    Level::Definition drop = classicDeadPositionDefinition;
    drop.layers[0][3][3] = ' ';
    CHECK(!solver::detail::DeadPositionIndex(
        Level::loadFromDefinition(drop, "drop dead-position test"))
        .staticAnalysisEnabled());

    const Level frozenLevel = Level::loadFromDefinition(
        frozenClusterDefinition, "frozen-cluster dead-position test");
    const solver::detail::DeadPositionIndex frozenDeadPositions(
        frozenLevel);
    const GameState frozenState = rules::initialState(frozenLevel);
    CHECK(frozenDeadPositions.multiRockFreezeAnalysisEnabled());
    for (const GameState::Movable& movable : frozenState.movables) {
        CHECK(!frozenDeadPositions.isDeadCell(movable.cell));
    }
    CHECK(frozenDeadPositions.rejectionReason(frozenState) ==
        solver::detail::DeadPositionReason::FrozenCluster);

    // A sealed group is harmless when each immovable rock already occupies a
    // distinct plate.
    GameState covered = frozenState;
    CHECK(covered.movables.size() == frozenLevel.pressurePlates().size());
    for (std::size_t i = 0; i < covered.movables.size(); ++i) {
        covered.movables[i].cell = frozenLevel.pressurePlates()[i];
    }
    CHECK(frozenDeadPositions.rejectionReason(covered) ==
        solver::detail::DeadPositionReason::None);

    // Knights can move a row or column of rocks as a chain, invalidating the
    // no-first-move proof used for Rogue-only 2x2 clusters.
    Level::Definition knightCluster = frozenClusterDefinition;
    knightCluster.layers[1][1][1] = 'K';
    const Level knightClusterLevel = Level::loadFromDefinition(
        knightCluster, "knight frozen-cluster gate test");
    const solver::detail::DeadPositionIndex knightDeadPositions(
        knightClusterLevel);
    CHECK(knightDeadPositions.staticAnalysisEnabled());
    CHECK(!knightDeadPositions.multiRockFreezeAnalysisEnabled());
    CHECK(knightDeadPositions.rejectionReason(
            rules::initialState(knightClusterLevel)) ==
        solver::detail::DeadPositionReason::None);
}

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
    CHECK(result.statistics.precomputedSuccessorsReused > 0);
    CHECK(result.statistics.precomputedSuccessorsReused +
            result.statistics.drivenSuccessors ==
        result.statistics.significantMovesTried);
    CHECK(result.statistics.peakFrontier > 0);
    CHECK(result.statistics.peakWalkRegion > 0);
    CHECK(result.statistics.canonicalizationFloods > 0);
    CHECK(result.statistics.canonicalizationWalkStates > 0);
    CHECK(result.statistics.canonicalizationCachedStates > 0);
    CHECK(result.statistics.peakCanonicalWalkRegion > 0);
    CHECK(result.statistics.solutionSignificantMoves.has_value());
    CHECK(result.statistics.solutionSignificantMoves.value_or(
        result.inputs.size()) < result.inputs.size());
}

void testPrecomputedMirrorSuccessorReplays()
{
    TEST("precomputedMirrorSuccessorReplays");
    const Level level = Level::loadFromDefinition(
        mirrorHeuristicDefinition, "mirror successor test");
    const solver::Result result = solver::solve(level, {
        .maxStates = 10'000,
        .strategy = solver::Strategy::BestFirst,
    });

    CHECK(result.solved());
    CHECK(std::ranges::find(result.inputs, solution::Input::Interact) !=
        result.inputs.end());
    CHECK(result.statistics.precomputedSuccessorsReused > 0);
    CHECK_MESSAGE(
        solution::record(
            level,
            mirrorHeuristicDefinition,
            result.inputs,
            "mirror successor test").solved,
        "a reused mirror preview must match replay-driver semantics");
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
    CHECK(result.statistics.heuristicGraphCells > 0);
    CHECK(result.statistics.heuristicGraphEdges > 0);
}

void testWalkingVariantsAreCanonicalizedBeforeEnqueue()
{
    TEST("walkingVariantsAreCanonicalizedBeforeEnqueue");
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
    CHECK(result.statistics.canonicalizationCacheHits > 0);
    CHECK(result.statistics.canonicalizationCacheHits <=
        result.statistics.canonicalDuplicates);
    CHECK(result.statistics.canonicalizationCachedStates >=
        result.statistics.generatedStates);
    CHECK(result.statistics.peakCanonicalizationCachedStates >=
        result.statistics.canonicalizationCachedStates);
    CHECK(result.statistics.canonicalizationFloods ==
        result.statistics.generatedStates);
}

void testWalkingCacheIsBoundedAndOptional()
{
    TEST("walkingCacheIsBoundedAndOptional");
    const Level level = Level::loadFromDefinition(
        cyclicDefinition, "bounded walking cache test");
    const solver::Result bounded = solver::solve(level, {
        .maxStates = 10'000,
        .maxCachedWalkingStates = 8,
    });
    CHECK(bounded.status == solver::Status::Exhausted);
    CHECK(bounded.statistics.peakCanonicalizationCachedStates <= 8);
    CHECK(bounded.statistics.canonicalizationCacheRotations > 0);
    CHECK(bounded.statistics.canonicalizationCacheEvictions > 0);

    const solver::Result disabled = solver::solve(level, {
        .maxStates = 10'000,
        .maxCachedWalkingStates = 0,
    });
    CHECK(disabled.status == solver::Status::Exhausted);
    CHECK(disabled.statistics.generatedStates ==
        bounded.statistics.generatedStates);
    CHECK(disabled.statistics.expandedPositions ==
        bounded.statistics.expandedPositions);
    CHECK(disabled.statistics.canonicalizationCacheHits == 0);
    CHECK(disabled.statistics.canonicalizationCachedStates == 0);
    CHECK(disabled.statistics.peakCanonicalizationCachedStates == 0);
}

void testDeadPositionsArePrunedBeforeCanonicalization()
{
    TEST("deadPositionsArePrunedBeforeCanonicalization");
    const Level level = Level::loadFromDefinition(
        classicDeadPositionDefinition, "dead-position solver test");
    const solver::Result result = solver::solve(level, {
        .maxStates = 10'000,
    });

    CHECK(result.status == solver::Status::Exhausted);
    CHECK(result.statistics.staticDeadPositionAnalysisEnabled);
    CHECK(result.statistics.staticDeadCells > 0);
    CHECK(result.statistics.deadPositionChecks > 0);
    CHECK(result.statistics.deadPositionPrunes > 0);
    CHECK(result.statistics.deadPositionUnitCountPrunes +
            result.statistics.deadPositionStaticMatchingPrunes +
            result.statistics.deadPositionFrozenClusterPrunes ==
        result.statistics.deadPositionPrunes);

    const Level frozenLevel = Level::loadFromDefinition(
        frozenClusterDefinition, "frozen-cluster solver test");
    const solver::Result frozen = solver::solve(frozenLevel, {
        .maxStates = 10'000,
    });
    CHECK(frozen.status == solver::Status::Exhausted);
    CHECK(frozen.statistics.generatedStates == 1);
    CHECK(frozen.statistics.expandedPositions == 0);
    CHECK(frozen.statistics.multiRockFreezeAnalysisEnabled);
    CHECK(frozen.statistics.deadPositionPrunes == 1);
    CHECK(frozen.statistics.deadPositionFrozenClusterPrunes == 1);
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
        testRelaxedHeuristicUsesMirrorTransport();
        testDeadPositionAnalysisIsConservativeAndFeatureAware();
        testPackedStateKeyIncludesEveryDynamicField();
        testSearchResultReplays();
        testPrecomputedMirrorSuccessorReplays();
        testExhaustionIsDistinctFromStateLimit();
        testBestFirstReportsHeuristicWork();
        testWalkingVariantsAreCanonicalizedBeforeEnqueue();
        testWalkingCacheIsBoundedAndOptional();
        testDeadPositionsArePrunedBeforeCanonicalization();
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
