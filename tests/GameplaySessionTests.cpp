// Headless tests for gameplay orchestration between input commands and the
// pure Rules module. No SDL, Vulkan, rendering, or animation dependencies.

#include "engine/GameplaySession.hpp"

#include <algorithm>
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
        std::cerr << "FAIL [" << currentTest << "] line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

Level makeLevel(const std::vector<std::vector<std::string>>& layers)
{
    std::vector<std::string> lines;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        lines.push_back("@layer " + std::to_string(layer));
        lines.insert(lines.end(), layers[layer].begin(), layers[layer].end());
        lines.emplace_back();
    }
    return Level::loadFromLines(lines, "test level");
}

GridPosition3 cell(int x, int y, int z)
{
    return { x, y, z };
}

void finishAction(GameplaySession& session)
{
    session.advanceActiveAction(session.activeActionDuration());
    CHECK(session.activeActionComplete());
    session.completeActiveAction();
}

// Runs the world until nothing is left in flight, admitting whatever becomes
// admissible on the way.
//
// Advances in whole completion boundaries rather than fixed slices, which is
// what `GameplayLoop` does and for the same reason: stepping over a completion
// commits an action late and plans whatever follows from a state that never
// existed.
void runUntilIdle(GameplaySession& session, const Level& level, int maxSteps = 256)
{
    for (int guard = 0; guard < maxSteps; ++guard) {
        // Admit first, then advance - the same order `GameplayLoop` uses.
        // Testing `moving()` before admitting would stop dead on a world that
        // is idle but has ambient motion still owed to it, which is exactly the
        // state a belt is in between one rider's step and the next.
        while (session.tryStartNextAction(level, {})) {
        }
        if (!session.moving()) {
            return;
        }
        session.advanceActiveAction(
            std::max(session.timeToNextCompletion(), 0.0001f));
        if (session.anyActionComplete()) {
            session.completeActiveAction();
        }
    }
}

void testMoveCommitsAfterAnimation()
{
    TEST("moveCommitsAfterAnimation");
    const Level level = makeLevel({
        { "...." },
        { " C  " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.2f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.moving());
    CHECK(session.state().players[0].cell == cell(1, 0, 1));
    CHECK(session.activeAction().after.players[0].cell == cell(2, 0, 1));
    CHECK(session.activeAction().facingDirection == MoveDirection::Right);

    session.advanceActiveAction(0.1f);
    CHECK(!session.activeActionComplete());
    CHECK(session.state().players[0].cell == cell(1, 0, 1));

    session.advanceActiveAction(0.1f);
    CHECK(session.activeActionComplete());
    session.completeActiveAction();
    CHECK(!session.moving());
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
    CHECK(session.historySize() == 1);
}

void testPushMetadata()
{
    TEST("pushMetadata");
    const Level level = makeLevel({
        { "....." },
        { " CR  " },
    });
    GameplaySession session;
    session.reset(level);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().playerPushing);
    CHECK(session.activeAction().after.players[0].cell == cell(2, 0, 1));
    CHECK(session.activeAction().after.movables[0].cell == cell(3, 0, 1));
}

void testUndoRoundTrip()
{
    TEST("undoRoundTrip");
    const Level level = makeLevel({
        { "...." },
        { " C  " },
    });
    GameplaySession session;
    session.reset(level);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    session.setActiveActionPresentation({
        .durationSeconds = 0.4f,
        .animations = {
            {
                .target = {
                    EntityKind::Player,
                    session.activeAction().before.players[0].id,
                },
                .initialUse = AnimationUse::PlayerIdle,
                .segments = {},
            },
        },
    });
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().reversed);
    CHECK(session.activeAction().facingDirection == MoveDirection::Right);
    CHECK(session.activeAction().presentation.animations.size() == 1);
    CHECK(session.activeActionDuration() == 0.4f);
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(1, 0, 1));
    CHECK(session.historySize() == 2);
}

