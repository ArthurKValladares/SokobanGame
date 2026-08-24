#include "engine/GameplayLoop.hpp"
#include "engine/Time.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
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
    return Level::loadFromLines(lines, "gameplay loop test");
}

void testOpposingDirectionsAreNeutral()
{
    TEST("opposingDirectionsAreNeutral");
    GameplayLoop::InputFrame input {
        .up = { .pressed = true, .down = true },
        .down = { .pressed = true, .down = true },
        .left = { .pressed = true, .down = true },
        .right = { .pressed = true, .down = true },
    };
    CHECK(!GameplayLoop::pressedVertical(input).has_value());
    CHECK(!GameplayLoop::pressedHorizontal(input).has_value());
    CHECK(!GameplayLoop::heldVertical(input).has_value());
    CHECK(!GameplayLoop::heldHorizontal(input).has_value());
}

void testSimulationTimingClampsLongFrames()
{
    TEST("simulationTimingClampsLongFrames");
    SimulationTiming timing;

    CHECK(timing.frameDelta(1.0f / 60.0f) == 1.0f / 60.0f);
    CHECK(timing.frameDelta(5.0f) ==
        SimulationTiming::maximumDeltaSeconds);
    CHECK(timing.frameDelta(-1.0f) == 0.0f);
    CHECK(timing.frameDelta(
        std::numeric_limits<float>::infinity()) == 0.0f);
}

void testSimulationTimingResetsAcrossMinimize()
{
    TEST("simulationTimingResetsAcrossMinimize");
    SimulationTiming timing;

    timing.setSuspended(SimulationSuspension::Minimized, true);
    CHECK(timing.suspended());
    CHECK(timing.frameDelta(30.0f) == 0.0f);
    CHECK(timing.frameDelta(1.0f / 60.0f) == 0.0f);

    timing.setSuspended(SimulationSuspension::Minimized, false);
    CHECK(!timing.suspended());
    CHECK(timing.frameDelta(30.0f) == 0.0f);
    CHECK(timing.frameDelta(1.0f / 60.0f) == 1.0f / 60.0f);
}

void testSimulationTimingTracksOverlappingSuspensions()
{
    TEST("simulationTimingTracksOverlappingSuspensions");
    SimulationTiming timing;

    timing.setSuspended(SimulationSuspension::Backgrounded, true);
    timing.setSuspended(SimulationSuspension::Minimized, true);
    CHECK(timing.suspended());
    CHECK(timing.frameDelta(60.0f) == 0.0f);

    timing.setSuspended(SimulationSuspension::Backgrounded, false);
    CHECK(timing.suspended());
    CHECK(timing.frameDelta(60.0f) == 0.0f);

    timing.setSuspended(SimulationSuspension::Minimized, false);
    CHECK(!timing.suspended());
    CHECK(timing.frameDelta(60.0f) == 0.0f);
    CHECK(timing.frameDelta(0.02f) == 0.02f);
}

void testSimulationTimingObservesTransientSuspendCycle()
{
    TEST("simulationTimingObservesTransientSuspendCycle");
    SimulationTiming timing;

    timing.setSuspended(SimulationSuspension::Minimized, true);
    timing.setSuspended(SimulationSuspension::Minimized, false);
    CHECK(!timing.suspended());
    CHECK(timing.frameDelta(30.0f) == 0.0f);
    CHECK(timing.frameDelta(30.0f) ==
        SimulationTiming::maximumDeltaSeconds);
}

