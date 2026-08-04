// Headless tests for space-time reservations.
//
// The property that matters is asymmetric: the table must never admit two
// actions that would disturb each other, and should admit ones that provably
// cannot. The second half is what buys concurrency; the first is what keeps the
// determinism guarantee true.

#include "engine/Reservation.hpp"

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

    // The block starts at x=1 and ends at x=6; every cell between is claimed at
    // the instant it is occupied, and none of them earlier. There is one more
    // instant than there are legs - the state before the first step counts.
    CHECK(holds(claims.writes, cell(1, 0), 0));
    CHECK(holds(claims.writes, cell(6, 0), static_cast<int>(planned.legs.size())));
    // Its resting cell stays claimed indefinitely - it is still standing there.
    CHECK(holds(claims.writes, cell(6, 0), 999));
    // But it does not claim the destination before it arrives.
    CHECK(!holds(claims.writes, cell(6, 0), 0));

    // The wall that stopped it is a read: had that cell been clear, the block
    // would have kept going, so the outcome depended on it.
    CHECK(holds(claims.reads, cell(7, 0), 999));

    // The player claims the cell it stepped into, open-ended.
    CHECK(holds(claims.writes, cell(1, 0), 999));
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
    CHECK(holds(claims.writes, cell(6, 0), 999));
    // But nothing at all on the player, which this action leaves alone.
    CHECK(!holds(claims.writes, cell(0, 0), 0));
    CHECK(!holds(claims.writes, cell(0, 0), 999));

    // So a player step clear of the block's path runs alongside it. This is
    // the concurrency the whole design is for.
    ReservationTable table;
    table.admit(1, claims, 0);

    plans::PlannedAction stepAside;
    stepAside.action.before = state;
    GameState aside = state;
    aside.players[0].cell = cell(0, 1);
    stepAside.action.after = aside;
    stepAside.legs.push_back(aside);
    CHECK(!table.conflict(plans::reservationsFor(stepAside), 0));

    // And stepping into the cell the block is vacating this very step is
    // allowed - that is a push, and refusing it would make the commonest
    // interaction in the game the one thing concurrency cannot express. The
    // block holds the cell at instant 0, the player from instant 1.
    plans::PlannedAction follow;
    follow.action.before = state;
    GameState followed = state;
    followed.players[0].cell = cell(1, 0);
    follow.action.after = followed;
    follow.legs.push_back(followed);
    CHECK(!table.conflict(plans::reservationsFor(follow), 0));
}

void testEntitiesCannotSwapThroughEachOther()
{
    TEST("entitiesCannotSwapThroughEachOther");
    // The case occupancy-at-instants misses on its own. Two entities that trade
    // cells in one step are each where the other was, so they share no instant
    // - but they cross mid-step and pass straight through one another.
    ActionReservations left;
    left.writes.push_back({ .cell = cell(1, 0), .firstStep = 0, .lastStep = 0 });
    left.writes.push_back({
        .cell = cell(2, 0), .firstStep = 1, .lastStep = std::nullopt });
    left.moves.push_back({ .from = cell(1, 0), .to = cell(2, 0), .step = 0 });

    ActionReservations right;
    right.writes.push_back({ .cell = cell(2, 0), .firstStep = 0, .lastStep = 0 });
    right.writes.push_back({
        .cell = cell(1, 0), .firstStep = 1, .lastStep = std::nullopt });
    right.moves.push_back({ .from = cell(2, 0), .to = cell(1, 0), .step = 0 });

    ReservationTable table;
    table.admit(1, left, 0);
    CHECK(table.conflict(right, 0).has_value());

    // Following in convoy is the same geometry minus the head-on crossing, and
    // is exactly what a push is, so it must still be admitted.
    ActionReservations convoy;
    convoy.writes.push_back({ .cell = cell(0, 0), .firstStep = 0, .lastStep = 0 });
    convoy.writes.push_back({
        .cell = cell(1, 0), .firstStep = 1, .lastStep = std::nullopt });
    convoy.moves.push_back({ .from = cell(0, 0), .to = cell(1, 0), .step = 0 });
    CHECK(!table.conflict(convoy, 0));

    // Not that the swap rule is doing all the work here: the first entity comes
    // to rest on (2,0) and holds it open-ended, so a later attempt on that cell
    // is refused by occupancy regardless of direction.
    ActionReservations later = right;
    later.moves[0].step = 1;
    later.writes[0].firstStep = 1; later.writes[0].lastStep = 1;
    later.writes[1].firstStep = 2;
    CHECK(table.conflict(later, 0).has_value());
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
    elsewhere.writes.push_back({
        .cell = cell(3, 5), .firstStep = 0, .lastStep = std::nullopt });
    CHECK(!table.conflict(elsewhere, 0));

    table.release(1);
    CHECK(table.empty());
}