void testContiguousUndoWalksOriginalHistory()
{
    TEST("contiguousUndoWalksOriginalHistory");
    const Level level = makeLevel({
        { "....." },
        { " C   " },
    });
    GameplaySession session;
    session.reset(level);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(3, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(1, 0, 1));

    session.queueUndo();
    CHECK(!session.tryStartNextAction(level, {}));
}

void testRestart()
{
    TEST("restart");
    const Level level = makeLevel({
        { "...." },
        { " C  " },
    });
    GameplaySession session;
    session.reset(level);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);

    session.queueRestart();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().after == rules::initialState(level));
    finishAction(session);
    CHECK(session.state() == rules::initialState(level));
}

void testUndoPausesAutomaticMotion()
{
    TEST("undoPausesAutomaticMotion");
    const Level level = makeLevel({
        { "....." },
        { " C>  " },
    });
    GameplaySession session;
    session.reset(level);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));

    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(3, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
    CHECK(!session.tryStartNextAction(level, {}));
}

void testActionTimingClampsAndIgnoresNegativeDelta()
{
    TEST("actionTimingClampsAndIgnoresNegativeDelta");
    const Level level = makeLevel({
        { "..." },
        { "C  " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.25f);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));

    CHECK(session.activeActionRemainingSeconds() == 0.25f);
    session.advanceActiveAction(-1.0f);
    CHECK(session.activeActionRemainingSeconds() == 0.25f);
    session.advanceActiveAction(10.0f);
    CHECK(session.activeActionRemainingSeconds() == 0.0f);
    CHECK(session.activeActionComplete());
    session.completeActiveAction();
    const std::size_t historySize = session.historySize();
    session.completeActiveAction();
    CHECK(session.historySize() == historySize);
}

void testQueuedCommandsWaitForActiveAction()
{
    TEST("queuedCommandsWaitForActiveAction");
    const Level level = makeLevel({
        { "...." },
        { "C   " },
    });
    GameplaySession session;
    session.reset(level);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));

    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(1, 0, 1));
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
}

void testBlockedQueuedCommandDoesNotStarveNextCommand()
{
    TEST("blockedQueuedCommandDoesNotStarveNextCommand");
    const Level level = makeLevel({
        { "..." },
        { "C  " },
    });
    GameplaySession session;
    session.reset(level);
    session.queueMove(MoveDirection::Up);
    session.queueMove(MoveDirection::Right);

    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().after.players[0].cell == cell(1, 0, 1));
}

void testDeadPlayerDiscardsCommandsUntilUndo()
{
    TEST("deadPlayerDiscardsCommandsUntilUndo");
    const Level level = makeLevel({
        { ".W" },
        { "C " },
    });
    GameplaySession session;
    session.reset(level);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].dead);

    session.queueRestart();
    session.queueMove(MoveDirection::Left);
    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().reversed);
    finishAction(session);
    CHECK(!session.state().players[0].dead);
    CHECK(session.state().players[0].cell == cell(0, 0, 1));
}

void testRestartCanBeUndone()
{
    TEST("restartCanBeUndone");
    const Level level = makeLevel({
        { "...." },
        { "C   " },
    });
    GameplaySession session;
    session.reset(level);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    const GameState moved = session.state();

    session.queueRestart();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state() == rules::initialState(level));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state() == moved);
}

void testNewMoveAfterUndoCreatesCleanHistoryBranch()
{
    TEST("newMoveAfterUndoCreatesCleanHistoryBranch");
    const Level level = makeLevel({
        { "....." },
        { " C   " },
    });
    GameplaySession session;
    session.reset(level);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(3, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));

    session.queueMove(MoveDirection::Left);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(1, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(1, 0, 1));
    session.queueUndo();
    CHECK(!session.tryStartNextAction(level, {}));
}

