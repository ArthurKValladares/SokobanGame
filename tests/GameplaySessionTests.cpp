// Headless tests for gameplay orchestration between input commands and the
// pure Rules module. No SDL, Vulkan, rendering, or animation dependencies.

#include "engine/GameplaySession.hpp"

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
    CHECK(session.state().player == cell(1, 0, 1));
    CHECK(session.activeAction().after.player == cell(2, 0, 1));
    CHECK(session.activeAction().facingDirection == MoveDirection::Right);

    session.advanceActiveAction(0.1f);
    CHECK(!session.activeActionComplete());
    CHECK(session.state().player == cell(1, 0, 1));

    session.advanceActiveAction(0.1f);
    CHECK(session.activeActionComplete());
    session.completeActiveAction();
    CHECK(!session.moving());
    CHECK(session.state().player == cell(2, 0, 1));
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
    CHECK(session.activeAction().after.player == cell(2, 0, 1));
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
    finishAction(session);
    CHECK(session.state().player == cell(2, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().reversed);
    CHECK(session.activeAction().facingDirection == MoveDirection::Right);
    finishAction(session);
    CHECK(session.state().player == cell(1, 0, 1));
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
    CHECK(session.state().player == cell(3, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(2, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(1, 0, 1));

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
    CHECK(session.state().player == cell(2, 0, 1));

    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(3, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(2, 0, 1));
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
    CHECK(session.state().player == cell(1, 0, 1));
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(2, 0, 1));
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
    CHECK(session.activeAction().after.player == cell(1, 0, 1));
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
    CHECK(session.state().playerDead);

    session.queueRestart();
    session.queueMove(MoveDirection::Left);
    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    CHECK(session.activeAction().reversed);
    finishAction(session);
    CHECK(!session.state().playerDead);
    CHECK(session.state().player == cell(0, 0, 1));
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
    CHECK(session.state().player == cell(3, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(2, 0, 1));

    session.queueMove(MoveDirection::Left);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(1, 0, 1));

    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(2, 0, 1));
    session.queueUndo();
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
    CHECK(session.state().player == cell(1, 0, 1));
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
    CHECK(saved.state.player == cell(3, 0, 1));
    CHECK(saved.playerMoveCount == 2);

    GameplaySession restored;
    CHECK(restored.restore(level, saved));
    CHECK(restored.snapshot() == saved);
    CHECK(restored.undoCount() == 2);

    restored.queueUndo();
    CHECK(restored.tryStartNextAction(level, {}));
    finishAction(restored);
    CHECK(restored.state().player == cell(2, 0, 1));
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
    corrupted.undoStack.front().before.player.x += 1;

    GameplaySession target;
    target.reset(level);
    const GameplaySession::Snapshot beforeRestore = target.snapshot();
    CHECK(!target.restore(level, corrupted));
    CHECK(target.snapshot() == beforeRestore);

    corrupted = source.snapshot();
    corrupted.undoStack.front().after.player.x += 10;
    corrupted.state = corrupted.undoStack.front().after;
    CHECK(!target.restore(level, corrupted));
    CHECK(target.snapshot() == beforeRestore);
}

} // namespace

int main()
{
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

    if (failures == 0) {
        std::cout << "GameplaySessionTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "GameplaySessionTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