void testRenderedPlayerNeverGoesBackwards()
{
    TEST("renderedPlayerNeverGoesBackwards");
    // A frame in which the player is drawn behind where the previous frame drew
    // them reads as a flicker, and it is invisible to every other test here -
    // the committed state is right, the action sequence is right, and only the
    // sampled position between them is wrong.
    //
    // The dt is deliberately not a divisor of the step duration, so frames land
    // part-way through actions and across completion boundaries rather than
    // neatly on them. That is where the presentation gets resynchronised, and
    // where a whole-world sync can stamp on an action still in flight.
    const Level level = makeLevel({
        { "..........", ".........." },
        { "C         ", "          " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.15f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::InputFrame holdRight {
        .right = { .pressed = true, .down = true },
    };
    const GameplayLoop::InputFrame holdDown {
        .down = { .pressed = true, .down = true },
    };

    // A frame can only carry the player dt/stepDuration of a tile, so anything
    // approaching a whole tile in one frame is a snap rather than motion.
    const float perFrame = (1.0f / 60.0f) / 0.15f;
    const float tolerance = perFrame * 3.0f;

    Vec3 previous = presentation.players().front().motion.renderPosition;
    float worst = 0.0f;
    int jumps = 0;
    float travelled = 0.0f;
    for (int frame = 0; frame < 90; ++frame) {
        // Turning corners, which is where the video flickers: a direction
        // change ends one action and starts another on the same frame.
        const bool goRight = (frame / 10) % 2 == 0;
        GameplayLoop::update(
            level,
            session,
            presentation,
            goRight ? holdRight : holdDown,
            1.0f / 60.0f,
            false);

        const Vec3 at = presentation.players().front().motion.renderPosition;
        const float moved = std::abs(at.x - previous.x) +
            std::abs(at.y - previous.y) + std::abs(at.z - previous.z);
        travelled += moved;
        if (moved > tolerance) {
            ++jumps;
            worst = std::max(worst, moved);
        }
        previous = at;
    }
    if (jumps != 0) {
        std::cerr << "          rendered position jumped on " << jumps
                  << " frame(s), worst " << worst << " tiles (tolerance "
                  << tolerance << ")\n";
    }
    CHECK(jumps == 0);
    // Guards against passing because the player never moved at all.
    CHECK(travelled > 3.0f);
}

void testChainedSlideIsDrawnTileByTile()
{
    TEST("chainedSlideIsDrawnTileByTile");
    // A chained slide owns one motion track per leg, and `seekAction` used to
    // apply all of them. The last one won, and a track whose leg had not begun
    // set the entity to *that* leg's starting cell - so a block one tile into a
    // five-tile slide was drawn at the start of the final leg, which is to say
    // at its destination, for the whole slide. Nothing caught it, because the
    // committed state and the plan were both correct; only the sampled position
    // between them was wrong.
    const Level level = makeLevel({
        { "..........", ".........." },
        { "CI      # ", "          " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.15f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::InputFrame push {
        .right = { .pressed = true, .down = true },
    };
    const GameplayLoop::InputFrame idle {};

    GameplayLoop::update(level, session, presentation, push, 1.0f / 60.0f, false);
    float previous = presentation.movables().front().renderPosition.x;
    float worst = 0.0f;
    int jumps = 0;
    // Only the push is driven; the rest is the slide playing out on its own.
    for (int frame = 0; frame < 20; ++frame) {
        GameplayLoop::update(
            level, session, presentation, idle, 1.0f / 60.0f, false);
        const float x = presentation.movables().front().renderPosition.x;
        const float moved = std::abs(x - previous);
        // A frame carries dt/stepDuration of a tile; a whole tile is a snap.
        if (moved > 0.4f) {
            ++jumps;
            worst = std::max(worst, moved);
        }
        previous = x;
    }
    if (jumps != 0) {
        std::cerr << "          block position jumped on " << jumps
                  << " frame(s), worst " << worst << " tiles\n";
    }
    CHECK(jumps == 0);
    // Travelling, but nowhere near the far wall yet - if it had teleported to
    // its destination this would already be 7.
    CHECK(previous > 2.0f);
    CHECK(previous < 5.0f);
}

void testMoveAdvancesSessionAndPresentation()
{
    TEST("moveAdvancesSessionAndPresentation");
    const Level level = makeLevel({ { "..." }, { "C  " } });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.1f,
        false);
    CHECK(result.stateCommitted);
    CHECK(!result.screenSolved);
    CHECK(!result.mirrorActivated);
    CHECK(session.state().players[0].cell == (GridPosition3 { 1, 0, 1 }));
    CHECK(session.playerMoveCount() == 1);
}

void testMirrorInputCommitsAnInstantAction()
{
    TEST("mirrorInputCommitsAnInstantAction");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "     ", "     ", "  3  ", "     ", "  C  " },
    });
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .interactPressed = true },
        0.01f,
        false);
    CHECK(result.stateCommitted);
    CHECK(result.mirrorActivated);
    const std::vector<GridPosition3> expectedDestinations {
        GridPosition3 { 0, 2, 1 },
    };
    CHECK(result.mirrorSwapDestinations == expectedDestinations);
    CHECK(session.state().players[0].cell == (GridPosition3 { 0, 2, 1 }));
    CHECK(session.playerMoveCount() == 0);
    CHECK(session.undoCount() == 1);
}

