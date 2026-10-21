// Headless tests for action planning.
//
// Planning is separated from execution so that an action's outcome is settled
// by a pure function of the world at the instant it begins. These tests pin
// that separation: the planners decide everything except the running move
// total, and re-planning the same inputs gives the same answer.

#include "TestHarness.hpp"

#include "engine/ActionPlan.hpp"
#include "engine/Reservation.hpp"

#include <algorithm>
#include <ranges>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

[[nodiscard]] GridPosition3 cell(int x, int y, int z)
{
    return { x, y, z };
}

[[nodiscard]] Level makeLevel(
    const std::vector<std::vector<std::string>>& layers,
    CharacterType character = CharacterType::Rogue)
{
    return Level::loadFromDefinition(
        { .layers = layers, .character = character }, "test level");
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

void testPlayerStepPlansAPush()
{
    TEST("playerStepPlansAPush");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
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

void testPlayerStepWithoutMovementHasNoPlan()
{
    TEST("playerStepWithoutMovementHasNoPlan");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    // Into the wall above.
    CHECK(!plans::planPlayerStep(level, state, MoveDirection::Up, {}, 0.25f));
}

void testPlanningIsPureAndRepeatable()
{
    TEST("planningIsPureAndRepeatable");
    // The whole point of planning up front: the same world gives the same
    // answer, every time, with nothing carried between calls.
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> first =
        plans::planPlayerStep(level, state, MoveDirection::Right, {}, 0.25f);
    const std::optional<plans::PlannedAction> second =
        plans::planPlayerStep(level, state, MoveDirection::Right, {}, 0.25f);
    CHECK(first == second);

    // Planning must not have touched the state it was given.
    CHECK(state == rules::initialState(level));
}

void testWalkingWithoutPushing()
{
    TEST("walkingWithoutPushing");
    const Level level = roomWithRock();
    const GameState state = rules::initialState(level);
    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
        level, state, MoveDirection::Left, {}, 0.25f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(planned->action.after.players[0].cell == cell(0, 0, 1));
        // Nothing was in the way, so this is a walk rather than a push.
        CHECK(!planned->action.playerPushing);
        CHECK(planned->action.after.movables[0].cell == cell(2, 0, 1));
    }
}

void testKnightEnemyPushUsesThePushPresentation()
{
    TEST("knightEnemyPushUsesThePushPresentation");
    const Level level = makeLevel({
        { "..." },
        { "CN " },
    }, CharacterType::Knight);
    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
        level,
        rules::initialState(level),
        MoveDirection::Right,
        {},
        0.25f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(planned->action.playerPushing);
        CHECK(planned->action.after.enemies[0].cell == cell(2, 0, 1));
    }
}

void testDruidPullIsPlannedAsPulling()
{
    TEST("druidPullIsPlannedAsPulling");
    const Level level = makeLevel({
        { "...." },
        { "RU  " },
    });
    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
        level,
        rules::initialState(level),
        MoveDirection::Right,
        {},
        0.25f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(planned->action.playerPulling);
        CHECK(!planned->action.playerPushing);
        CHECK(planned->action.after.players[0].cell == cell(2, 0, 1));
        CHECK(planned->action.after.movables[0].cell == cell(1, 0, 1));
    }

    const Level enemyLevel = makeLevel({
        { "...." },
        { "NU  " },
    });
    const std::optional<plans::PlannedAction> enemyPlanned =
        plans::planPlayerStep(
            enemyLevel,
            rules::initialState(enemyLevel),
            MoveDirection::Right,
            {},
            0.25f);
    CHECK(enemyPlanned.has_value());
    if (enemyPlanned) {
        CHECK(enemyPlanned->action.playerPulling);
        CHECK(enemyPlanned->action.after.enemies[0].cell == cell(1, 0, 1));
    }
}

