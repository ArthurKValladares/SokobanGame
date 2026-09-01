// Headless tests for space-time reservations.
//
// The property that matters is asymmetric: the table must never admit two
// actions that would disturb each other, and should admit ones that provably
// cannot. The second half is what buys concurrency; the first is what keeps the
// determinism guarantee true.
//
// The claim rule is one sentence: an action holds every cell on its path from
// the moment it starts until the instant it leaves that cell, and holds the cell
// it comes to rest on open-ended. So the *only* concurrency the table admits on
// a shared cell is something arriving after the entity has gone. Entering a cell
// ahead of a moving entity is refused, deliberately - see "What the concurrency
// is for" in DESIGN-deterministic-actions.md.

#include "TestHarness.hpp"

#include "engine/Reservation.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

[[nodiscard]] GridPosition3 cell(int x, int y, int z = 1)
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

[[nodiscard]] bool holds(
    const std::vector<Reservation>& reservations,
    GridPosition3 at,
    int step)
{
    const Reservation probe {
        .cell = at, .firstStep = step, .lastStep = step };
    for (const Reservation& reservation : reservations) {
        if (reservation.overlaps(probe)) {
            return true;
        }
    }
    return false;
}

void testOverlapRules()
{
    TEST("overlapRules");
    const Reservation early { .cell = cell(1, 0), .firstStep = 0, .lastStep = 2 };
    const Reservation late { .cell = cell(1, 0), .firstStep = 3, .lastStep = 5 };
    const Reservation touching {
        .cell = cell(1, 0), .firstStep = 2, .lastStep = 4 };
    CHECK(!early.overlaps(late));
    CHECK(!late.overlaps(early));
    CHECK(early.overlaps(touching));
    CHECK(touching.overlaps(early));

    // Different cells never clash, whatever the timing.
    const Reservation elsewhere {
        .cell = cell(9, 9), .firstStep = 0, .lastStep = 99 };
    CHECK(!early.overlaps(elsewhere));

    // An unset end runs forever, which is what a resting entity needs.
    const Reservation resting {
        .cell = cell(1, 0), .firstStep = 1, .lastStep = std::nullopt };
    CHECK(resting.overlaps(late));
    CHECK(late.overlaps(resting));
    CHECK(resting.overlaps(early));
    const Reservation before {
        .cell = cell(1, 0), .firstStep = 0, .lastStep = 0 };
    CHECK(!resting.overlaps(before));
    CHECK(!before.overlaps(resting));
}

// A pushed ice block that travels several tiles before a wall stops it.
[[nodiscard]] plans::PlannedAction slidePlan()
{
    const Level level = makeLevel({
        { "........" },
        { "CI     #" },
    });
    const GameState state = rules::initialState(level);
    return *plans::worldStep(level, state, MoveDirection::Right, {}, 0.1f);
}

void testSlideClaimsItsWholePath()
{
    TEST("slideClaimsItsWholePath");
    const plans::PlannedAction planned = slidePlan();
    const ActionReservations claims = plans::reservationsFor(planned);

    // The block starts at x=1 and ends at x=6. Every cell between is claimed
    // from the action's start - not from the instant the block arrives - and
    // released at the instant it leaves. There is one more instant than there
    // are legs: the state before the first step counts.
    CHECK(holds(claims.cells, cell(1, 0), 0));
    CHECK(holds(claims.cells, cell(6, 0), static_cast<int>(planned.legs.size())));
    // Its resting cell stays claimed indefinitely - it is still standing there.
    CHECK(holds(claims.cells, cell(6, 0), 999));

    // And this is the claim rule, in the one place it differs from the shape
    // that came before: the destination is claimed from instant 0, long before
    // the block gets there. Nothing may walk into a cell the block is going to
    // occupy. Deliberate - it is what keeps the machinery this small.
    CHECK(holds(claims.cells, cell(6, 0), 0));
    CHECK(holds(claims.cells, cell(3, 0), 0));

    // Behind it, the claim ends. The block leaves (2,0) at instant 1, and from
    // instant 2 the cell belongs to whoever wants it.
    CHECK(holds(claims.cells, cell(2, 0), 1));
    CHECK(!holds(claims.cells, cell(2, 0), 2));

    // The wall that stopped the block is claimed by nothing at all. Under the
    // old shape it was a declared *read* - the outcome depended on it being
    // there - and the read set is gone: the outcome was committed when the plan
    // was made and is meant not to change, so there is nothing to protect.
    CHECK(!holds(claims.cells, cell(7, 0), 0));
    CHECK(!holds(claims.cells, cell(7, 0), 999));

    // The player claims the cell it stepped into, open-ended.
    CHECK(holds(claims.cells, cell(1, 0), 999));
}