void testPlayerMoveCountTracksUndoAndRestart()
{
    TEST("playerMoveCountTracksUndoAndRestart");
    const Level level = makeLevel({
        { "....." },
        { " C   " },
    });
    GameplaySession session;
    session.reset(level);
    CHECK(session.playerMoveCount() == 0);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.playerMoveCount() == 1);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.playerMoveCount() == 2);

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.playerMoveCount() == 1);

    session.queueRestart();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.playerMoveCount() == 0);

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.playerMoveCount() == 1);
}

void testSnapshotRestoresExactStateAndUndoStack()
{
    TEST("snapshotRestoresExactStateAndUndoStack");
    const Level level = makeLevel({
        { "....." },
        { " C   " },
    });
    GameplaySession original;
    original.reset(level);
    original.queueMove(MoveDirection::Right);
    CHECK(original.tryStartNextAction(level, {}));
    finishAction(original);
    original.queueMove(MoveDirection::Right);
    CHECK(original.tryStartNextAction(level, {}));
    finishAction(original);

    const GameplaySession::Snapshot saved = original.snapshot();
    CHECK(saved.undoStack.size() == 2);
    CHECK(saved.state.players[0].cell == cell(3, 0, 1));
    CHECK(saved.playerMoveCount == 2);

    GameplaySession restored;
    CHECK(restored.restore(level, saved));
    CHECK(restored.snapshot() == saved);
    CHECK(restored.undoCount() == 2);

    restored.queueUndo();
    CHECK(restored.tryStartNextAction(level, {}));
    finishAction(restored);
    CHECK(restored.state().players[0].cell == cell(2, 0, 1));
    CHECK(restored.playerMoveCount() == 1);
    CHECK(restored.undoCount() == 1);
}

void testResetClearsUndoStackForNewScreen()
{
    TEST("resetClearsUndoStackForNewScreen");
    const Level firstScreen = makeLevel({
        { "...." },
        { " C  " },
    });
    const Level nextScreen = makeLevel({
        { "..." },
        { "C  " },
    });
    GameplaySession session;
    session.reset(firstScreen);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(firstScreen, {}));
    finishAction(session);
    CHECK(session.undoCount() == 1);

    session.reset(nextScreen);
    CHECK(session.undoCount() == 0);
    CHECK(session.snapshot().undoStack.empty());
    CHECK(session.state() == rules::initialState(nextScreen));
    session.queueUndo();
    CHECK(!session.tryStartNextAction(nextScreen, {}));
}

void testSnapshotRestoreAcceptsRestartHistory()
{
    TEST("snapshotRestoreAcceptsRestartHistory");
    const Level level = makeLevel({
        { "...." },
        { " C  " },
    });
    GameplaySession original;
    original.reset(level);
    original.queueMove(MoveDirection::Right);
    CHECK(original.tryStartNextAction(level, {}));
    finishAction(original);
    const GameState moved = original.state();

    original.queueRestart();
    CHECK(original.tryStartNextAction(level, {}));
    finishAction(original);
    CHECK(original.state() == rules::initialState(level));

    GameplaySession restored;
    CHECK(restored.restore(level, original.snapshot()));
    CHECK(restored.undoCount() == 2);
    restored.queueUndo();
    CHECK(restored.tryStartNextAction(level, {}));
    finishAction(restored);
    CHECK(restored.state() == moved);
    CHECK(restored.playerMoveCount() == 1);
}

void testInvalidSnapshotIsRejectedWithoutMutation()
{
    TEST("invalidSnapshotIsRejectedWithoutMutation");
    const Level level = makeLevel({
        { "...." },
        { " C  " },
    });
    GameplaySession source;
    source.reset(level);
    source.queueMove(MoveDirection::Right);
    CHECK(source.tryStartNextAction(level, {}));
    finishAction(source);

    GameplaySession::Snapshot corrupted = source.snapshot();
    corrupted.undoStack.front().before.players[0].cell.x += 1;

    GameplaySession target;
    target.reset(level);
    const GameplaySession::Snapshot beforeRestore = target.snapshot();
    CHECK(!target.restore(level, corrupted));
    CHECK(target.snapshot() == beforeRestore);

    corrupted = source.snapshot();
    corrupted.undoStack.front().after.players[0].cell.x += 10;
    corrupted.state = corrupted.undoStack.front().after;
    CHECK(!target.restore(level, corrupted));
    CHECK(target.snapshot() == beforeRestore);
}