void testWitchSwapDoesNotUseThePushPresentation()
{
    TEST("witchSwapDoesNotUseThePushPresentation");
    const Level level = makeLevel({
        { ".." },
        { "HR" },
    });
    const std::optional<plans::PlannedAction> planned =
        plans::planPlayerStep(
            level,
            rules::initialState(level),
            MoveDirection::Right,
            {},
            0.25f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(!planned->action.playerPushing);
        CHECK(planned->action.after.players[0].cell == cell(1, 0, 1));
        CHECK(planned->action.after.movables[0].cell == cell(0, 0, 1));
    }


    const Level enemyLevel = makeLevel({
        { "...." },
        { "H N " },
    });
    const std::optional<plans::PlannedAction> enemyPlanned =
        plans::planPlayerStep(
            enemyLevel,
            rules::initialState(enemyLevel),
            MoveDirection::Right,
            {},
            0.25f);
    CHECK(enemyPlanned.has_value());
    if (enemyPlanned) {
        CHECK(!enemyPlanned->action.playerPushing);
        CHECK(enemyPlanned->action.after.players[0].cell == cell(2, 0, 1));
        CHECK(enemyPlanned->action.after.enemies[0].cell == cell(0, 0, 1));
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
        plans::planPlayerStep(level, initial, MoveDirection::Right, {}, 0.25f);
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
        plans::planPlayerStep(level, state, MoveDirection::Right, {}, 0.25f);
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
    // Carried through so a reversed effort animation keeps its type.
    CHECK(back.playerPushing == forward->action.playerPushing);
    CHECK(back.playerPulling == forward->action.playerPulling);

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
    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
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
// A level together with a state whose entities have been placed by hand.
struct Fixture {
    Level level;
    GameState state;
};

// Moves authored movables onto the cells a test needs them on.
//
// Why this is necessary at all: a movable's start tile ('R', 'I') resolves to
// Air, so "a rock standing on a conveyor" cannot be authored - the cell is
// either the rock or the belt, never both. Every belt scenario would otherwise
// have to be reached by pushing something on, which makes the setup longer than
// the test and drags the push's own mechanics into a test about belts.
//
// The result is the state that push would have produced, without the push. It
// is deliberately not validated: these are reachable configurations, and a test
// that wants an unreachable one is welcome to it.
void placeMovables(GameState& state, const std::vector<GridPosition3>& cells)
{
    for (std::size_t i = 0; i < cells.size() && i < state.movables.size(); ++i) {
        state.movables[i].cell = cells[i];
    }
}

// A belt running right along row 0 with two movables queued on its leftmost
// cells, and the player parked on row 1 clear of it.
//
// `movables[0]` is the follower at x=0; `movables[1]` is the leader at x=1.
[[nodiscard]] Fixture beltWithTwoRiders()
{
    Level level = makeLevel({
        { "........", "........" },
        { ">>>>>>>#", "CRR     " },
    });
    GameState state = rules::initialState(level);
    placeMovables(state, { cell(0, 0, 1), cell(1, 0, 1) });
    return { std::move(level), std::move(state) };
}

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
    const std::optional<plans::PlannedAction> pushed = plans::planPlayerStep(
        level, state, MoveDirection::Right, {}, 0.2f);
    CHECK(pushed.has_value());
    if (!pushed) {
        return;
    }
    const std::optional<plans::PlannedAction> planned = plans::planSlides(
        level,
        pushed->action.after,
        plans::slidingEntities(pushed->action.after),
        {},
        0.2f);
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
    // The separate slide does not take another player step or effort.
    CHECK(planned->action.after.players == pushed->action.after.players);
    CHECK(!planned->action.playerPushing);
    CHECK(!planned->action.playerPulling);
    CHECK(planned->action.playerMoveCountBefore == 0);
    CHECK(planned->action.playerMoveCountAfter == 0);
    CHECK(planned == plans::planSlides(
        level,
        pushed->action.after,
        plans::slidingEntities(pushed->action.after),
        {},
        0.2f));

    // Every leg differs from the one before it: no wasted frames.
    for (std::size_t i = 1; i < planned->legs.size(); ++i) {
        CHECK(!(planned->legs[i] == planned->legs[i - 1]));
    }
}

void testSlideOutcomeIgnoresLaterInterference()
{
    TEST("slideOutcomeIgnoresLaterInterference");
    // The slide's destination is fixed before its first tile of travel, so
    // nothing that happens during the slide can be consulted afterwards.
    const Level level = iceRink();
    const std::optional<plans::PlannedAction> pushed = plans::planPlayerStep(
        level, rules::initialState(level), MoveDirection::Right, {}, 0.2f);
    CHECK(pushed.has_value());
    if (!pushed) {
        return;
    }
    const std::optional<plans::PlannedAction> planned = plans::planSlides(
        level,
        pushed->action.after,
        plans::slidingEntities(pushed->action.after),
        {},
        0.2f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }
    const GridPosition3 destination = planned->action.after.movables[0].cell;

    // Re-plan from a mid-slide leg with the world otherwise untouched: the rock
    // still ends up in the same place, which is what "already decided" means.
    const std::optional<plans::PlannedAction> resumed = plans::planSlides(
        level,
        planned->legs.front(),
        plans::slidingEntities(planned->legs.front()),
        {},
        0.2f);
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
    const std::optional<plans::PlannedAction> planned = plans::planConveyorRides(
        level, state, plans::conveyorRiders(level, state), {}, 0.2f);
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
    state.players[0].sliding.reset();
    state.enemies.push_back({
        .id = 3,
        .sliding = MoveDirection::Right,
    });
    CHECK(plans::anySlideMomentum(state));
}

// Plans leave unrelated motion alone so actions can run concurrently.

void testPlayerStepIsOneStepAndLeavesTheSlideBehind()
{
    TEST("playerStepIsOneStepAndLeavesTheSlideBehind");
    const Level level = iceRink();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
        level, state, MoveDirection::Right, {}, 0.2f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }

    // One tile, one leg. The player is released after their own step instead of
    // being held for however far the block they pushed travels.
    CHECK(planned->legs.size() == 1);
    CHECK(planned->action.after.players[0].cell == cell(1, 0, 1));
    CHECK(planned->action.playerPushing);
    // The block is left mid-slide, for a separate plan to carry.
    CHECK(planned->action.after.movables[0].cell == cell(2, 0, 1));
    CHECK(planned->action.after.movables[0].sliding == MoveDirection::Right);
}

