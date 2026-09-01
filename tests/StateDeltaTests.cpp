// Headless tests for per-entity state deltas.
//
// The point of a delta is that completing an action writes only what that
// action changed. The tests worth having are therefore the ones that show an
// unrelated entity surviving, and that keying by id rather than vector
// position holds up when a concurrent action appends players.

#include "TestHarness.hpp"

#include "engine/StateDelta.hpp"

#include <iostream>
#include <string>

namespace {

using namespace sokoban;

[[nodiscard]] GridPosition3 cell(int x, int y, int z)
{
    return { x, y, z };
}

[[nodiscard]] GameState twoPlayersAndARock()
{
    GameState state;
    state.players.push_back({ .id = 1, .cell = cell(0, 0, 1) });
    state.players.push_back({ .id = 2, .cell = cell(5, 5, 1) });
    state.movables.push_back({
        .id = 3, .type = TileType::Rock, .cell = cell(2, 0, 1) });
    state.enemies.push_back({ .id = 4, .cell = cell(7, 7, 1) });
    return state;
}

void testUnchangedEntitiesAreAbsent()
{
    TEST("unchangedEntitiesAreAbsent");
    const GameState before = twoPlayersAndARock();
    GameState after = before;
    after.players[0].cell = cell(1, 0, 1);

    const StateDelta delta = StateDelta::between(before, after);
    // Only the player that moved. Everything absent from the delta is what a
    // concurrent action is free to be touching.
    CHECK(delta.players.size() == 1);
    CHECK(delta.movables.empty());
    CHECK(delta.enemies.empty());
    CHECK(delta.players[0].id == 1);
    CHECK(delta.players[0].before->cell == cell(0, 0, 1));
    CHECK(delta.players[0].after->cell == cell(1, 0, 1));
    CHECK(!delta.empty());

    CHECK(StateDelta::between(before, before).empty());
}

void testApplyingReproducesAfter()
{
    TEST("applyingReproducesAfter");
    const GameState before = twoPlayersAndARock();
    GameState after = before;
    after.players[0].cell = cell(1, 0, 1);
    after.movables[0].cell = cell(3, 0, 1);
    after.movables[0].sliding = MoveDirection::Right;

    GameState applied = before;
    StateDelta::between(before, after).applyTo(applied);
    // Equal as a whole value, not just entity by entity: vector order has to
    // survive too, because state comparisons throughout the game use ==.
    CHECK(applied == after);
}

void testUnrelatedConcurrentChangeSurvives()
{
    TEST("unrelatedConcurrentChangeSurvives");
    // This is the reason the type exists. Two actions are in flight; the first
    // one's `after` was captured before the second had done anything. Assigning
    // that whole state would erase the second action's work.
    const GameState before = twoPlayersAndARock();
    GameState first = before;
    first.players[0].cell = cell(1, 0, 1);

    GameState live = before;
    live.players[1].cell = cell(5, 4, 1);  // the other action, already applied
    live.enemies[0].cell = cell(7, 6, 1);

    StateDelta::between(before, first).applyTo(live);

    CHECK(live.players[0].cell == cell(1, 0, 1));  // first action landed
    CHECK(live.players[1].cell == cell(5, 4, 1));  // second action intact
    CHECK(live.enemies[0].cell == cell(7, 6, 1));
    // The whole-state assignment this replaced would have failed here.
    CHECK(!(live == first));
}

void testCreatedAndRemovedEntities()
{
    TEST("createdAndRemovedEntities");
    // Mirror activation appends players; undoing it has to take them out.
    const GameState before = twoPlayersAndARock();
    GameState after = before;
    after.players.push_back({ .id = 9, .cell = cell(3, 3, 1) });

    const StateDelta delta = StateDelta::between(before, after);
    CHECK(delta.players.size() == 1);
    CHECK(!delta.players[0].before.has_value());
    CHECK(delta.players[0].after->id == 9);

    GameState applied = before;
    delta.applyTo(applied);
    CHECK(applied == after);

    const StateDelta undo = delta.inverted();
    CHECK(undo.players.size() == 1);
    CHECK(undo.players[0].before->id == 9);
    CHECK(!undo.players[0].after.has_value());

    undo.applyTo(applied);
    CHECK(applied == before);
}

void testKeyedByIdNotPosition()
{
    TEST("keyedByIdNotPosition");
    // A delta computed earlier must still land on the right entity after a
    // concurrent action has appended a player and shifted nothing but indices.
    const GameState before = twoPlayersAndARock();
    GameState after = before;
    after.players[1].cell = cell(5, 4, 1);
    const StateDelta delta = StateDelta::between(before, after);
    CHECK(delta.players[0].id == 2);

    GameState live = before;
    // A reflection lands at the front of the vector, so player id 2 is no
    // longer at index 1.
    live.players.insert(
        live.players.begin(), { .id = 9, .cell = cell(3, 3, 1) });
    delta.applyTo(live);

    CHECK(live.players.size() == 3);
    CHECK(live.players[0].id == 9);
    CHECK(live.players[0].cell == cell(3, 3, 1));   // untouched
    CHECK(live.players[2].id == 2);
    CHECK(live.players[2].cell == cell(5, 4, 1));   // moved, despite the shift
}

void testInvertedRoundTrip()
{
    TEST("invertedRoundTrip");
    const GameState before = twoPlayersAndARock();
    GameState after = before;
    after.players[0].cell = cell(1, 0, 1);
    after.movables[0].fallen = true;
    after.enemies[0].fallen = true;

    const StateDelta delta = StateDelta::between(before, after);
    GameState state = before;
    delta.applyTo(state);
    CHECK(state == after);
    delta.inverted().applyTo(state);
    CHECK(state == before);
    // Inverting twice is the original, which is what makes undo of an undo the
    // same machinery rather than a special case.
    CHECK(delta.inverted().inverted() == delta);
}

void testWholesaleReplacementLikeRestart()
{
    TEST("wholesaleReplacementLikeRestart");
    // Restart moves everything at once and drops any reflected players. Ids in
    // the restarted state come from rules::initialState, which numbers from 1,
    // so they line up with the originals.
    GameState before = twoPlayersAndARock();
    before.players[0].cell = cell(4, 4, 1);
    before.players.push_back({ .id = 9, .cell = cell(3, 3, 1) });

    GameState restarted = twoPlayersAndARock();
    const StateDelta delta = StateDelta::between(before, restarted);
    GameState state = before;
    delta.applyTo(state);
    CHECK(state == restarted);
    CHECK(state.players.size() == 2);
}

void testStatesWithoutIdsFallBackToPosition()
{
    TEST("statesWithoutIdsFallBackToPosition");
    // Hand-authored states and editor previews carry no ids. resolvedEntityId
    // derives a key from the index for them, so deltas still work positionally.
    GameState before;
    before.players.push_back({ .cell = cell(0, 0, 1) });
    before.movables.push_back({ .type = TileType::Rock, .cell = cell(2, 0, 1) });

    GameState after = before;
    after.players[0].cell = cell(1, 0, 1);

    const StateDelta delta = StateDelta::between(before, after);
    CHECK(delta.players.size() == 1);
    CHECK(delta.movables.empty());

    GameState state = before;
    delta.applyTo(state);
    CHECK(state == after);
}

void testDeltaOfEqualStatesChangesNothing()
{
    TEST("deltaOfEqualStatesChangesNothing");
    const GameState before = twoPlayersAndARock();
    GameState live = before;
    live.players[0].cell = cell(9, 9, 1);
    const GameState untouched = live;

    // An action that changed nothing must not stamp anything over a state that
    // moved on underneath it.
    StateDelta::between(before, before).applyTo(live);
    CHECK(live == untouched);
}

} // namespace

int main()
{
    testUnchangedEntitiesAreAbsent();
    testApplyingReproducesAfter();
    testUnrelatedConcurrentChangeSurvives();
    testCreatedAndRemovedEntities();
    testKeyedByIdNotPosition();
    testInvertedRoundTrip();
    testWholesaleReplacementLikeRestart();
    testStatesWithoutIdsFallBackToPosition();
    testDeltaOfEqualStatesChangesNothing();

    if (failures == 0) {
        std::cout << "StateDeltaTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "StateDeltaTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