void testUninvolvedEntitiesAreNotClaimed()
{
    TEST("uninvolvedEntitiesAreNotClaimed");
    // The bug this pins: claims were built for every entity in the state, not
    // for the ones the action moves. A bystander's resting cell was therefore
    // claimed open-ended by an action that never touched it - and since every
    // plan is computed from the same whole world, every plan claimed every
    // bystander, so no two plans could ever run together.
    //
    // Caught only when reservationsFor and the table are used together; each
    // looked right on its own.
    const Level level = makeLevel({
        { "........", "........" },
        { "CI     #", "        " },
    });
    const GameState state = rules::initialState(level);

    // A slide that moves only the block: the player stays where it is.
    plans::PlannedAction slide;
    slide.action.before = state;
    GameState current = state;
    for (int x = 2; x <= 6; ++x) {
        current.movables[0].cell = cell(x, 0);
        slide.legs.push_back(current);
    }
    slide.action.after = current;

    const ActionReservations claims = plans::reservationsFor(slide);
    // The block's path, as before.
    CHECK(holds(claims.cells, cell(6, 0), 999));
    // But nothing at all on the player, which this action leaves alone.
    CHECK(!holds(claims.cells, cell(0, 0), 0));
    CHECK(!holds(claims.cells, cell(0, 0), 999));

    // So a player step clear of the block's path runs alongside it. This is
    // the concurrency the whole design is for - case 1, two actions with
    // nothing to do with each other.
    ReservationTable table;
    table.admit(1, claims, 0);

    plans::PlannedAction stepAside;
    stepAside.action.before = state;
    GameState aside = state;
    aside.players[0].cell = cell(0, 1);
    stepAside.action.after = aside;
    stepAside.legs.push_back(aside);
    CHECK(!table.conflict(plans::reservationsFor(stepAside), 0));

    // Stepping into the cell the block is vacating this very step, however, is
    // now refused - and there is nothing left to construct it with. This case
    // used to assert the opposite: a push handed off between *two* actions, the
    // block leaving the cell at the instant the player entered it. Entity
    // ownership makes that unbuildable, because a real push is one action moving
    // both entities and never two actions to arbitrate. What is left here is
    // some unrelated second action reaching for a cell an ongoing one still
    // holds, which is exactly what should be refused.
    plans::PlannedAction follow;
    follow.action.before = state;
    GameState followed = state;
    followed.players[0].cell = cell(1, 0);
    follow.action.after = followed;
    follow.legs.push_back(followed);
    CHECK(table.conflict(plans::reservationsFor(follow), 0).has_value());

    // Behind the block is the one thing that is allowed. By step 3 it has left
    // (2,0) - it was there at instant 1 - so an action beginning then may have
    // the cell. This is case 2, and the only reason claims carry time at all.
    plans::PlannedAction behind;
    behind.action.before = state;
    GameState entered = state;
    entered.players[0].cell = cell(2, 0);
    behind.action.after = entered;
    behind.legs.push_back(entered);
    CHECK(!table.conflict(plans::reservationsFor(behind), 3));
    // But not while it is still on its way there.
    CHECK(table.conflict(plans::reservationsFor(behind), 1).has_value());
}

void testEntitiesCannotSwapThroughEachOther()
{
    TEST("entitiesCannotSwapThroughEachOther");
    // Two entities that trade cells in one step are each where the other was,
    // so under a scheme that claimed only the instant of arrival they shared no
    // instant and passed straight through one another. That needed a separate
    // set of boundary crossings to catch.
    //
    // The claim rule catches it with no extra concept: each entity claims its
    // destination from instant 0, where the other is still standing. Built here
    // through reservationsFor rather than by hand, because the point is that the
    // ordinary claims are sufficient.
    GameState state;
    state.movables.push_back({ .id = 1, .cell = cell(1, 0) });
    state.movables.push_back({ .id = 2, .cell = cell(2, 0) });

    const auto moveOne = [&](std::size_t index, GridPosition3 to) {
        plans::PlannedAction planned;
        planned.action.before = state;
        GameState after = state;
        after.movables[index].cell = to;
        planned.action.after = after;
        planned.legs.push_back(after);
        return plans::reservationsFor(planned);
    };

    ReservationTable table;
    table.admit(1, moveOne(0, cell(2, 0)), 0);
    CHECK(table.conflict(moveOne(1, cell(1, 0)), 0).has_value());

    // And the same geometry without the head-on crossing is refused too, which
    // is the deliberate part: a follower entering the cell the leader is leaving
    // this very step is a cell "ahead" by the rule's reckoning, since the leader
    // has not left yet. Convoy motion that must work - belt riders following
    // each other - is planned as one action for exactly this reason.
    GameState convoyState;
    convoyState.movables.push_back({ .id = 3, .cell = cell(0, 0) });
    plans::PlannedAction convoy;
    convoy.action.before = convoyState;
    GameState convoyAfter = convoyState;
    convoyAfter.movables[0].cell = cell(1, 0);
    convoy.action.after = convoyAfter;
    convoy.legs.push_back(convoyAfter);
    CHECK(table.conflict(plans::reservationsFor(convoy), 0).has_value());
}