void testSlidePlanSettlesTheDestination()
{
    TEST("slidePlanSettlesTheDestination");
    const Level level = iceRink();
    const GameState pushed = plans::planPlayerStep(
        level, rules::initialState(level), MoveDirection::Right, {}, 0.2f)
                                ->action.after;

    const std::vector<EntityId> sliding = plans::slidingEntities(pushed);
    CHECK(sliding.size() == 1);
    if (sliding.empty()) {
        return;
    }

    const std::optional<plans::PlannedAction> slide =
        plans::planSlides(level, pushed, sliding, {}, 0.2f);
    CHECK(slide.has_value());
    if (!slide) {
        return;
    }

    // The whole journey, decided now. Nothing that happens while it travels can
    // move where it stops.
    CHECK(slide->legs.size() > 1);
    CHECK(!plans::anySlideMomentum(slide->action.after));
    CHECK(slide->action.after.movables[0].cell == cell(6, 0, 1));
    // The player is not this action's to move, so it must be untouched.
    CHECK(slide->action.after.players[0] == pushed.players[0]);
}

void testSlidePlanLeavesUnrelatedMomentumAlone()
{
    TEST("slidePlanLeavesUnrelatedMomentumAlone");
    const Level level = makeLevel({
        { "........", "........" },
        { "CI  #   ", " I     #" },
    });
    GameState state = rules::initialState(level);
    state.movables[0].sliding = MoveDirection::Right;
    state.movables[1].sliding = MoveDirection::Right;

    const std::optional<plans::PlannedAction> slide = plans::planSlides(
        level, state, { state.movables[0].id }, {}, 0.2f);
    CHECK(slide.has_value());
    if (slide) {
        CHECK(slide->action.after.movables[0].cell == cell(3, 0, 1));
        CHECK(!slide->action.after.movables[0].sliding);
        CHECK(slide->action.after.movables[1] == state.movables[1]);
        CHECK(slide->action.after.players == state.players);
        CHECK(plans::anySlideMomentum(slide->action.after));
        // The unrelated slider cannot keep this action looping after its own
        // rock has stopped. Every recorded leg belongs to the selected rock.
        CHECK(slide->legs.size() <
            static_cast<std::size_t>(plans::maxChainedSteps));
        for (const GameState& leg : slide->legs) {
            CHECK(leg.movables[1] == state.movables[1]);
        }
    }
}