void testMirrorActionIsInstantUndoableAndRestorable()
{
    TEST("mirrorActionIsInstantUndoableAndRestorable");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "     ", "     ", "  3  ", "     ", "  C  " },
    });
    GameplaySession session;
    session.reset(level);
    const GameState initial = session.state();

    session.queueMirror();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeActionDuration() == 0.0f);
    CHECK(session.activeAction().after.players[0].cell == cell(0, 2, 1));
    finishAction(session);
    CHECK(session.playerMoveCount() == 0);
    CHECK(session.undoCount() == 1);

    GameplaySession restored;
    CHECK(restored.restore(level, session.snapshot()));
    CHECK(restored.state() == session.state());
    restored.queueUndo();
    CHECK(restored.tryStartNextAction(level, {}));
    finishAction(restored);
    CHECK(restored.state() == initial);
}

void testMirrorDuplicationIsInstantUndoableAndRestorable()
{
    TEST("mirrorDuplicationIsInstantUndoableAndRestorable");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "  3  ", "     ", "  C  ", "     ", "  2  " },
    });
    GameplaySession session;
    session.reset(level);
    const GameState initial = session.state();

    session.queueMirror();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().after.players.size() == 2);
    finishAction(session);
    CHECK(session.state().players.size() == 2);
    CHECK(session.playerMoveCount() == 0);

    GameplaySession restored;
    CHECK(restored.restore(level, session.snapshot()));
    CHECK(restored.state().players.size() == 2);
    restored.queueUndo();
    CHECK(restored.tryStartNextAction(level, {}));
    finishAction(restored);
    CHECK(restored.state() == initial);
}

} // namespace

