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
    // Movable mirrors can create transport edges anywhere; fixed authored
    // mirror edges cannot give an admissible estimate for these screens.
    CHECK(heuristic.mirrorEdgeCount() == 0);
    CHECK(heuristic.estimate(rules::initialState(level)) == 0);

    const Level assignmentLevel = Level::loadFromDefinition(
        assignmentHeuristicDefinition, "assignment heuristic test");
    const solver::detail::RelaxedHeuristic assignment(assignmentLevel);
    CHECK(assignment.estimate(rules::initialState(assignmentLevel)) == 5);
}

void testDeadPositionAnalysisIsConservativeAndFeatureAware()
{
    TEST("pressurePlateSwitchesDoNotBecomeSolverGoals");
    const Level level = Level::loadFromLayers({
        { "....." },
        { "C P E" },
    }, "plate switch solver test");
    const solver::detail::DeadPositionIndex deadPositions(level);
    CHECK(!deadPositions.applicable());
    CHECK(!deadPositions.staticAnalysisEnabled());
    CHECK(!deadPositions.rejects(rules::initialState(level)));

    const solver::Result result = solver::solve(level, {
        .maxStates = 1'000,
    });
    CHECK(result.solved());
    CHECK(result.statistics.deadPositionChecks == 0);
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
    changed.activeButtons = { { 1, 2, 3 } };
    checkChanged(changed);
    changed = state;
    changed.activeLevers = { { 1, 2, 3 } };
    checkChanged(changed);
    changed = state;
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
    changed.players[0].quarterTurns = 1;
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
    changed.movables[0].quarterTurns = 3;
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
    changed.enemies[0].quarterTurns = 2;
    checkChanged(changed);

    changed = state;
    changed.turnedMirrors.push_back({ .cell = { 1, 2, 3 }, .quarterTurns = 1 });
    checkChanged(changed);
    GameState turned = changed;
    turned.turnedMirrors[0].quarterTurns = 2;
    CHECK(solver::detail::makePackedStateKey(turned, activeController) !=
        solver::detail::makePackedStateKey(changed, activeController));
    turned = changed;
    ++turned.turnedMirrors[0].cell.x;
    CHECK(solver::detail::makePackedStateKey(turned, activeController) !=
        solver::detail::makePackedStateKey(changed, activeController));

    changed = state;
    changed.elevators.push_back({ .cell = { 1, 2, 0 }, .phase = 0 });
    checkChanged(changed);
    GameState moved = changed;
    moved.elevators[0].cell.z = 3;
    CHECK(solver::detail::makePackedStateKey(moved, activeController) !=
        solver::detail::makePackedStateKey(changed, activeController));
    moved = changed;
    moved.elevators[0].phase = 2;
    CHECK(solver::detail::makePackedStateKey(moved, activeController) !=
        solver::detail::makePackedStateKey(changed, activeController));

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

void testSolverRidesElevators()
{
    TEST("solverRidesElevators");
    // The End is on a ledge only the elevator reaches; the plate is on the
    // platform, so stepping onto it is the ride.
    const Level::Definition definition {
        .layers = {
            { "....=." },
            { "  C P." },
            { "     ." },
            { "     E" },
        },
        .elevators = { Level::Elevator {
            .cell = { 4, 0, 0 },
            .pressurePlates = { { 4, 0, 1 } },
            .levels = { 0, 2 },
        } },
    };
    const Level level = Level::loadFromDefinition(definition, "elevator solver");
    // The relaxed heuristic knows the elevator can carry a hero up.
    const solver::detail::RelaxedHeuristic heuristic(level);
    CHECK(heuristic.estimate(rules::initialState(level)) < 16);
    const solver::Result result = solver::solve(level, { .maxStates = 10'000 });
    CHECK(result.solved());
    CHECK_MESSAGE(
        solution::record(level, definition, result.inputs, "elevator solver")
            .solved,
        "elevator solution replays through the recording driver");
}

void testSolverActivatesButtonForAnotherHeroElevator()
{
    TEST("solverActivatesButtonForAnotherHeroElevator");
    const Level::Definition definition {
        .layers = { { "...=" }, { "QE K" }, { "    " }, { "   E" } },
        .plates = { { { 0, 0, 1 }, TileType::Button } },
        .elevators = { { .cell = { 3, 0, 0 },
                         .pressurePlates = { { 0, 0, 1 } },
                         .levels = { 0, 2 } } },
    };
    const Level level = Level::loadFromDefinition(definition, "button elevator solver");
    const auto result = solver::solve(level, { .maxStates = 10'000 });
    CHECK(result.solved());
    CHECK(std::ranges::find(result.inputs, solution::Input::Interact) != result.inputs.end());
    CHECK(solution::record(level, definition, result.inputs, "button elevator solver").solved);
}

void testSolverLatchesLeverToCrossGate()
{
    TEST("solverLatchesLeverToCrossGate");
    const Level::Definition definition {
        .layers = { { "...." }, { "Q GE" } },
        .gates = { { .cell = { 2, 0, 1 }, .pressurePlates = { { 0, 0, 1 } } } },
        .plates = { { { 0, 0, 1 }, TileType::LeverSouth } },
    };
    const Level level = Level::loadFromDefinition(definition, "lever gate solver");
    const auto result = solver::solve(level, { .maxStates = 10'000 });
    CHECK(result.solved());
    CHECK(std::ranges::find(result.inputs, solution::Input::Interact) != result.inputs.end());
    CHECK(solution::record(level, definition, result.inputs, "lever gate solver").solved);
}

void testSolverRidesMinecarts()
{
    TEST("solverRidesMinecarts");
    // One hero starts on the cart while the other can press its switch. The
    // rail span is not walkable without the cart, so the far End exercises
    // the minecart transport edge in both the heuristic and the real search.
    const Level::Definition definition {
        .layers = {
            { ".M--_..." },
            { " C  EPEC" },
        },
        .plates = {
            { .cell = { 1, 0, 0 }, .tile = TileType::RailStopEastWest },
        },
        .minecarts = { Level::Minecart {
            .cell = { 1, 0, 0 },
            .pressurePlates = { { 5, 0, 1 } },
            .initialDirection = 1,
        } },
    };
    const Level level = Level::loadFromDefinition(definition, "minecart solver");
    const solver::detail::RelaxedHeuristic heuristic(level);
    CHECK(heuristic.estimate(rules::initialState(level)) < 16);
    const solver::Result result = solver::solve(level, { .maxStates = 20'000 });
    CHECK(result.solved());
    CHECK_MESSAGE(
        solution::record(level, definition, result.inputs, "minecart solver")
            .solved,
        "minecart solution replays through the recording driver");
}

void testPrecomputedMirrorSuccessorReplays()
{
    TEST("precomputedMirrorSuccessorReplays");
    const Level::Definition mirrorCompletion {
        .layers = {
            { ".....", ".....", ".....", ".....", "....." },
            { "E 3  ", "     ", "  C  ", "     ", "  2 E" },
        },
    };
    // The two equidistant mirrors duplicate the only hero directly onto both
    // Ends, so Interact is required under the current completion rules.
    const Level level = Level::loadFromDefinition(
        mirrorCompletion, "mirror successor test");
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
            mirrorCompletion,
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

void testGateSwitchesRemainSearchable()
{
    TEST("gateSwitchesRemainSearchable");
    const Level level = Level::loadFromDefinition({
        .layers = {
            {
                ".......",
                ".......",
                ".......",
                ".......",
                ".......",
            },
            {
                "#######",
                "#  P  #",
                "#C RGE#",
                "#     #",
                "#######",
            },
        },
        .gates = { Level::Gate {
            .cell = { 4, 2, 1 },
            .pressurePlates = { { 3, 1, 1 } },
        } },
    }, "gate solver test");
    const solver::Result result = solver::solve(level, {
        .maxStates = 10'000,
    });

    CHECK(result.solved());
    CHECK(!result.statistics.staticDeadPositionAnalysisEnabled);
    CHECK(result.statistics.deadPositionChecks == 0);
    CHECK(result.statistics.deadPositionPrunes == 0);
}

void testProgressCanCancelSearch()
{
    TEST("progressCanCancelSearch");
    const Level level =
        Level::loadFromDefinition(cyclicDefinition, "cancel solver test");
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
        testSolverRidesElevators();
        testSolverActivatesButtonForAnotherHeroElevator();
        testSolverLatchesLeverToCrossGate();
        testSolverRidesMinecarts();
        testSearchResultReplays();
        testPrecomputedMirrorSuccessorReplays();
        testExhaustionIsDistinctFromStateLimit();
        testBestFirstReportsHeuristicWork();
        testWalkingVariantsAreCanonicalizedBeforeEnqueue();
        testWalkingCacheIsBoundedAndOptional();
        testGateSwitchesRemainSearchable();
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
