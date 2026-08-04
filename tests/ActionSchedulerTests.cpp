// Headless tests for concurrent action execution.
//
// The scheduler exists to let the player keep moving while a pushed block is
// still travelling. These tests cover the two halves of that bargain: actions
// that cannot disturb each other run together, and ones that could are refused;
// and whatever runs concurrently commits without treading on anything else.

#include "engine/ActionScheduler.hpp"

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

[[nodiscard]] bool started(
    const std::variant<ActionScheduler::Started, ActionScheduler::Rejection>& r)
{
    return std::holds_alternative<ActionScheduler::Started>(r);
}

// A state with two entities far apart, so plans over them are independent.
[[nodiscard]] GameState twoRocks()
{
    GameState state;
    state.players.push_back({ .id = 1, .cell = cell(0, 0) });
    state.movables.push_back({
        .id = 2, .type = TileType::Rock, .cell = cell(5, 0) });
    state.movables.push_back({
        .id = 3, .type = TileType::Rock, .cell = cell(5, 9) });
    return state;
}

// Moves one movable to a new cell over `duration`, claiming only those cells.
[[nodiscard]] std::pair<ActionPlan, ActionReservations> movePlan(
    const GameState& before,
    std::size_t movableIndex,
    GridPosition3 to,
    float duration)
{
    ActionPlan plan;
    plan.before = before;
    plan.after = before;
    plan.after.movables[movableIndex].cell = to;
    plan.durationSeconds = duration;

    ActionReservations claims;
    claims.writes.push_back({
        .cell = before.movables[movableIndex].cell,
        .firstStep = 0,
        .lastStep = 0,
    });
    claims.writes.push_back({
        .cell = to, .firstStep = 0, .lastStep = std::nullopt });
    return { plan, claims };
}

void testIndependentActionsRunTogether()
{
    TEST("independentActionsRunTogether");
    ActionScheduler scheduler;
    scheduler.reset(twoRocks(), 0.1f);

    const auto [first, firstClaims] =
        movePlan(scheduler.state(), 0, cell(6, 0), 0.5f);
    const auto [second, secondClaims] =
        movePlan(scheduler.state(), 1, cell(6, 9), 0.2f);

    CHECK(started(scheduler.tryStart(first, firstClaims)));
    // Nowhere near the first, so it starts while that one is still running.
    CHECK(started(scheduler.tryStart(second, secondClaims)));
    CHECK(scheduler.inFlight().size() == 2);
    CHECK(!scheduler.idle());

    // The shorter one finishes first and commits alone.
    std::vector<std::size_t> done = scheduler.advance(0.25f);
    CHECK(done.size() == 1);
    CHECK(scheduler.state().movables[1].cell == cell(6, 9));
    CHECK(scheduler.state().movables[0].cell == cell(5, 0));
    CHECK(scheduler.inFlight().size() == 1);

    done = scheduler.advance(0.3f);
    CHECK(done.size() == 1);
    CHECK(scheduler.state().movables[0].cell == cell(6, 0));
    // Both landed: neither commit erased the other.
    CHECK(scheduler.state().movables[1].cell == cell(6, 9));
    CHECK(scheduler.idle());
}

void testConflictingActionIsRefused()
{
    TEST("conflictingActionIsRefused");
    ActionScheduler scheduler;
    scheduler.reset(twoRocks(), 0.1f);

    const auto [first, firstClaims] =
        movePlan(scheduler.state(), 0, cell(6, 0), 0.5f);
    CHECK(started(scheduler.tryStart(first, firstClaims)));

    // Wants the cell the first action is about to come to rest on.
    ActionPlan intruder;
    intruder.before = scheduler.state();
    intruder.after = intruder.before;
    intruder.after.players[0].cell = cell(6, 0);
    intruder.durationSeconds = 0.1f;
    ActionReservations intruderClaims;
    intruderClaims.writes.push_back({
        .cell = cell(6, 0), .firstStep = 0, .lastStep = std::nullopt });

    const auto result = scheduler.tryStart(intruder, intruderClaims);
    CHECK(!started(result));
    if (const auto* rejection =
            std::get_if<ActionScheduler::Rejection>(&result)) {
        CHECK(rejection->cell == cell(6, 0));
        // Points at what is holding it, which is what a rejection would need
        // in order to explain itself.
        CHECK(rejection->blockedBy != 0);
    }
    CHECK(scheduler.inFlight().size() == 1);

    // Once the holder finishes, the cell frees up.
    static_cast<void>(scheduler.advance(0.6f));
    CHECK(scheduler.idle());
    // Re-planned against the new state, since the rock now sits there; this
    // stands in for the session re-planning a queued command.
    ActionReservations elsewhere;
    elsewhere.writes.push_back({
        .cell = cell(7, 0), .firstStep = 0, .lastStep = std::nullopt });
    CHECK(started(scheduler.tryStart(intruder, elsewhere)));
}

