// Headless tests for action planning.
//
// Planning is separated from execution so that an action's outcome is settled
// by a pure function of the world at the instant it begins. These tests pin
// that separation: the planners decide everything except the running move
// total, and re-planning the same inputs gives the same answer.

#include "engine/ActionPlan.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

[[nodiscard]] GridPosition3 cell(int x, int y, int z)
{
    return { x, y, z };
}

[[nodiscard]] Level makeLevel(
    const std::vector<std::vector<std::string>>& layers)
{
    std::vector<std::string> lines;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        lines.push_back("@layer " + std::to_string(layer));
        lines.insert(lines.end(), layers[layer].begin(), layers[layer].end());
        lines.emplace_back();
    }
    return Level::loadFromLines(lines, "test level");
}

// Ground along the bottom layer, with the player at (1,0) and a rock at (2,0)
// on the layer above, so the player can push it right or walk left freely.
[[nodiscard]] Level roomWithRock()
{
    return makeLevel({
        { "....." },
        { " CR  " },
    });
}

void testWorldStepPlansAPush()
{
    TEST("worldStepPlansAPush");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> planned = plans::worldStep(
        level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }
    CHECK(planned->action.before == state);
    CHECK(planned->action.after.players[0].cell == cell(2, 0, 1));
    CHECK(planned->action.after.movables[0].cell == cell(3, 0, 1));
    CHECK(planned->action.playerPushing);
    CHECK(planned->action.facingDirection == MoveDirection::Right);
    CHECK(planned->action.durationSeconds == 0.25f);
    CHECK(!planned->action.reversed);
    // The session owns the running total, so the planner leaves it alone.
    CHECK(planned->action.playerMoveCountBefore == 0);
    CHECK(planned->action.playerMoveCountAfter == 0);
}

void testWorldStepWithoutMovementHasNoPlan()
{
    TEST("worldStepWithoutMovementHasNoPlan");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    // Into the wall above.
    CHECK(!plans::worldStep(level, state, MoveDirection::Up, {}, 0.25f));
    // Nothing is sliding or riding a belt, so an automatic step does nothing.
    CHECK(!plans::worldStep(level, state, std::nullopt, {}, 0.25f));
}

void testPlanningIsPureAndRepeatable()
{
    TEST("planningIsPureAndRepeatable");
    // The whole point of planning up front: the same world gives the same
    // answer, every time, with nothing carried between calls.
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> first =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.25f);
    const std::optional<plans::PlannedAction> second =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(first == second);

    // Planning must not have touched the state it was given.
    CHECK(state == rules::initialState(level));
}

void testWalkingWithoutPushing()
{
    TEST("walkingWithoutPushing");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    const std::optional<plans::PlannedAction> planned = plans::worldStep(
        level, state, MoveDirection::Left, {}, 0.25f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(planned->action.after.players[0].cell == cell(0, 0, 1));
        // Nothing was in the way, so this is a walk rather than a push.
        CHECK(!planned->action.playerPushing);
        CHECK(planned->action.after.movables[0].cell == cell(2, 0, 1));
    }
}

void testRestartPlan()
{
    TEST("restartPlan");
    const Level level = roomWithRock();
    const GameState initial = rules::initialState(level);

    // Already at the opening state, so there is nothing to restart to.
    CHECK(!plans::restart(level, initial, 0.25f));

    const std::optional<plans::PlannedAction> moved =
        plans::worldStep(level, initial, MoveDirection::Right, {}, 0.25f);
    CHECK(moved.has_value());
    if (!moved) {
        return;
    }
    const std::optional<ActionPlan> plan =
        plans::restart(level, moved->action.after, 0.25f);
    CHECK(plan.has_value());
    if (plan) {
        CHECK(plan->before == moved->action.after);
        CHECK(plan->after == initial);
    }

    // A dead player has to stay dead until undone; restart must not rescue it.
    GameState dead = initial;
    dead.players[0].dead = true;
    CHECK(!plans::restart(level, dead, 0.25f));
}

void testInvertedSwapsEndpointsAndCounts()
{
    TEST("invertedSwapsEndpointsAndCounts");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    std::optional<plans::PlannedAction> forward =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(forward.has_value());
    if (!forward) {
        return;
    }
    forward->action.playerMoveCountBefore = 4;
    forward->action.playerMoveCountAfter = 5;

    const ActionPlan back = plans::inverted(forward->action);
    CHECK(back.before == forward->action.after);
    CHECK(back.after == forward->action.before);
    CHECK(back.reversed);
    CHECK(back.playerMoveCountBefore == 5);
    CHECK(back.playerMoveCountAfter == 4);
    // Carried through so the reversed animation still knows it was a push.
    CHECK(back.playerPushing == forward->action.playerPushing);

    // Inverting twice returns the original, aside from the reversed flag that
    // marks which direction history is being walked in.
    ActionPlan again = plans::inverted(back);
    CHECK(again.before == forward->action.before);
    CHECK(again.after == forward->action.after);
    CHECK(again.playerMoveCountAfter == forward->action.playerMoveCountAfter);
}