void testBeltCarriesARiderOneActionPerStep()
{
    TEST("beltCarriesARiderOneActionPerStep");
    // Belt motion is ambient and never terminates, so a ride is planned one
    // step at a time and re-planned, rather than committed as a chain the way a
    // slide is. That keeps a rider's claims one cell and one interval long -
    // without which the area around any belt would be permanently unusable.
    //
    // Reached by pushing, because a movable's start tile resolves to Air and a
    // rock standing on a conveyor cannot be authored directly.
    const Level level = makeLevel({
        { "........" },
        { "CR>>>>>#" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    // The push alone. A belt gives no momentum, so there is no consequence
    // slide to plan alongside it.
    CHECK(session.inFlight().size() == 1);
    finishAction(session);
    CHECK(session.state().movables[0].cell == cell(2, 0, 1));
    CHECK(rules::hasPendingMotion(level, session.state()));

    // From here the belt carries it, one action per step, until the wall.
    runUntilIdle(session, level);
    CHECK(session.state().movables[0].cell == cell(6, 0, 1));
    CHECK(session.state().players[0].cell == cell(1, 0, 1));
    // It comes to rest pinned against the wall while still standing on the
    // belt, so `hasPendingMotion` goes on saying yes forever - it reports that
    // an entity is on a conveyor, not that the conveyor can move it. What stops
    // the scheduling loop is the planner: a ride that changes nothing is no
    // plan at all, so nothing is admitted and the world stays idle.
    CHECK(rules::hasPendingMotion(level, session.state()));
    CHECK(!session.moving());
    CHECK(!session.tryStartNextAction(level, {}));

    // One entry for the push and one per belt step, rather than a single
    // chained ride: four cells travelled, four actions.
    CHECK(session.undoCount() == 5);
    // The ride is not the player's doing, so it never counts as a move.
    CHECK(session.playerMoveCount() == 1);
}

// Plays a fixed input script to completion and returns everything a trace
// should be judged on.
struct ScriptedRun {
    GameState finalState;
    std::vector<GameplaySession::Action> committed;
    int playerMoveCount = 0;
};

[[nodiscard]] ScriptedRun runScript(
    const Level& level, const std::vector<MoveDirection>& script)
{
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    for (const MoveDirection input : script) {
        session.queueMove(input);
        runUntilIdle(session, level);
    }
    const GameplaySession::Snapshot snapshot = session.snapshot();
    return {
        snapshot.state,
        snapshot.undoStack,
        snapshot.playerMoveCount,
    };
}

void testGoldenTraceIsReproducible()
{
    TEST("goldenTraceIsReproducible");
    // The same inputs must produce the same actions - not merely the same
    // final position, but the same sequence of plans reaching it.
    //
    // Compared against a second run rather than against literals baked into
    // the test. A hard-coded expectation would pin the ordering of a hundred
    // fields nobody reads and break on every unrelated change; what actually
    // needs guarding is that nothing in the scheduler has become dependent on
    // anything other than the level, the state and the input. Concurrency is
    // exactly where that kind of dependence creeps in - commit order, clock
    // rounding, iteration over a table - and none of it shows up in the final
    // state alone.
    const Level level = makeLevel({
        { "........", "........" },
        { "CI     #", "        " },
    });
    const std::vector<MoveDirection> script {
        MoveDirection::Right,
        MoveDirection::Down,
        MoveDirection::Right,
        MoveDirection::Up,
        MoveDirection::Left,
    };

    const ScriptedRun first = runScript(level, script);
    const ScriptedRun second = runScript(level, script);

    // The whole plan sequence, compared exactly. `ActionPlan` has a defaulted
    // `operator==`, so this covers endpoints, move counts, push flags and
    // presentation together.
    CHECK(first.committed == second.committed);
    CHECK(first.finalState == second.finalState);
    CHECK(first.playerMoveCount == second.playerMoveCount);

    // Guards against the comparison passing because nothing happened.
    CHECK(first.committed.size() > 1);
    CHECK(first.playerMoveCount > 1);
    // The push really did set a slide off, so the trace covers the interesting
    // path rather than five plain steps.
    CHECK(first.finalState.movables[0].cell == cell(6, 0, 1));

    // A trace is only worth recording if it is also a valid history.
    GameplaySession restored;
    CHECK(restored.restore(
        level,
        GameplaySession::Snapshot {
            .state = first.finalState,
            .undoStack = first.committed,
            .playerMoveCount = first.playerMoveCount,
            .automaticMotionPaused = false,
        }));
}

void testQueuedCommandsGoAheadOfAmbientMotion()
{
    TEST("queuedCommandsGoAheadOfAmbientMotion");
    // The starvation rule. A belt rider releases its reservation at the end of
    // every step and immediately takes another, so a queued player action
    // waiting on a cell in the belt's path could be shut out for as long as the
    // belt runs. What prevents it is ordering: at every scheduling point the
    // command queue is drained before any new ambient action is started.
    const Level level = makeLevel({
        { "........", "........" },
        { "CR>>>>>#", "        " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    // Idle, with a rider on the belt owed a step.
    CHECK(!session.moving());
    CHECK(rules::hasPendingMotion(level, session.state()));

    session.queueMove(MoveDirection::Down);
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.inFlight().size() == 1);
    // The player's step, not the belt's - the queue was drained first. Had the
    // belt gone first this would be the rider's action instead, and on a long
    // enough belt the player would never get a turn.
    const GameplaySession::Action& started = session.inFlight().front().plan;
    CHECK(started.after.players[0].cell == cell(1, 1, 1));
    CHECK(started.after.movables[0].cell == cell(2, 0, 1));
}

void testAnEntityInFlightCannotBeTakenBySomethingElse()
{
    TEST("anEntityInFlightCannotBeTakenBySomethingElse");
    // The guarantee, enforced where it belongs. A block's destination is
    // settled at the moment it is pushed, so the block is spoken for until it
    // gets there, and nothing else may plan to move it.
    //
    // This was admitted before, and the reason is worth remembering: the
    // reservation table reasons about cells at instants, and by the time the
    // second push was tried it believed the block had left the cell it was
    // claimed on - while authoritative state, which is what planning reads,
    // still had the block sitting there because the slide had not committed.
    // Both were right on their own terms. Ownership is the rule neither could
    // express.
    const Level level = makeLevel({
        { "........" },
        { "CI     #" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    session.advanceActiveAction(session.timeToNextCompletion());
    session.completeActiveAction();
    // The push has landed; the slide is still travelling and owns the block.
    CHECK(session.moving());
    CHECK(session.state().movables[0].cell == cell(2, 0, 1));

    // Walking back into it would plan a second move for the same block.
    // Refused, and refused for that reason rather than on a cell.
    const auto before = session.admissionStats();
    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(level, {}));
    const auto after = session.admissionStats();
    CHECK(after.refusedByOwnership == before.refusedByOwnership + 1);
    CHECK(after.admitted == before.admitted);

    // Queued, not dropped: it runs once the slide has landed, and the block
    // ends exactly where the push said it would.
    runUntilIdle(session, level);
    CHECK(session.state().movables[0].cell == cell(6, 0, 1));
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
}

void testSnapshotMidFlightRestoresFromTheCommittedState()
{
    TEST("snapshotMidFlightRestoresFromTheCommittedState");
    // What lets checkpointing stop waiting for the world to go idle. A snapshot
    // holds the committed state and the stack chained to it, and an action in
    // flight has contributed to neither - so it is a valid save, and what it
    // loses is the action, not the consistency.
    const Level level = makeLevel({
        { "........" },
        { "CI     #" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    session.advanceActiveAction(session.timeToNextCompletion());
    session.completeActiveAction();
    // The push has committed; the slide it set off has not.
    CHECK(session.moving());

    const GameplaySession::Snapshot snapshot = session.snapshot();
    CHECK(snapshot.state.movables[0].cell == cell(2, 0, 1));

    GameplaySession restored;
    CHECK(restored.restore(level, snapshot));
    CHECK(!restored.moving());
    CHECK(restored.state() == snapshot.state);

    // The momentum survived in committed state, so the slide is simply planned
    // again and still runs to the wall.
    while (restored.tryStartNextAction(level, {})) {
    }
    runUntilIdle(restored, level);
    CHECK(restored.state().movables[0].cell == cell(6, 0, 1));
}

void testIceSlideIsSettledAtTheMomentOfThePush()
{
    TEST("iceSlideIsSettledAtTheMomentOfThePush");
    const Level level = makeLevel({
        { "........" },
        { "CI     #" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));

    // Two actions, one instant. The push is a single step so the player is
    // released after their own tile; the slide is a second plan that starts a
    // step later. Both were made from the same state, which is the whole of
    // what the guarantee asks for.
    CHECK(session.inFlight().size() == 2);
    const ActionScheduler::InFlight& push = session.inFlight()[0];
    const ActionScheduler::InFlight& slide = session.inFlight()[1];
    CHECK(push.causalGroup != 0);
    CHECK(slide.causalGroup == push.causalGroup);

    // The destination is settled before a single tile of travel has played:
    // the ice runs to the wall and the plan already says so.
    CHECK(slide.plan.after.movables[0].cell == cell(6, 0, 1));
    // Committed as one chain rather than re-decided every step.
    CHECK(slide.legs.size() > 1);
    CHECK(slide.legs.back() == slide.plan.after);
    // Admitted now, but not yet running - it is waiting out the push.
    CHECK(slide.elapsedSeconds < 0.0f);
    CHECK(push.elapsedSeconds == 0.0f);

    runUntilIdle(session, level);
    CHECK(session.state().movables[0].cell == cell(6, 0, 1));
    CHECK(session.state().players[0].cell == cell(1, 0, 1));
    // Nothing is left moving: the slide was spent inside its own action.
    CHECK(!rules::hasPendingMotion(level, session.state()));

    // Two actions to the scheduler, one to the player. Undo is the player's
    // idea of what happened, so the slide folded into the entry its push
    // opened.
    CHECK(session.undoCount() == 1);
    CHECK(session.playerMoveCount() == 1);
    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state() == rules::initialState(level));
    CHECK(session.playerMoveCount() == 0);
}

void testPlayerMovesAlongsideASlideItCannotDisturb()
{
    TEST("playerMovesAlongsideASlideItCannotDisturb");
    // The point of the whole reservation system: the player is released after
    // their own tile instead of being locked out for the length of the slide.
    const Level level = makeLevel({
        { "........", "........" },
        { "CI     #", "        " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.inFlight().size() == 2);

    // Finish only the push. The slide is still travelling.
    session.advanceActiveAction(session.timeToNextCompletion());
    session.completeActiveAction();
    CHECK(session.moving());

    // Stepping aside touches none of the cells the slide has left to cross, so
    // it is admitted rather than queued behind it.
    session.queueMove(MoveDirection::Down);
    CHECK(session.tryStartNextAction(level, {}));
    // Two now in flight at once: the slide, and a player step that started
    // while it was running.
    CHECK(session.inFlight().size() == 2);

    runUntilIdle(session, level);
    CHECK(session.state().players[0].cell == cell(1, 1, 1));
    CHECK(session.state().movables[0].cell == cell(6, 0, 1));
    // Counted on commit, not on admission.
    CHECK(session.playerMoveCount() == 2);
}

void testCommandRefusedByAClaimIsRequeuedNotLost()
{
    TEST("commandRefusedByAClaimIsRequeuedNotLost");
    // Queued, not rejected. A command the table refuses has to go back on the
    // queue and be retried, or a conflicting input would simply vanish.
    const Level level = makeLevel({
        { "........" },
        { "CI     #" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    session.advanceActiveAction(session.timeToNextCompletion());
    session.completeActiveAction();

    // Straight into the sliding block's path, so it cannot be admitted now.
    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(level, {}));
    CHECK(session.state().players[0].cell == cell(1, 0, 1));

    // Not consumed by the refusal: once the slide is done it runs.
    runUntilIdle(session, level);
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
    CHECK(session.playerMoveCount() == 2);
}

void testConcurrentPlayHistoryRoundTrips()
{
    TEST("concurrentPlayHistoryRoundTrips");
    // An undo entry records what its action changed, replayed onto the running
    // chain. Actions committing out of the order they were planned in must
    // still leave a stack that restores.
    const Level level = makeLevel({
        { "........", "........" },
        { "CI     #", "        " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    session.advanceActiveAction(session.timeToNextCompletion());
    session.completeActiveAction();
    // Committed while the slide it set off is still in flight, so this entry
    // lands in the stack between the push and the slide that folds into it.
    session.queueMove(MoveDirection::Down);
    CHECK(session.tryStartNextAction(level, {}));
    runUntilIdle(session, level);

    // Two entries, not three: the slide folded into the push that caused it
    // even though an unrelated step committed in between. That entry is what
    // the parallel group vector exists for, and folding into it moved an
    // endpoint the later entry was chained to - so the later one was rebased.
    CHECK(session.undoCount() == 2);
    CHECK(session.playerMoveCount() == 2);

    const GameplaySession::Snapshot snapshot = session.snapshot();
    // Every entry begins where the previous one ended, whatever order the
    // actions actually committed in. `restore` rejects any stack where that
    // fails to hold, so this is the invariant under test.
    GameState chained = rules::initialState(level);
    for (const GameplaySession::Action& entry : snapshot.undoStack) {
        CHECK(entry.before == chained);
        chained = entry.after;
    }
    CHECK(chained == session.state());

    GameplaySession restored;
    CHECK(restored.restore(level, snapshot));
    CHECK(restored.state() == session.state());
    CHECK(restored.playerMoveCount() == session.playerMoveCount());
    CHECK(restored.undoCount() == session.undoCount());

    // And the restored stack still unwinds to the opening state.
    for (std::size_t undos = restored.undoCount(); undos > 0; --undos) {
        restored.queueUndo();
        CHECK(restored.tryStartNextAction(level, {}));
        finishAction(restored);
    }
    CHECK(restored.state() == rules::initialState(level));
    CHECK(restored.playerMoveCount() == 0);
}

void testQueueIsBounded()
{
    TEST("queueIsBounded");
    // Buffered input is a courtesy, not a recording. Mashing during a long
    // action must not spool out as a burst of moves once it ends.
    const Level level = makeLevel({
        { "......" },
        { "C     " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);

    for (int i = 0; i < 5; ++i) {
        session.queueMove(MoveDirection::Right);
    }

    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    // Only the last two survived, so the player ends two cells along and not
    // five.
    CHECK(session.state().players[0].cell == cell(2, 0, 1));
    CHECK(!session.tryStartNextAction(level, {}));
}

void testStaleCommandsAreDropped()
{
    TEST("staleCommandsAreDropped");
    // Staleness is what stops a refused command retrying forever. A step into
    // the path of a long slide is refused every time it is tried, and must
    // eventually be given up on rather than played back into a world that has
    // moved on.
    const Level level = makeLevel({
        { "........" },
        { "CI     #" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.3f);

    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(level, {}));
    session.advanceActiveAction(session.timeToNextCompletion());
    session.completeActiveAction();

    // Into the block's path: refused now, and for as long as the slide runs.
    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(level, {}));

    runUntilIdle(session, level);

    // Dropped rather than played back, so the player stays where the push left
    // them.
    CHECK(!session.tryStartNextAction(level, {}));
    CHECK(session.state().players[0].cell == cell(1, 0, 1));

    // A command entered afterwards is honoured normally: staleness is measured
    // from when it was entered, not from some global age.
    session.queueMove(MoveDirection::Left);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == cell(0, 0, 1));
}

int main()
{
    testQueueIsBounded();
    testStaleCommandsAreDropped();
    testIceSlideIsSettledAtTheMomentOfThePush();
    testAnEntityInFlightCannotBeTakenBySomethingElse();
    testSnapshotMidFlightRestoresFromTheCommittedState();
    testBeltCarriesARiderOneActionPerStep();
    testQueuedCommandsGoAheadOfAmbientMotion();
    testGoldenTraceIsReproducible();
    testPlayerMovesAlongsideASlideItCannotDisturb();
    testCommandRefusedByAClaimIsRequeuedNotLost();
    testConcurrentPlayHistoryRoundTrips();
    testMoveCommitsAfterAnimation();
    testPushMetadata();
    testUndoRoundTrip();
    testContiguousUndoWalksOriginalHistory();
    testRestart();
    testUndoPausesAutomaticMotion();
    testActionTimingClampsAndIgnoresNegativeDelta();
    testQueuedCommandsWaitForActiveAction();
    testBlockedQueuedCommandDoesNotStarveNextCommand();
    testDeadPlayerDiscardsCommandsUntilUndo();
    testRestartCanBeUndone();
    testNewMoveAfterUndoCreatesCleanHistoryBranch();
    testPlayerMoveCountTracksUndoAndRestart();
    testSnapshotRestoresExactStateAndUndoStack();
    testResetClearsUndoStackForNewScreen();
    testSnapshotRestoreAcceptsRestartHistory();
    testInvalidSnapshotIsRejectedWithoutMutation();
    testMirrorActionIsInstantUndoableAndRestorable();
    testMirrorDuplicationIsInstantUndoableAndRestorable();

    if (failures == 0) {
        std::cout << "GameplaySessionTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "GameplaySessionTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