void testPlayerStepLeavesAmbientRidersAlone()
{
    TEST("playerStepLeavesAmbientRidersAlone");
    const Level level = makeLevel({
        { ".....", "....." },
        { "C   R", " >   " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 1, 1); // onto the belt

    const std::optional<plans::PlannedAction> planned = plans::planPlayerStep(
        level, state, MoveDirection::Right, {}, 0.2f);
    CHECK(planned.has_value());
    if (planned) {
        CHECK(planned->action.after.players[0].cell == cell(1, 0, 1));
        CHECK(planned->action.after.movables[0] == state.movables[0]);
    }
}

void testConveyorRideIsOneStepPerAction()
{
    TEST("conveyorRideIsOneStepPerAction");
    const Level level = makeLevel({
        { ".....", "....." },
        { "C   R", " >>  " },
    });
    GameState state = rules::initialState(level);
    state.movables[0].cell = cell(1, 1, 1);

    const std::vector<EntityId> riders = plans::conveyorRiders(level, state);
    CHECK(riders.size() == 1);
    if (riders.empty()) {
        return;
    }

    const std::optional<plans::PlannedAction> ride =
        plans::planConveyorRides(level, state, riders, {}, 0.2f);
    CHECK(ride.has_value());
    if (ride) {
        // The belt has more to give, but this action is over: ambient motion
        // never terminates, so a chained ride would never end, and one-step
        // claims are what keep the area around a belt usable.
        CHECK(ride->legs.size() == 1);
        CHECK(ride->action.after.movables[0].cell == cell(2, 1, 1));
        CHECK(ride->action.after.players[0] == state.players[0]);
    }

    GameState enemyState = rules::initialState(level);
    enemyState.enemies.push_back({
        .id = 99,
        .cell = cell(1, 1, 1),
    });
    const std::vector<EntityId> enemyRiders =
        plans::conveyorRiders(level, enemyState);
    CHECK(enemyRiders.size() == 1);
    const std::optional<plans::PlannedAction> enemyRide =
        enemyRiders.empty()
        ? std::nullopt
        : plans::planConveyorRides(
              level, enemyState, enemyRiders, {}, 0.2f);
    CHECK(enemyRide.has_value());
    if (enemyRide) {
        CHECK(enemyRide->action.after.enemies[0].cell == cell(2, 1, 1));
    }
}

void testScopedPlannersRefuseEntitiesWithNothingToDo()
{
    TEST("scopedPlannersRefuseEntitiesWithNothingToDo");
    const Level level = iceRink();
    const GameState state = rules::initialState(level);

    // No momentum and no belt: nothing to plan, and saying so is how the
    // session's admit loop terminates.
    CHECK(plans::slidingEntities(state).empty());
    CHECK(plans::conveyorRiders(level, state).empty());
    CHECK(!plans::planSlides(level, state, { state.movables[0].id }, {}, 0.2f));
    CHECK(!plans::planConveyorRides(
        level, state, { state.movables[0].id }, {}, 0.2f));
    // Empty and invalid-only batches must never select the whole world.
    GameState moving = state;
    moving.movables[0].sliding = MoveDirection::Right;
    CHECK(plans::planSlides(
        level, moving, plans::slidingEntities(moving), {}, 0.2f).has_value());
    CHECK(!plans::planSlides(level, moving, {}, {}, 0.2f));
    CHECK(!plans::planSlides(level, moving, { invalidEntityId }, {}, 0.2f));

    const Fixture belt = beltWithTwoRiders();
    CHECK(plans::planConveyorRides(
        belt.level,
        belt.state,
        plans::conveyorRiders(belt.level, belt.state),
        {},
        0.2f).has_value());
    CHECK(!plans::planConveyorRides(belt.level, belt.state, {}, {}, 0.2f));
    CHECK(!plans::planConveyorRides(
        belt.level, belt.state, { invalidEntityId }, {}, 0.2f));
}

} // namespace

void testOutcomeSurvivesAnyChangeOutsideItsClaims()
{
    TEST("outcomeSurvivesAnyChangeOutsideItsClaims");
    // The guarantee, stated as an executable property.
    //
    // A plan's outcome is a pure function of the state at the instant it was
    // made, and its claims are what the reservation table will protect. So
    // mutating any cell the plan does not claim must leave its outcome
    // bit-for-bit identical - and if it does not, either the claim set is
    // understated or the planner is consulting something no claim covers.
    // Either way the table would then be admitting concurrency that can change
    // a committed outcome, which is the one thing this design exists to make
    // impossible.
    //
    // This is the property that let the read set go. The cell that stopped the
    // slide used to be declared as a read; under the claim rule the block claims
    // its whole path from the start, so the only cells left unclaimed are ones
    // it never touches - and those are exactly the ones this loop mutates.
    //
    // Note the asymmetry being tested: this says nothing about whether the claim
    // set is *tight*. An overstated one only refuses concurrency that would have
    // been safe, which costs responsiveness and never correctness.
    const Level level = makeLevel({
        { "........", "........" },
        { "CI     #", "        " },
    });
    const GameState pushed = plans::planPlayerStep(
        level, rules::initialState(level), MoveDirection::Right, {}, 0.2f)
                                ->action.after;

    const std::vector<EntityId> sliding = plans::slidingEntities(pushed);
    CHECK(sliding.size() == 1);
    if (sliding.empty()) {
        return;
    }
    const std::optional<plans::PlannedAction> slide =
        plans::planSlides(level, pushed, sliding, {}, 0.2f);
    CHECK(slide.has_value());
    if (!slide) {
        return;
    }
    const ActionReservations claims = plans::reservationsFor(*slide);

    const auto claimed = [&](GridPosition3 at) {
        return std::ranges::any_of(
            claims.cells,
            [&](const Reservation& reservation) {
                return reservation.cell.x == at.x &&
                    reservation.cell.y == at.y &&
                    reservation.cell.z == at.z;
            });
    };

    // Every free cell on the board the slide does not claim.
    // Putting the player on each in turn is the strongest mutation available
    // here, since a player is exactly the kind of thing that could block it.
    int tested = 0;
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x < 8; ++x) {
            const GridPosition3 at = cell(x, y, 1);
            if (claimed(at) || !rules::staticCellAllowsEntity(level, at)) {
                continue;
            }

            GameState mutated = pushed;
            mutated.players[0].cell = at;
            const std::optional<plans::PlannedAction> replanned =
                plans::planSlides(level, mutated, sliding, {}, 0.2f);
            CHECK(replanned.has_value());
            if (!replanned) {
                continue;
            }
            ++tested;
            // The block lands in exactly the same place, having taken exactly
            // the same route to get there.
            CHECK(replanned->action.after.movables[0] ==
                slide->action.after.movables[0]);
            CHECK(replanned->legs.size() == slide->legs.size());
        }
    }
    // Guards against the property passing vacuously because nothing qualified.
    CHECK(tested > 0);
}