void testRejectedMirrorInputDoesNotEmitActivation()
{
    TEST("rejectedMirrorInputDoesNotEmitActivation");
    const Level level = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "C    ", "     ", "  3  ", "     ", "     " },
    });
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .interactPressed = true },
        0.01f,
        false);

    CHECK(!result.mirrorActivated);
    CHECK(result.mirrorSwapDestinations.empty());
    CHECK(!result.stateCommitted);
    CHECK(session.state().players[0].cell == (GridPosition3 { 0, 0, 1 }));
    CHECK(session.undoCount() == 0);
}

void testSolvedScreenAndDraftOutcomesDiffer()
{
    TEST("solvedScreenAndDraftOutcomesDiffer");
    const Level level = makeLevel({ { ".." }, { "CE" } });

    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    GameplayLoop::UpdateResult result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.1f,
        false);
    CHECK(result.screenSolved);
    CHECK(!result.draftSolved);
    CHECK(!result.stateCommitted);

    session.reset(level);
    presentation.resetEntities(session.state());
    result = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.1f,
        true);
    CHECK(!result.screenSolved);
    CHECK(result.draftSolved);
}

void testMirrorDuplicationRequiresEveryPlayerOnAnEnd()
{
    TEST("mirrorDuplicationRequiresEveryPlayerOnAnEnd");
    auto activate = [](const Level& level) {
        GameplaySession session;
        session.reset(level);
        GameplayPresentation presentation;
        presentation.resetEntities(session.state());
        return GameplayLoop::update(
            level,
            session,
            presentation,
            { .interactPressed = true },
            0.01f,
            false);
    };

    const Level oneEnd = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "E 3  ", "     ", "  C  ", "     ", "  2  " },
    });
    const GameplayLoop::UpdateResult incomplete = activate(oneEnd);
    CHECK(incomplete.mirrorActivated);
    CHECK(incomplete.mirrorSwapDestinations.size() == 2);
    CHECK(!incomplete.screenSolved);
    CHECK(incomplete.stateCommitted);

    const Level twoEnds = makeLevel({
        { ".....", ".....", ".....", ".....", "....." },
        { "E 3  ", "     ", "  C  ", "     ", "  2 E" },
    });
    const GameplayLoop::UpdateResult complete = activate(twoEnds);
    CHECK(complete.mirrorActivated);
    CHECK(complete.mirrorSwapDestinations.size() == 2);
    CHECK(complete.screenSolved);
    CHECK(!complete.stateCommitted);
}

} // namespace

int main()
{
    testOpposingDirectionsAreNeutral();
    testSimulationTimingClampsLongFrames();
    testSimulationTimingResetsAcrossMinimize();
    testSimulationTimingTracksOverlappingSuspensions();
    testSimulationTimingObservesTransientSuspendCycle();
    testRenderedPlayerNeverGoesBackwards();
    testChainedSlideIsDrawnTileByTile();
    testMoveAdvancesSessionAndPresentation();
    testMirrorInputCommitsAnInstantAction();
    testRejectedMirrorInputDoesNotEmitActivation();
    testSolvedScreenAndDraftOutcomesDiffer();
    testMirrorDuplicationRequiresEveryPlayerOnAnEnd();

    if (failures == 0) {
        std::cout << "GameplayLoopTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GameplayLoopTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
