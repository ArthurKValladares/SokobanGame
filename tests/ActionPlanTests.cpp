// Headless tests for action planning.
//
// Planning is separated from execution so that an action's outcome is settled
// by a pure function of the world at the instant it begins. These tests pin
// that separation: the planners decide everything except the running move
// total, and re-planning the same inputs gives the same answer.

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

// The scoped planners. `worldStep` plans every entity, so a plan made while a
// slide was in flight re-planned that slide and collided with the copy already
// running - nothing could ever be admitted alongside anything else. These pin
// the per-entity planners that replace it.

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
        plans::planSlide(level, pushed, sliding.front(), {}, 0.2f);
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

void testPlayerStepAndSlideComposeToTheWholeWorldStep()
{
    TEST("playerStepAndSlideComposeToTheWholeWorldStep");
    const Level level = iceRink();
    const GameState state = rules::initialState(level);

    const std::optional<plans::PlannedAction> whole =
        plans::worldStep(level, state, MoveDirection::Right, {}, 0.2f);
    const std::optional<plans::PlannedAction> step =
        plans::planPlayerStep(level, state, MoveDirection::Right, {}, 0.2f);
    CHECK(whole.has_value());
    CHECK(step.has_value());
    if (!whole || !step) {
        return;
    }

    GameState composed = step->action.after;
    for (const EntityId slider : plans::slidingEntities(composed)) {
        if (const std::optional<plans::PlannedAction> slide =
                plans::planSlide(level, composed, slider, {}, 0.2f)) {
            composed = slide->action.after;
        }
    }

    // Splitting the planner must not change where anything ends up. This is the
    // property that lets the two coexist while the split is wired through.
    CHECK(composed == whole->action.after);
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
        plans::planConveyorRide(level, state, riders.front(), {}, 0.2f);
    CHECK(ride.has_value());
    if (ride) {
        // The belt has more to give, but this action is over: ambient motion
        // never terminates, so a chained ride would never end, and one-step
        // claims are what keep the area around a belt usable.
        CHECK(ride->legs.size() == 1);
        CHECK(ride->action.after.movables[0].cell == cell(2, 1, 1));
        CHECK(ride->action.after.players[0] == state.players[0]);
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
    CHECK(!plans::planSlide(level, state, state.movables[0].id, {}, 0.2f));
    CHECK(!plans::planConveyorRide(level, state, state.movables[0].id, {}, 0.2f));
    CHECK(!plans::planSlide(level, state, invalidEntityId, {}, 0.2f));
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
        plans::planConveyorRide(belt.level, belt.state, riders[0], {}, 0.2f);
    CHECK(!followerAlone.has_value());
    // The leader alone is fine - nothing is in front of it.
    const std::optional<plans::PlannedAction> leaderAlone =
        plans::planConveyorRide(belt.level, belt.state, riders[1], {}, 0.2f);
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

    const std::optional<plans::PlannedAction> planned = plans::worldStep(
        level, rules::initialState(level), MoveDirection::Right, {}, 0.2f);
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

int main()
{
    testChainPlanningStopsAtTheCap();
    testBeltRidersFollowEachOtherDownOneBelt();
    testOutcomeSurvivesAnyChangeOutsideItsClaims();
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
    testPlayerStepIsOneStepAndLeavesTheSlideBehind();
    testSlidePlanSettlesTheDestination();
    testPlayerStepAndSlideComposeToTheWholeWorldStep();
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