void testBeltRidersFollowEachOtherDownOneBelt()
{
    TEST("beltRidersFollowEachOtherDownOneBelt");
    const Fixture belt = beltWithTwoRiders();
    const std::vector<EntityId> riders =
        plans::conveyorRiders(belt.level, belt.state);
    CHECK(riders.size() == 2);
    if (riders.size() != 2) {
        return;
    }

    const std::optional<plans::PlannedAction> ride = plans::planConveyorRides(
        belt.level, belt.state, riders, {}, 0.2f);
    CHECK(ride.has_value());
    if (!ride) {
        return;
    }
    // Both advance. The follower moves into the cell the leader vacates during
    // the same step, which is the whole reason riders are planned as a set.
    CHECK(ride->action.after.movables[0].cell == cell(1, 0, 1));
    CHECK(ride->action.after.movables[1].cell == cell(2, 0, 1));
    // One step, never chained: belt motion does not terminate, so a chained
    // ride would be an action that never ends.
    CHECK(ride->legs.size() == 1);

    // And the bug the set exists to avoid. Planned one at a time, the follower
    // is outside its own plan's scope-mate, so it sees the leader as scenery
    // standing in the cell it is about to leave, and refuses to move. A queue
    // on a belt would never advance.
    const std::optional<plans::PlannedAction> followerAlone =
        plans::planConveyorRides(belt.level, belt.state, { riders[0] }, {}, 0.2f);
    CHECK(!followerAlone.has_value());
    // The leader alone is fine - nothing is in front of it.
    const std::optional<plans::PlannedAction> leaderAlone =
        plans::planConveyorRides(belt.level, belt.state, { riders[1] }, {}, 0.2f);
    CHECK(leaderAlone.has_value());
}

void testChainPlanningStopsAtTheCap()
{
    TEST("chainPlanningStopsAtTheCap");
    // `maxChainedSteps` guards against a cycle nobody has thought of rather
    // than an expected limit - with the mechanics as they stand a slide travels
    // in a straight line and is bounded by the board, and no arrangement of
    // ice, belts and falls has been found that loops. So the cap is reached
    // here the only way it can be: a corridor longer than the cap.
    //
    // What matters is that planning stops rather than running away, and that
    // stopping is safe. It is, because a capped plan leaves the block still
    // carrying momentum, and momentum is what ambient motion schedules on - so
    // the remainder is planned as a second action instead of being lost.
    const int width = plans::maxChainedSteps + 8;
    const std::string floor(static_cast<std::size_t>(width), '.');
    const std::string row =
        "CI" + std::string(static_cast<std::size_t>(width - 3), ' ') + "#";
    const Level level = makeLevel({ { floor }, { row } });

    const std::optional<plans::PlannedAction> pushed = plans::planPlayerStep(
        level, rules::initialState(level), MoveDirection::Right, {}, 0.2f);
    CHECK(pushed.has_value());
    if (!pushed) {
        return;
    }
    const std::optional<plans::PlannedAction> planned = plans::planSlides(
        level,
        pushed->action.after,
        plans::slidingEntities(pushed->action.after),
        {},
        0.2f);
    CHECK(planned.has_value());
    if (!planned) {
        return;
    }

    // Cut short exactly at the cap, not one leg over.
    CHECK(static_cast<int>(planned->legs.size()) == plans::maxChainedSteps);
    // And cut short rather than finished: the block is still travelling, so
    // this is genuinely the cap engaging and not a slide that happened to end.
    CHECK(plans::anySlideMomentum(planned->action.after));
    CHECK(planned->action.after.movables[0].cell.x < width - 2);

    // The remainder is still schedulable, which is what makes the cap safe to
    // hit. Ambient motion sees the momentum and plans the rest.
    const std::vector<EntityId> sliding =
        plans::slidingEntities(planned->action.after);
    CHECK(sliding.size() == 1);
    const std::optional<plans::PlannedAction> rest = plans::planSlides(
        level, planned->action.after, sliding, {}, 0.2f);
    CHECK(rest.has_value());
    if (rest) {
        // Which finishes the journey at the wall.
        CHECK(rest->action.after.movables[0].cell == cell(width - 2, 0, 1));
        CHECK(!plans::anySlideMomentum(rest->action.after));
    }
}

