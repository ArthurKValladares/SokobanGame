// Headless tests for action planning.
//
// Planning is separated from execution so that an action's outcome is settled
// by a pure function of the world at the instant it begins. These tests pin
// that separation: the planners decide everything except the running move
// total, and re-planning the same inputs gives the same answer.

#include "engine/ActionPlan.hpp"

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

    const std::optional<ActionPlan> plan = plans::worldStep(
        level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(plan.has_value());
    if (!plan) {
        return;
    }
    CHECK(plan->before == state);
    CHECK(plan->after.players[0].cell == cell(2, 0, 1));
    CHECK(plan->after.movables[0].cell == cell(3, 0, 1));
    CHECK(plan->playerPushing);
    CHECK(plan->facingDirection == MoveDirection::Right);
    CHECK(plan->durationSeconds == 0.25f);
    CHECK(!plan->reversed);
    // The session owns the running total, so the planner leaves it alone.
    CHECK(plan->playerMoveCountBefore == 0);
    CHECK(plan->playerMoveCountAfter == 0);
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

    const std::optional<ActionPlan> first =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.25f);
    const std::optional<ActionPlan> second =
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
    const std::optional<ActionPlan> plan = plans::worldStep(
        level, state, MoveDirection::Left, {}, 0.25f);
    CHECK(plan.has_value());
    if (plan) {
        CHECK(plan->after.players[0].cell == cell(0, 0, 1));
        // Nothing was in the way, so this is a walk rather than a push.
        CHECK(!plan->playerPushing);
        CHECK(plan->after.movables[0].cell == cell(2, 0, 1));
    }
}

void testRestartPlan()
{
    TEST("restartPlan");
    const Level level = roomWithRock();
    const GameState initial = rules::initialState(level);

    // Already at the opening state, so there is nothing to restart to.
    CHECK(!plans::restart(level, initial, 0.25f));

    const std::optional<ActionPlan> moved =
        plans::worldStep(level, initial, MoveDirection::Right, {}, 0.25f);
    CHECK(moved.has_value());
    if (!moved) {
        return;
    }
    const std::optional<ActionPlan> plan =
        plans::restart(level, moved->after, 0.25f);
    CHECK(plan.has_value());
    if (plan) {
        CHECK(plan->before == moved->after);
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
    std::optional<ActionPlan> forward =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(forward.has_value());
    if (!forward) {
        return;
    }
    forward->playerMoveCountBefore = 4;
    forward->playerMoveCountAfter = 5;

    const ActionPlan back = plans::inverted(*forward);
    CHECK(back.before == forward->after);
    CHECK(back.after == forward->before);
    CHECK(back.reversed);
    CHECK(back.playerMoveCountBefore == 5);
    CHECK(back.playerMoveCountAfter == 4);
    // Carried through so the reversed animation still knows it was a push.
    CHECK(back.playerPushing == forward->playerPushing);

    // Inverting twice returns the original, aside from the reversed flag that
    // marks which direction history is being walked in.
    ActionPlan again = plans::inverted(back);
    CHECK(again.before == forward->before);
    CHECK(again.after == forward->after);
    CHECK(again.playerMoveCountAfter == forward->playerMoveCountAfter);
}

void testPlayerMovementHelpers()
{
    TEST("playerMovementHelpers");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    const std::optional<ActionPlan> plan = plans::worldStep(
        level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(plan.has_value());
    if (!plan) {
        return;
    }
    CHECK(plans::anyPlayerMoved(plan->before, plan->after));
    CHECK(!plans::anyPlayerMoved(plan->before, plan->before));
    CHECK(plans::firstPlayerMovementDirection(plan->before, plan->after) ==
        MoveDirection::Right);
    // Backwards, which is how undo derives the facing it animates with.
    CHECK(plans::firstPlayerMovementDirection(plan->after, plan->before) ==
        MoveDirection::Left);
    CHECK(!plans::firstPlayerMovementDirection(plan->before, plan->before));

    // A player appearing or vanishing counts as movement, because mirror
    // activation adds reflections and undo removes them again.
    GameState grown = plan->before;
    grown.players.push_back({ .id = 99, .cell = cell(4, 0, 1) });
    CHECK(plans::anyPlayerMoved(plan->before, grown));
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

    if (failures == 0) {
        std::cout << "ActionPlanTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ActionPlanTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