void testDisjointActionsDoNotConflict()
{
    TEST("disjointActionsDoNotConflict");
    const ActionReservations claims = plans::reservationsFor(slidePlan());
    ReservationTable table;
    table.admit(1, claims, 0);
    CHECK(table.size() == 1);

    // Somewhere else entirely on the board.
    ActionReservations elsewhere;
    elsewhere.cells.push_back({
        .cell = cell(3, 5), .firstStep = 0, .lastStep = std::nullopt });
    CHECK(!table.conflict(elsewhere, 0));

    table.release(1);
    CHECK(table.empty());
}

void testCrossingBehindTheBlockIsAllowed()
{
    TEST("crossingBehindTheBlockIsAllowed");
    // The one case time is kept for, and the only reason a claim carries a last
    // step rather than being held for the whole action. The block passes x=2 at
    // instant 1; from instant 2 the cell is nobody's, so something crossing it
    // afterwards never meets the block.
    const plans::PlannedAction planned = slidePlan();
    ReservationTable table;
    table.admit(1, plans::reservationsFor(planned), 0);

    ActionReservations passer;
    passer.cells.push_back({
        .cell = cell(2, 0), .firstStep = 2, .lastStep = 2 });
    passer.cells.push_back({
        .cell = cell(2, 1), .firstStep = 2, .lastStep = std::nullopt });
    CHECK(!table.conflict(passer, 0));

    // Ahead of it is the other half of the same rule, and it is refused. The
    // block reaches x=6 several steps in, and a plain point-in-time scheme would
    // let something pass through x=6 and be gone before it arrives. That is a
    // deliberate loss: the block's destination is settled, and admitting anyone
    // into its path means arbitrating a collision that the plan has already
    // decided will not happen.
    ActionReservations ahead;
    ahead.cells.push_back({
        .cell = cell(6, 0), .firstStep = 0, .lastStep = 0 });
    ahead.cells.push_back({
        .cell = cell(6, 1), .firstStep = 0, .lastStep = std::nullopt });
    CHECK(table.conflict(ahead, 0).has_value());
}

void testStandingInThePathConflicts()
{
    TEST("standingInThePathConflicts");
    // Something moves onto the block's destination early and simply stays there.
    // This used to be the read set's motivating case; under the claim rule the
    // destination is held open-ended from the start and a plain claim-against-
    // claim test refuses it, which is why the read set could go.
    const plans::PlannedAction planned = slidePlan();
    ReservationTable table;
    table.admit(7, plans::reservationsFor(planned), 0);

    ActionReservations stander;
    stander.cells.push_back({
        .cell = cell(6, 0), .firstStep = 0, .lastStep = std::nullopt });

    const std::optional<ReservationTable::Conflict> conflict =
        table.conflict(stander, 0);
    CHECK(conflict.has_value());
    if (conflict) {
        CHECK(conflict->heldBy == 7);
        CHECK(conflict->cell == cell(6, 0));
    }
}

void testConflictIsSymmetric()
{
    TEST("conflictIsSymmetric");
    // Whichever action was admitted first, the pair must be judged the same
    // way, or admission order would decide correctness.
    ActionReservations first;
    first.cells.push_back({
        .cell = cell(4, 0), .firstStep = 2, .lastStep = 4 });
    ActionReservations second;
    second.cells.push_back({
        .cell = cell(4, 0), .firstStep = 3, .lastStep = 3 });

    ReservationTable a;
    a.admit(1, first, 0);
    CHECK(a.conflict(second, 0).has_value());

    ReservationTable b;
    b.admit(2, second, 0);
    CHECK(b.conflict(first, 0).has_value());
}