void testWaterStreamPlansCarryPlatformsAndReserveTheirRiders()
{
    TEST("waterStreamPlansCarryPlatformsAndReserveTheirRiders");
    const Level level = makeLevel({ { ".\"\"WW" }, { " $   " },
        { " R   " }, { " C   " } });
    const auto before = rules::initialState(level);
    const auto riders = plans::conveyorRiders(level, before);
    CHECK(riders == std::vector<EntityId> { before.movables[0].id });
    CHECK(plans::waterStreamRiders(level, before) == riders);
    const auto ride = plans::planConveyorRides(level, before, riders, {}, 0.2f);
    CHECK(ride.has_value());
    if (!ride) return;
    CHECK(ride->legs.size() == 1);
    CHECK(ride->action.after.movables[0].cell == cell(2, 0, 1));
    CHECK(ride->action.after.movables[1].cell == cell(2, 0, 2));
    CHECK(ride->action.after.players[0].cell == cell(2, 0, 3));
    CHECK(!ride->action.after.movables[0].fallen);
    CHECK(!ride->action.after.players[0].dead);
    CHECK(plans::planConveyorRides(level, before, riders, {}, 0.2f) == ride);
    CHECK(!plans::planConveyorRides(level, before, riders, { .conveyor = 0 }, 0.2f));
    const auto reverse = plans::inverted(ride->action);
    CHECK(reverse.before == ride->action.after);
    CHECK(reverse.after == before);
    const auto claims = plans::reservationsFor(*ride);
    for (int z = 1; z <= 3; ++z) {
        for (int x = 1; x <= 2; ++x) {
            CHECK(std::ranges::any_of(claims.cells, [&](const Reservation& claim) {
                return claim.cell == cell(x, 0, z);
            }));
        }
    }
    CHECK(plans::conveyorRiders(level, ride->action.after) == riders);

    const Level blocked = makeLevel({ { "W\"WWW" }, { " $   " }, { " C#  " } });
    const auto blockedState = rules::initialState(blocked);
    CHECK(!plans::planConveyorRides(blocked, blockedState,
        plans::conveyorRiders(blocked, blockedState), {}, 0.2f));
}

void testWaterStreamRidersExcludeBeltsAboveStreams()
{
    TEST("waterStreamRidersExcludeBeltsAboveStreams");
    const Level level = makeLevel({ { "W\"WWW", "W\"WWW" },
        { " $   ", "C>   " } });
    auto state = rules::initialState(level);
    state.players[0].cell = cell(1, 1, 1);
    CHECK(plans::conveyorRiders(level, state).size() == 2);
    CHECK(plans::waterStreamRiders(level, state) ==
        std::vector<EntityId> { state.movables[0].id });
}

void testWaterStreamPlansReevaluateCornersAndReserveTheirStack()
{
    TEST("waterStreamPlansReevaluateCornersAndReserveTheirStack");
    const Level level = makeLevel({ { "WWWW", "W\",W", "W'`W", "WWWW" },
        { "    ", " $  ", "    ", "    " },
        { "    ", " R  ", "    ", "    " },
        { "    ", " C  ", "    ", "    " } });
    auto state = rules::initialState(level);
    const auto riders = plans::conveyorRiders(level, state);
    for (const auto target : { cell(2, 1, 1), cell(2, 2, 1), cell(1, 2, 1), cell(1, 1, 1) }) {
        const auto ride = plans::planConveyorRides(level, state, riders, {}, 0.2f);
        CHECK(ride.has_value());
        if (!ride) return;
        CHECK(ride->legs.size() == 1);
        CHECK(ride->action.before == state);
        CHECK(ride->action.after.movables[0].cell == target);
        CHECK(ride->action.after.movables[1].cell == cell(target.x, target.y, 2));
        CHECK(ride->action.after.players[0].cell == cell(target.x, target.y, 3));
        const auto claims = plans::reservationsFor(*ride);
        ReservationTable reservations;
        reservations.admit(1, claims, 0);
        for (int z = 1; z <= 3; ++z) {
            const ActionReservations incoming { .cells = {
                Reservation { .cell = cell(target.x, target.y, z) } } };
            CHECK(reservations.conflict(incoming, 0).has_value());
        }
        CHECK(!reservations.conflict({ .cells = { Reservation { .cell = cell(0, 0, 1) } } }, 0));
        CHECK(plans::conveyorRiders(level, ride->action.after) == riders);
        state = ride->action.after;
    }
}

void testRobotPatrolPlansOneBeatAndLeavesUnrelatedActorsAlone()
{
    TEST("robotPatrolPlansOneBeatAndLeavesUnrelatedActorsAlone");
    const Level level = makeLevel({ { ".......", "......." }, { " }   } ", " C     " } });
    const GameState initial = rules::initialState(level);
    const auto robots = plans::patrollingRobots(initial);
    CHECK(robots.size() == 2);
    CHECK(!plans::planRobotPatrols(level, initial, {}, {}, 0.2f));
    CHECK(!plans::planRobotPatrols(level, initial, robots, { .robot = 0 }, 0.2f));
    const auto patrol = plans::planRobotPatrols(level, initial, { robots[0] }, {}, 0.2f);
    CHECK(patrol.has_value());
    if (!patrol) return;
    CHECK(patrol->legs.size() == 1);
    CHECK(patrol->action.durationSeconds == 0.2f);
    CHECK(patrol->action.after.movables[0].cell == cell(2, 0, 1));
    CHECK(patrol->action.after.movables[1] == initial.movables[1]);
    CHECK(patrol->action.after.players == initial.players);
    CHECK(!patrol->action.playerPushing);
    CHECK(!patrol->action.facingDirection);
    CHECK(plans::planRobotPatrols(level, initial, { robots[0] }, {}, 0.2f) == patrol);
}