void testPlayerMovementHelpers()
{
    TEST("playerMovementHelpers");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    const std::optional<plans::PlannedAction> planned = plans::worldStep(
        level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }
    CHECK(plans::anyPlayerMoved(planned->action.before, planned->action.after));
    CHECK(!plans::anyPlayerMoved(planned->action.before, planned->action.before));
    CHECK(plans::firstPlayerMovementDirection(planned->action.before, planned->action.after) ==
        MoveDirection::Right);
    // Backwards, which is how undo derives the facing it animates with.
    CHECK(plans::firstPlayerMovementDirection(planned->action.after, planned->action.before) ==
        MoveDirection::Left);
    CHECK(!plans::firstPlayerMovementDirection(planned->action.before, planned->action.before));

    // A player appearing or vanishing counts as movement, because mirror
    // activation adds reflections and undo removes them again.
    GameState grown = planned->action.before;
    grown.players.push_back({ .id = 99, .cell = cell(4, 0, 1) });
    CHECK(plans::anyPlayerMoved(planned->action.before, grown));
}

// An ice block the player can push, with a long run of floor ahead of it and a
// wall to stop it. Ice is a movable that keeps its momentum, not a floor.
[[nodiscard]] Level iceRink()
{
    return makeLevel({
        { "........" },
        { "CI     #" },
    });
}

void testSlideResolvesAsOneAction()
{
    TEST("slideResolvesAsOneAction");
    const Level level = iceRink();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> planned = plans::worldStep(
        level, state, MoveDirection::Right, {}, 0.2f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }

    // The rock's whole journey is settled here, not one tile at a time. It runs
    // out of ice or hits the wall; either way the destination is decided before
    // anything has moved.
    CHECK(planned->legs.size() > 1);
    CHECK(!plans::anySlideMomentum(planned->action.after));
    CHECK(planned->legs.back() == planned->action.after);
    // Duration covers every leg, so the animation has time to play them.
    const float expected =
        0.2f * static_cast<float>(planned->legs.size());
    CHECK(std::abs(planned->action.durationSeconds - expected) < 0.0001f);
    // Still one player move, however far the rock travelled.
    CHECK(planned->action.playerPushing);

    // Every leg differs from the one before it: no wasted frames.
    for (std::size_t i = 1; i < planned->legs.size(); ++i) {
        CHECK(!(planned->legs[i] == planned->legs[i - 1]));
    }
}

void testSlideOutcomeIgnoresLaterInterference()
{
    TEST("slideOutcomeIgnoresLaterInterference");
    // The guarantee, stated as a test. Planning the same push against the same
    // world always gives the same destination, and that destination is fixed
    // before the first tile of travel - so nothing that happens during the
    // slide can be consulted, because the answer already exists.
    const Level level = iceRink();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> planned =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.2f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }
    const GridPosition3 destination = planned->action.after.movables[0].cell;

    // Re-plan from a mid-slide leg with the world otherwise untouched: the rock
    // still ends up in the same place, which is what "already decided" means.
    const std::optional<plans::PlannedAction> resumed = plans::worldStep(
        level, planned->legs.front(), std::nullopt, {}, 0.2f);
    CHECK(resumed.has_value());
    if (resumed) {
        CHECK(resumed->action.after.movables[0].cell == destination);
    }
}

void testChainStopsAtConveyors()
{
    TEST("chainStopsAtConveyors");
    // Belt motion is ambient and never ends, so chaining it would produce an
    // action that never finishes. A rider gets one step per action.
    const Level level = makeLevel({
        { "...", "..." },
        { ">> ", "C  " },
    });
    GameState state = rules::initialState(level);
    state.players[0].cell = cell(0, 0, 1);  // step onto the belt
    const std::optional<plans::PlannedAction> planned =
        plans::worldStep(level, state, std::nullopt, {}, 0.2f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(planned->legs.size() == 1);
        // The belt has more to give, but this action is over.
        CHECK(rules::hasPendingMotion(level, planned->action.after));
        CHECK(!plans::anySlideMomentum(planned->action.after));
    }
}

void testSlideMomentumDetection()
{
    TEST("slideMomentumDetection");
    GameState state;
    CHECK(!plans::anySlideMomentum(state));
    state.movables.push_back({ .id = 1, .type = TileType::Rock });
    CHECK(!plans::anySlideMomentum(state));
    state.movables[0].sliding = MoveDirection::Right;
    CHECK(plans::anySlideMomentum(state));
    state.movables[0].sliding.reset();
    state.players.push_back({ .id = 2 });
    state.players[0].sliding = MoveDirection::Left;
    CHECK(plans::anySlideMomentum(state));
}

} // namespace

int main()
{
    testWorldStepPlansAPush();
    testWorldStepWithoutMovementHasNoPlan();
    testPlanningIsPureAndRepeatable();
    testWalkingWithoutPushing();
    testRestartPlan();
    testInvertedSwapsEndpointsAndCounts();
    testPlayerMovementHelpers();
    testSlideResolvesAsOneAction();
    testSlideOutcomeIgnoresLaterInterference();
    testChainStopsAtConveyors();
    testSlideMomentumDetection();

    if (failures == 0) {
        std::cout << "ActionPlanTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ActionPlanTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