void testCommitsAreDeltasNotWholeStates()
{
    TEST("commitsAreDeltasNotWholeStates");
    // Both plans captured `before` at the same moment, so each one's `after`
    // still shows the other entity where it started. Assigning either wholesale
    // would revert the other; committing deltas does not.
    ActionScheduler scheduler;
    scheduler.reset(twoRocks(), 0.1f);

    const auto [first, firstClaims] =
        movePlan(scheduler.state(), 0, cell(6, 0), 0.1f);
    const auto [second, secondClaims] =
        movePlan(scheduler.state(), 1, cell(6, 9), 0.1f);
    CHECK(first.after.movables[1].cell == cell(5, 9));   // stale by design
    CHECK(second.after.movables[0].cell == cell(5, 0));

    CHECK(started(scheduler.tryStart(first, firstClaims)));
    CHECK(started(scheduler.tryStart(second, secondClaims)));

    const std::vector<std::size_t> done = scheduler.advance(0.15f);
    CHECK(done.size() == 2);
    CHECK(scheduler.state().movables[0].cell == cell(6, 0));
    CHECK(scheduler.state().movables[1].cell == cell(6, 9));
}

void testCompletionOrderIsDeterministic()
{
    TEST("completionOrderIsDeterministic");
    // Two actions ending in the same tick must commit in a reproducible order,
    // or a bug involving both would not reproduce.
    const auto run = [] {
        ActionScheduler scheduler;
        scheduler.reset(twoRocks(), 0.1f);
        const auto [a, aClaims] =
            movePlan(scheduler.state(), 0, cell(6, 0), 0.2f);
        const auto [b, bClaims] =
            movePlan(scheduler.state(), 1, cell(6, 9), 0.1f);
        static_cast<void>(scheduler.tryStart(a, aClaims));
        static_cast<void>(scheduler.tryStart(b, bClaims));
        return scheduler.advance(0.5f);
    };
    const std::vector<std::size_t> first = run();
    CHECK(first.size() == 2);
    CHECK(run() == first);
    // The one that ran out earliest commits first: b (0.1s) before a (0.2s).
    if (first.size() == 2) {
        CHECK(first[0] == 2);
        CHECK(first[1] == 1);
    }
}

void testSharedClockPlacesClaims()
{
    TEST("sharedClockPlacesClaims");
    ActionScheduler scheduler;
    scheduler.reset(twoRocks(), 0.1f);
    CHECK(scheduler.currentStep() == 0);

    // A long-running action claims a cell only from its fourth step onward.
    ActionPlan slow;
    slow.before = scheduler.state();
    slow.after = slow.before;
    slow.after.movables[0].cell = cell(9, 0);
    slow.durationSeconds = 1.0f;
    ActionReservations late;
    late.writes.push_back({
        .cell = cell(9, 0), .firstStep = 4, .lastStep = std::nullopt });
    CHECK(started(scheduler.tryStart(slow, late)));

    // Something wanting that cell right now is fine - the block is nowhere
    // near it yet and will be gone from where it is.
    ActionReservations nowAndGone;
    nowAndGone.writes.push_back({
        .cell = cell(9, 0), .firstStep = 0, .lastStep = 1 });
    CHECK(started(scheduler.tryStart(slow, nowAndGone)));

    // But three steps in, the same claim lands on top of it.
    static_cast<void>(scheduler.advance(0.3f));
    CHECK(scheduler.currentStep() == 3);
    CHECK(!started(scheduler.tryStart(slow, nowAndGone)));
}

void testResetClearsEverything()
{
    TEST("resetClearsEverything");
    ActionScheduler scheduler;
    scheduler.reset(twoRocks(), 0.1f);
    const auto [plan, claims] =
        movePlan(scheduler.state(), 0, cell(6, 0), 0.5f);
    CHECK(started(scheduler.tryStart(plan, claims)));
    static_cast<void>(scheduler.advance(0.2f));
    CHECK(!scheduler.idle());

    scheduler.reset(twoRocks(), 0.1f);
    CHECK(scheduler.idle());
    CHECK(scheduler.currentStep() == 0);
    CHECK(scheduler.state().movables[0].cell == cell(5, 0));
    // The old action's claims went with it.
    CHECK(started(scheduler.tryStart(plan, claims)));
}

void testZeroDurationActionCompletesImmediately()
{
    TEST("zeroDurationActionCompletesImmediately");
    // Mirror activation is instant until its presentation timeline is
    // installed, so a zero-length action has to commit rather than hang.
    ActionScheduler scheduler;
    scheduler.reset(twoRocks(), 0.1f);
    const auto [plan, claims] =
        movePlan(scheduler.state(), 0, cell(6, 0), 0.0f);
    CHECK(started(scheduler.tryStart(plan, claims)));
    const std::vector<std::size_t> done = scheduler.advance(0.0f);
    CHECK(done.size() == 1);
    CHECK(scheduler.idle());
    CHECK(scheduler.state().movables[0].cell == cell(6, 0));
}

} // namespace

int main()
{
    testIndependentActionsRunTogether();
    testConflictingActionIsRefused();
    testCommitsAreDeltasNotWholeStates();
    testCompletionOrderIsDeterministic();
    testSharedClockPlacesClaims();
    testResetClearsEverything();
    testZeroDurationActionCompletesImmediately();

    if (failures == 0) {
        std::cout << "ActionSchedulerTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ActionSchedulerTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