void testRoundRockPlansSkipThenRideAtQuarterSkippingSpeed()
{
    TEST("roundRockPlansSkipThenRideAtQuarterSkippingSpeed");
    const Level level = makeLevel({ { "..WWWW.." }, { "C\\  $   " }, { "        " } });
    const auto initial = rules::initialState(level);
    const auto push = plans::planPlayerStep(level, initial, MoveDirection::Right, {}, 0.2f);
    CHECK(push.has_value());
    if (!push) return;
    CHECK(push->legs.size() == 1);
    CHECK(push->action.durationSeconds == 0.2f);
    CHECK(push->action.playerPushing);
    const auto moving = plans::slidingEntities(push->action.after);
    const auto skip = plans::planSlides(level, push->action.after, moving, {}, 0.2f);
    CHECK(skip.has_value());
    if (!skip) return;
    CHECK(skip->legs.size() == 4);
    CHECK(skip->legs[0].movables[0].cell == cell(3, 0, 1));
    CHECK(skip->legs[1].movables[0].cell == cell(4, 0, 2));
    CHECK(skip->action.after.movables[0].cell == cell(5, 0, 2));
    CHECK(skip->action.after.movables[1].cell == cell(5, 0, 1));
    CHECK(!plans::anySlideMomentum(skip->action.after));
    CHECK(skip->action.after.players == push->action.after.players);
    CHECK(std::abs(skip->action.durationSeconds - 1.0f) < 0.0001f);
    const auto durations = plans::legDurations(&level, skip->action.before,
        skip->legs, skip->action.durationSeconds);
    CHECK(durations.size() == 4);
    CHECK(std::abs(durations[0] - 0.1f) < 0.0001f);
    CHECK(std::abs(durations[1] - 0.1f) < 0.0001f);
    CHECK(std::abs(durations[2] - 0.4f) < 0.0001f);
    CHECK(std::abs(durations[3] - 0.4f) < 0.0001f);
    CHECK(skip->legs[2].movables[1].cell == skip->legs[3].movables[1].cell);
    CHECK(skip->legs[2].movables[1].sliding.has_value());
    CHECK(!skip->legs[3].movables[1].sliding);
    CHECK(skip == plans::planSlides(level, push->action.after, moving, {}, 0.2f));
    const auto claims = plans::reservationsFor(*skip);
    for (int x : { 4, 5 }) {
        for (int z : { 1, 2 }) {
            CHECK(std::ranges::any_of(claims.cells, [&](const Reservation& claim) {
                return claim.cell == cell(x, 0, z);
            }));
        }
    }
}

void testRoundRockWakeJoinsPlanClosureAndReservations()
{
    TEST("roundRockWakeJoinsPlanClosureAndReservations");
    const Level level = makeLevel({
        { ".......", "WWWWWWW", "WWWWWWW", "..WWWW.", "WWWWWWW", "WWWWWWW", "......." },
        { "       ", "       ", "   $   ", "C\\     ", "   $   ", "       ", "       " },
    });
    const auto initial = rules::initialState(level);
    const auto push = plans::planPlayerStep(level, initial, MoveDirection::Right, {}, 0.2f);
    CHECK(push.has_value());
    if (!push) return;
    const auto skip = plans::planSlides(level, push->action.after,
        plans::slidingEntities(push->action.after), {}, 0.2f);
    CHECK(skip.has_value());
    if (!skip) return;
    CHECK(!plans::anySlideMomentum(skip->action.after));
    const auto& state = skip->action.after;
    CHECK(state.movables[0].cell == cell(3, 1, 1));
    CHECK(state.movables[2].cell == cell(3, 5, 1));
    CHECK(state.movables[1].cell == cell(6, 3, 1));
    const auto durations = plans::legDurations(&level, skip->action.before,
        skip->legs, skip->action.durationSeconds);
    const GameState* previous = &skip->action.before;
    bool sawPadMovement = false;
    for (std::size_t leg = 0; leg < skip->legs.size(); ++leg) {
        for (std::size_t index : { std::size_t(0), std::size_t(2) }) {
            if (rules::lilyPadDrivenByRock(level, *previous, skip->legs[leg], index)) {
                CHECK(std::abs(durations[leg] - .4f) < .0001f);
                sawPadMovement = sawPadMovement ||
                    previous->movables[index].cell != skip->legs[leg].movables[index].cell;
            }
        }
        previous = &skip->legs[leg];
    }
    CHECK(sawPadMovement);
    const auto claims = plans::reservationsFor(*skip);
    for (const auto endpoint : { cell(3, 1, 1), cell(3, 5, 1), cell(6, 3, 1) }) {
        CHECK(std::ranges::any_of(claims.cells, [&](const Reservation& claim) {
            return claim.cell == endpoint && !claim.lastStep;
        }));
    }
}