void testBaseStepShiftsClaims()
{
    TEST("baseStepShiftsClaims");
    // Actions start at different moments, so their step indices are relative
    // and the table places them on the shared clock.
    ActionReservations claims;
    claims.cells.push_back({
        .cell = cell(2, 0), .firstStep = 0, .lastStep = 1 });

    ReservationTable table;
    table.admit(1, claims, 0);          // occupies steps 0-1
    CHECK(table.conflict(claims, 1).has_value());   // steps 1-2, overlaps at 1
    CHECK(!table.conflict(claims, 2));              // steps 2-3, clear

    ReservationTable late;
    late.admit(1, claims, 10);
    CHECK(!late.conflict(claims, 0));
    CHECK(late.conflict(claims, 11).has_value());
}

void testReleaseFreesTheCells()
{
    TEST("releaseFreesTheCells");
    ActionReservations claims;
    claims.cells.push_back({
        .cell = cell(2, 0), .firstStep = 0, .lastStep = std::nullopt });

    ReservationTable table;
    table.admit(1, claims, 0);
    table.admit(2, claims, 0);
    CHECK(table.conflict(claims, 0).has_value());

    table.release(1);
    CHECK(table.size() == 1);
    // Still held by the second action.
    CHECK(table.conflict(claims, 0).has_value());
    table.release(2);
    CHECK(!table.conflict(claims, 0));

    table.admit(3, claims, 0);
    table.clear();
    CHECK(table.empty());
}

void testEmptyPlanClaimsNothing()
{
    TEST("emptyPlanClaimsNothing");
    const plans::PlannedAction empty;
    const ActionReservations claims = plans::reservationsFor(empty);
    CHECK(claims.cells.empty());

    ReservationTable table;
    table.admit(1, plans::reservationsFor(slidePlan()), 0);
    CHECK(!table.conflict(claims, 0));
}

} // namespace

void testEntitiesAnActionAddsAreClaimed()
{
    TEST("entitiesAnActionAddsAreClaimed");
    // The bug this pins: tracks were built by walking `before`-sized ranges
    // into each leg, so an entity a leg *adds* fell off the end of the loop and
    // claimed nothing at all. Mirror activation clones a player, and the
    // clone's destination was left free for anything else to walk into - a
    // silent hole in the guarantee rather than a visible refusal.
    const Level level = makeLevel({
        { "........", "........" },
        { "C      #", "        " },
    });
    const GameState state = rules::initialState(level);

    // Mirror activation as the session builds it: instantaneous, no legs of its
    // own, and the clone appended to the player vector.
    GameState reflected = state;
    reflected.players[0].cell = cell(3, 0);
    GameState::Player clone = reflected.players[0];
    clone.id = reflected.players[0].id + 100;
    clone.cell = cell(5, 0);
    reflected.players.push_back(clone);

    plans::PlannedAction activation;
    activation.action.before = state;
    activation.action.after = reflected;
    activation.legs.push_back(reflected);

    const ActionReservations claims = plans::reservationsFor(activation);
    // The original's move, as before.
    CHECK(holds(claims.cells, cell(0, 0), 0));
    CHECK(holds(claims.cells, cell(3, 0), 999));
    // And the clone's cell, held open-ended from the instant it appears.
    CHECK(holds(claims.cells, cell(5, 0), 1));
    CHECK(holds(claims.cells, cell(5, 0), 999));
    // But not before it existed. This is the one thing `firstStep` still varies
    // for: every other claim starts at the action's own base, and an entity that
    // was standing nowhere cannot claim a cell it was not yet in.
    CHECK(!holds(claims.cells, cell(5, 0), 0));

    // So something else walking onto the clone's cell is now refused.
    ReservationTable table;
    table.admit(1, claims, 0);

    plans::PlannedAction intruder;
    GameState before = state;
    before.movables.push_back({});
    before.movables[0].cell = cell(5, 1);
    intruder.action.before = before;
    GameState after = before;
    after.movables[0].cell = cell(5, 0);
    intruder.action.after = after;
    intruder.legs.push_back(after);
    CHECK(table.conflict(plans::reservationsFor(intruder), 0).has_value());
}

int main()
{
    testEntitiesAnActionAddsAreClaimed();
    testOverlapRules();
    testSlideClaimsItsWholePath();
    testUninvolvedEntitiesAreNotClaimed();
    testEntitiesCannotSwapThroughEachOther();
    testDisjointActionsDoNotConflict();
    testCrossingBehindTheBlockIsAllowed();
    testStandingInThePathConflicts();
    testConflictIsSymmetric();
    testBaseStepShiftsClaims();
    testReleaseFreesTheCells();
    testEmptyPlanClaimsNothing();

    if (failures == 0) {
        std::cout << "ReservationTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ReservationTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