void testCrossingAheadOfTheBlockIsAllowed()
{
    TEST("crossingAheadOfTheBlockIsAllowed");
    // This is the case a plain dirty-tile scheme gets wrong. The block reaches
    // x=6 several steps in; something that passes through x=6 and is gone
    // before it arrives never meets it.
    const plans::PlannedAction planned = slidePlan();
    ReservationTable table;
    table.admit(1, plans::reservationsFor(planned), 0);

    ActionReservations passer;
    passer.writes.push_back({
        .cell = cell(6, 0), .firstStep = 0, .lastStep = 0 });
    passer.writes.push_back({
        .cell = cell(6, 1), .firstStep = 1, .lastStep = std::nullopt });
    CHECK(!table.conflict(passer, 0));
}

void testStandingInThePathConflicts()
{
    TEST("standingInThePathConflicts");
    // The case a write-only, point-in-time scheme misses. Something moves onto
    // the block's destination early and simply stays there; it writes the cell
    // once, but is still standing on it when the block arrives.
    const plans::PlannedAction planned = slidePlan();
    ReservationTable table;
    table.admit(7, plans::reservationsFor(planned), 0);

    ActionReservations stander;
    stander.writes.push_back({
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
    first.writes.push_back({
        .cell = cell(4, 0), .firstStep = 2, .lastStep = 4 });
    ActionReservations second;
    second.writes.push_back({
        .cell = cell(4, 0), .firstStep = 3, .lastStep = 3 });

    ReservationTable a;
    a.admit(1, first, 0);
    CHECK(a.conflict(second, 0).has_value());

    ReservationTable b;
    b.admit(2, second, 0);
    CHECK(b.conflict(first, 0).has_value());
}

void testReadsAloneDoNotConflict()
{
    TEST("readsAloneDoNotConflict");
    // Two actions may both depend on the same wall staying where it is.
    ActionReservations first;
    first.reads.push_back({
        .cell = cell(7, 0), .firstStep = 0, .lastStep = std::nullopt });
    ActionReservations second = first;

    ReservationTable table;
    table.admit(1, first, 0);
    CHECK(!table.conflict(second, 0));

    // But a write against that read does conflict: moving the wall changes the
    // first action's outcome.
    ActionReservations writer;
    writer.writes.push_back({
        .cell = cell(7, 0), .firstStep = 3, .lastStep = 3 });
    CHECK(table.conflict(writer, 0).has_value());
}

void testBaseStepShiftsClaims()
{
    TEST("baseStepShiftsClaims");
    // Actions start at different moments, so their step indices are relative
    // and the table places them on the shared clock.
    ActionReservations claims;
    claims.writes.push_back({
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
    claims.writes.push_back({
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
    CHECK(claims.writes.empty());
    CHECK(claims.reads.empty());

    ReservationTable table;
    table.admit(1, plans::reservationsFor(slidePlan()), 0);
    CHECK(!table.conflict(claims, 0));
}

} // namespace

int main()
{
    testOverlapRules();
    testSlideClaimsItsWholePath();
    testUninvolvedEntitiesAreNotClaimed();
    testEntitiesCannotSwapThroughEachOther();
    testDisjointActionsDoNotConflict();
    testCrossingAheadOfTheBlockIsAllowed();
    testStandingInThePathConflicts();
    testConflictIsSymmetric();
    testReadsAloneDoNotConflict();
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