void testCoastingPadWithoutRockUsesSlowerClockAndStopsInWater()
{
    TEST("coastingPadWithoutRockUsesSlowerClockAndStopsInWater");
    for (bool withHeroRider : { false, true }) {
        const Level level = makeLevel({
            { "WWWWW..", "......." }, { " $     ", "C      " },
            { withHeroRider ? " C     " : "       ", "       " },
        });
        auto state = rules::initialState(level);
        state.movables[0].sliding = MoveDirection::Right;
        const auto coast = plans::planSlides(level, state,
            plans::slidingEntities(state), {}, .2f);
        CHECK(coast.has_value());
        if (!coast) continue;
        CHECK(coast->legs.size() == 4);
        CHECK(coast->action.after.movables[0].cell == cell(4, 0, 1));
        CHECK(!coast->action.after.movables[0].sliding);
        CHECK(std::abs(coast->action.durationSeconds - 1.6f) < .0001f);
        const auto durations = plans::legDurations(&level, state,
            coast->legs, coast->action.durationSeconds);
        for (float duration : durations) CHECK(std::abs(duration - .4f) < .0001f);
        CHECK(coast->legs[2].movables[0].cell == coast->legs[3].movables[0].cell);
        CHECK(coast->legs[2].movables[0].sliding.has_value());
        if (withHeroRider) {
            CHECK(std::ranges::any_of(coast->action.after.players, [](const auto& player) {
                return player.cell == cell(4, 0, 2) && !player.dead;
            }));
        }
    }
    // Direct hero pushes retain the ordinary one-step clock.
    const Level pushLevel = makeLevel({ { "..WWW" }, { "C$   " }, { " C   " } });
    const auto initial = rules::initialState(pushLevel);
    const auto push = plans::planPlayerStep(pushLevel, initial,
        MoveDirection::Right, {}, .2f, initial.players[0].controller);
    CHECK(push.has_value());
    if (push) {
        CHECK(push->action.durationSeconds == .2f);
        CHECK(push->action.after.movables[0].cell == cell(2, 0, 1));
        CHECK(!push->action.after.movables[0].sliding);
    }
    // A Knight's regular-rock chain shove also uses the slower pad clock,
    // including when the pad travels just one cell without coasting momentum.
    const Level chainLevel = makeLevel({ { "..WW.." }, { "KR$   " } });
    const auto chainInitial = rules::initialState(chainLevel);
    const auto chainPush = plans::planPlayerStep(chainLevel, chainInitial,
        MoveDirection::Right, {}, .2f, chainInitial.players[0].controller);
    CHECK(chainPush.has_value());
    if (chainPush) {
        CHECK(std::abs(chainPush->action.durationSeconds - .4f) < .0001f);
        CHECK(chainPush->action.after.movables[1].cell == cell(3, 0, 1));
        CHECK(!chainPush->action.after.movables[1].sliding);
        CHECK(rules::lilyPadDrivenByRock(chainLevel, chainInitial,
            chainPush->action.after, 1));
    }
}

int main()
{
    testRoundRockPlansSkipThenRideAtQuarterSkippingSpeed();
    testCoastingPadWithoutRockUsesSlowerClockAndStopsInWater();
    testRoundRockWakeJoinsPlanClosureAndReservations();
    testWaterStreamPlansCarryPlatformsAndReserveTheirRiders();
    testWaterStreamRidersExcludeBeltsAboveStreams();
    testWaterStreamPlansReevaluateCornersAndReserveTheirStack();
    testRobotPatrolPlansOneBeatAndLeavesUnrelatedActorsAlone();
    testChainPlanningStopsAtTheCap();
    testBeltRidersFollowEachOtherDownOneBelt();
    testOutcomeSurvivesAnyChangeOutsideItsClaims();
    testPlayerStepPlansAPush();
    testPlayerStepWithoutMovementHasNoPlan();
    testPlanningIsPureAndRepeatable();
    testWalkingWithoutPushing();
    testKnightEnemyPushUsesThePushPresentation();
    testDruidPullIsPlannedAsPulling();
    testWitchSwapDoesNotUseThePushPresentation();
    testRestartPlan();
    testInvertedSwapsEndpointsAndCounts();
    testPlayerMovementHelpers();
    testSlideResolvesAsOneAction();
    testSlideOutcomeIgnoresLaterInterference();
    testChainStopsAtConveyors();
    testSlideMomentumDetection();
    testPlayerStepIsOneStepAndLeavesTheSlideBehind();
    testSlidePlanSettlesTheDestination();
    testSlidePlanLeavesUnrelatedMomentumAlone();
    testPlayerStepLeavesAmbientRidersAlone();
    testConveyorRideIsOneStepPerAction();
    testScopedPlannersRefuseEntitiesWithNothingToDo();

    if (failures == 0) {
        std::cout << "ActionPlanTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ActionPlanTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
