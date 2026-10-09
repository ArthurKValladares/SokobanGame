#include "TestHarness.hpp"

#include "engine/GameplayLoop.hpp"
#include "engine/Time.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace sokoban;

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

void testSimulationTimingScalesOneSharedDelta()
{
    TEST("simulationTimingScalesOneSharedDelta");

    CHECK(std::abs(
        SimulationTiming::scaledDelta(0.05f, 0.1f) - 0.005f) < 0.000001f);
    CHECK(std::abs(
        SimulationTiming::scaledDelta(0.05f, 0.5f) - 0.025f) < 0.000001f);
    CHECK(std::abs(
        SimulationTiming::scaledDelta(0.025f, 2.0f) - 0.05f) < 0.000001f);
    CHECK(std::abs(
        SimulationTiming::scaledDelta(0.08f, 4.0f) - 0.32f) < 0.000001f);
    CHECK(std::abs(
        SimulationTiming::scaledDelta(0.016f, 10.0f) - 0.16f) < 0.000001f);
    CHECK(SimulationTiming::scaledDelta(-1.0f, 0.5f) == 0.0f);
    CHECK(SimulationTiming::scaledDelta(0.05f, 0.0f) == 0.0f);
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
        // Discarded on purpose: these three cases drive frames to watch
        // `presentation` move, and say nothing about what the step returned.
        // The rest of this file captures the result, because it checks it.
        static_cast<void>(GameplayLoop::update(
            level,
            session,
            presentation,
            goRight ? holdRight : holdDown,
            1.0f / 60.0f,
            false));

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

    static_cast<void>(GameplayLoop::update(
        level, session, presentation, push, 1.0f / 60.0f, false));
    float previous = presentation.movables().front().renderPosition.x;
    float worst = 0.0f;
    int jumps = 0;
    // Only the push is driven; the rest is the slide playing out on its own.
    for (int frame = 0; frame < 20; ++frame) {
        static_cast<void>(GameplayLoop::update(
            level, session, presentation, idle, 1.0f / 60.0f, false));
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

void testHeldMoveFollowsDirectlyBehindSlide()
{
    TEST("heldMoveFollowsDirectlyBehindSlide");
    const Level level = makeLevel({
        { "............." },
        { "CI          #" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    static_cast<void>(GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.01f,
        false));
    for (int frame = 1; frame < 35; ++frame) {
        static_cast<void>(GameplayLoop::update(
            level,
            session,
            presentation,
            { .right = { .down = true } },
            0.01f,
            false));
    }

    // The long slide has not committed yet, but held input has already walked
    // the player onto its released trail instead of waiting at the push cell.
    CHECK(session.moving());
    CHECK(session.state().movables[0].cell == (GridPosition3 { 2, 0, 1 }));
    CHECK(session.state().players[0].cell == (GridPosition3 { 2, 0, 1 }));
    CHECK(presentation.players()[0].motion.renderPosition.x > 2.0f);
}

void testCompletingActionPreservesConcurrentPresentation()
{
    TEST("completingActionPreservesConcurrentPresentation");
    const Level level = makeLevel({
        { "..........", ".........." },
        { "CI  P   # ", "          " },
    });

    const auto makeSession = [&] {
        GameplaySession session;
        session.reset(level);
        session.setStepDurationSeconds(0.125f);
        return session;
    };
    const GameplayLoop::InputFrame push {
        .right = { .pressed = true, .down = true },
    };
    const GameplayLoop::InputFrame moveDown {
        .down = { .pressed = true, .down = true },
    };

    GameplaySession session = makeSession();
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt) {
        static_cast<void>(GameplayLoop::update(
            level, session, presentation, input, dt, false));
    };

    update(push, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.0625f);
    update(moveDown, 0.0625f);
    CHECK(session.inFlight().size() == 2);
    CHECK(std::abs(
        presentation.movables().front().renderPosition.x - 3.5f) < 0.0001f);
    CHECK(presentation.controlActivation({ 4, 0, 1 }).value_or(0.0f) > 0.0f);
    CHECK(presentation.controlActivation({ 4, 0, 1 }).value_or(1.0f) < 1.0f);

    // The player completes exactly at the frame boundary while the block's
    // longer slide survives. Its visual must remain sampled at that boundary.
    update({}, 0.0625f);
    CHECK(session.state().players.front().cell == (GridPosition3 { 1, 1, 1 }));
    CHECK(session.state().movables.front().cell == (GridPosition3 { 2, 0, 1 }));
    CHECK(session.inFlight().size() == 1);
    CHECK(std::abs(
        presentation.movables().front().renderPosition.x - 4.0f) < 0.0001f);
    CHECK(presentation.movables().front().moving);
    CHECK(std::abs(presentation.controlActivation({ 4, 0, 1 }).value_or(-1.0f) - 1.0f) < 0.0001f);

    update({}, 0.015625f);
    CHECK(std::abs(
        presentation.movables().front().renderPosition.x - 4.125f) < 0.0001f);
    CHECK(presentation.movables().front().moving);

    for (int frame = 0; frame < 100 && session.moving(); ++frame) {
        update({}, 0.0625f);
    }
    CHECK(!session.moving());
    CHECK(presentation.movables().front().renderPosition.x ==
        static_cast<float>(session.state().movables.front().cell.x));
    CHECK(!presentation.movables().front().moving);

    // With time left in the frame, the loop must produce the same survivor
    // sample after crossing the player's completion boundary.
    session = makeSession();
    presentation.resetEntities(session.state());
    update(push, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.0625f);
    update(moveDown, 0.0625f);
    update({}, 0.078125f);
    CHECK(session.inFlight().size() == 1);
    CHECK(std::abs(
        presentation.movables().front().renderPosition.x - 4.125f) < 0.0001f);
    CHECK(presentation.movables().front().moving);
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

void testInstantMirrorActivationAnimatesPressureEdges()
{
    TEST("instantMirrorActivationAnimatesPressureEdges");
    const GridPosition3 source { 2, 4, 1 };
    const GridPosition3 destination { 0, 2, 1 };
    const Level level = Level::loadFromDefinition({
        .layers = {
            { ".....", ".....", ".....", ".....", "....." },
            { "     ", "     ", "P 3  ", "     ", "  C  " },
        },
        .plates = { { source, TileType::PressurePlate } },
    }, "instant pressure reflection");
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt) {
        return GameplayLoop::update(level, session, presentation, input, dt, false);
    };
    const auto activation = update({ .interactPressed = true }, 0.06f);
    CHECK(activation.mirrorActivated);
    CHECK(session.moving());
    CHECK(session.state().players[0].cell == source);
    CHECK(std::abs(presentation.controlActivation(source).value_or(-1.0f) - 0.5f) < 0.0001f);
    CHECK(std::abs(presentation.controlActivation(destination).value_or(-1.0f) - 0.5f) < 0.0001f);
    update({}, 0.07f);
    CHECK(!session.moving());
    CHECK(session.state().players[0].cell == destination);
    CHECK(!presentation.controlActivation(source));
    CHECK(!presentation.controlActivation(destination));
    update({ .undoPressed = true }, 0.06f);
    CHECK(std::abs(presentation.controlActivation(source).value_or(-1.0f) - 0.5f) < 0.0001f);
    CHECK(std::abs(presentation.controlActivation(destination).value_or(-1.0f) - 0.5f) < 0.0001f);
    update({}, 0.07f);
    CHECK(session.state().players[0].cell == source);
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

void testWitchSwapEmitsBothParticleEndpointsOnce()
{
    TEST("witchSwapEmitsBothParticleEndpointsOnce");
    const Level level = makeLevel({
        { "....." },
        { "H  N " },
    });
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult swapped = GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true, .down = true } },
        0.01f,
        false);
    CHECK(swapped.witchSwapped);
    const std::vector<GridPosition3> expected {
        GridPosition3 { 0, 0, 1 },
        GridPosition3 { 3, 0, 1 },
    };
    CHECK(swapped.witchSwapDestinations == expected);

    const GameplayLoop::UpdateResult continued = GameplayLoop::update(
        level, session, presentation, {}, 0.01f, false);
    CHECK(!continued.witchSwapped);
    CHECK(continued.witchSwapDestinations.empty());
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

void testTurretShotCueWaitsForMovementToFinish()
{
    TEST("turretShotCueWaitsForMovementToFinish");
    const Level level = makeLevel({
        { "...." },
        { "e  C" },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.25f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult moving = GameplayLoop::update(
        level,
        session,
        presentation,
        { .left = { .pressed = true, .down = true } },
        0.01f,
        false);
    CHECK(moving.turretShots.empty());
    CHECK(session.moving());

    const GameplayLoop::UpdateResult landed = GameplayLoop::update(
        level, session, presentation, {}, 0.24f, false);

    CHECK(landed.turretShots.size() == 1);
    if (!landed.turretShots.empty()) {
        const GridPosition3 expectedTarget { 2, 0, 1 };
        CHECK(landed.turretShots[0].shot.target.kind == EntityKind::Player);
        CHECK(landed.turretShots[0].shot.targetCell == expectedTarget);
        CHECK(landed.turretShots[0].impactDelaySeconds == 0.0f);
    }
}

void testPushedTurretWaitsUntilItLandsBeforeVolleyStarts()
{
    TEST("pushedTurretWaitsUntilItLandsBeforeVolleyStarts");
    const Level level = makeLevel({
        { ".....", ".....", "....." },
        { "e    ", "  n  ", "  C  " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.25f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult moving = GameplayLoop::update(
        level,
        session,
        presentation,
        { .up = { .pressed = true, .down = true } },
        0.01f,
        false);
    const GridPosition3 start { 2, 1, 1 };
    CHECK(moving.turretShots.empty());
    CHECK(session.state().movables[1].cell == start);
    CHECK(!session.state().movables[1].dead);

    const GameplayLoop::UpdateResult landed = GameplayLoop::update(
        level, session, presentation, {}, 0.24f, false);
    const GridPosition3 destination { 2, 0, 1 };
    CHECK(landed.turretShots.size() == 1);
    CHECK(session.state().movables[1].cell == destination);
    CHECK(session.state().movables[1].dead);
}

void testFacingTurretsStartAnAmbientMutualVolley()
{
    TEST("facingTurretsStartAnAmbientMutualVolley");
    const Level level = makeLevel({
        { ".....", "....." },
        { "e   w", "  C  " },
    });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.25f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());

    const GameplayLoop::UpdateResult fired = GameplayLoop::update(
        level, session, presentation, {}, 0.01f, false);
    CHECK(session.moving());
    CHECK(!session.state().movables[0].dead);
    CHECK(!session.state().movables[1].dead);
    CHECK(fired.turretShots.size() == 2);
    for (const auto& shot : fired.turretShots) {
        CHECK(shot.shot.target.kind == EntityKind::Movable);
        CHECK(shot.impactDelaySeconds > 0.0f);
    }

    static_cast<void>(GameplayLoop::update(
        level, session, presentation, {}, 0.24f, false));
    CHECK(!session.moving());
    CHECK(session.state().movables[0].dead);
    CHECK(session.state().movables[1].dead);
}

void testPortalIceFallFiresAtStationaryHeroAndUndoesTogether()
{
    TEST("portalIceFallFiresAtStationaryHeroAndUndoesTogether");
    const Level level = Level::loadFromDefinition({
        .layers = { { ".......", ".......", ".......", ".......", "......." },
                    { ".......", ".......", ". .....", ".......", "......." },
                    { " O     ", "       ", "   CI o", "       ", " n     " } },
        .portals = { { .cell = { 1, 0, 2 }, .color = { 1, 1, 0 } },
                     { .cell = { 6, 2, 2 }, .color = { 1, 1, 0 } } },
    }, "live portal ice fall");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.25f);
    const GameState initial = session.state();
    GameplayPresentation presentation;
    presentation.resetEntities(initial);
    const auto started = GameplayLoop::update(
        level, session, presentation,
        { .right = { .pressed = true } }, 0.01f, false);
    CHECK(started.turretShots.empty());
    CHECK(session.inFlight().size() == 2);
    if (session.inFlight().size() == 2) {
        const auto& slide = session.inFlight()[1];
        CHECK(slide.plan.after.players[0].dead);
        CHECK(slide.turretShots.size() == 1);
        CHECK(slide.portalTransits.size() == 1);
        CHECK(slide.causalGroup == session.inFlight()[0].causalGroup);
    }

    std::size_t shots = 0;
    std::size_t portalSounds = 0;
    bool fired = false;
    for (int frame = 0; frame < 150; ++frame) {
        const auto result = GameplayLoop::update(
            level, session, presentation, {}, 0.01f, false);
        shots += result.turretShots.size();
        portalSounds += static_cast<std::size_t>(std::ranges::count(
            result.sounds, GameplaySound::PortalTravel));
        if (!result.turretShots.empty()) {
            fired = true;
            CHECK(session.state().movables[0].cell == GridPosition3({ 1, 2, 1 }));
            CHECK(result.turretShots.front().shot.beamSegments.size() == 2);
            CHECK(result.turretShots.front().impactDelaySeconds == 0.0f);
        }
        if (!fired) {
            CHECK(!session.state().players[0].dead);
        }
    }
    CHECK(shots == 1);
    CHECK(portalSounds == 1);
    CHECK(session.state().players[0].cell == GridPosition3({ 4, 2, 2 }));
    CHECK(session.state().players[0].dead);
    CHECK(!session.moving());
    CHECK(session.undoCount() == 1);
    static_cast<void>(GameplayLoop::update(
        level, session, presentation, { .undoPressed = true }, 0.01f, false));
    for (int frame = 0; frame < 150; ++frame) {
        const auto result = GameplayLoop::update(
            level, session, presentation, {}, 0.01f, false);
        CHECK(result.turretShots.empty());
        CHECK(result.sounds.empty());
    }
    CHECK(session.state() == initial);
    CHECK(session.undoCount() == 0);
}

void testPortalCrossingsReachTheLivePresentation()
{
    TEST("portalCrossingsReachTheLivePresentation");
    const Level level = Level::loadFromDefinition(
        {
            .layers = { { "......", "......", "......" },
                        { "  C   ", "      ", "   p  " } },
            .plates = { { .cell = { 2, 0, 1 }, .tile = TileType::PortalEast } },
            .portals = { { .cell = { 2, 0, 1 }, .color = { 1, 0, 1 } },
                         { .cell = { 3, 2, 1 }, .color = { 1, 0, 1 } } },
        },
        "live edge portals");
    GameplaySession session;
    session.reset(level);
    session.setStepRates({ .playerMove = 3 });
    session.setStepDurationSeconds(1.0f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    static_cast<void>(GameplayLoop::update(
        level,
        session,
        presentation,
        { .right = { .pressed = true } },
        0.016f,
        false));
    const auto* action =
        session.inFlight().empty() ? nullptr : &session.inFlight().front();
    CHECK(action != nullptr);
    if (action) {
        CHECK(action->portalTransits.size() == 1);
        CHECK(action->plan.after.players[0].cell == GridPosition3({ 3, 0, 1 }));
        CHECK(action->plan.presentation.motions.size() == 2);
        if (action->plan.presentation.motions.size() == 2) {
            CHECK(
                action->plan.presentation.motions[0].to ==
                Vec3({ 2.5f, 0, 1 }));
            CHECK(
                action->plan.presentation.motions[1].from ==
                Vec3({ 3, 2.5f, 1 }));
            CHECK(action->plan.presentation.motions[1].to == Vec3({ 3, 0, 1 }));
        }
    }
}

void testButtonCyclesRepeatWithoutHoldingTheCapDown()
{
    TEST("buttonCyclesRepeatWithoutHoldingTheCapDown");
    const Level level = Level::loadFromDefinition({
        .layers = { { "...." }, { "C   " } },
        .plates = { { { 0, 0, 1 }, TileType::ButtonEast } },
    }, "button motion");
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt) {
        (void)GameplayLoop::update(level, session, presentation, input, dt, false);
    };
    for (int pulse = 0; pulse < 2; ++pulse) {
        update({ .interactPressed = true }, 0.035f);
        CHECK(session.moving());
        CHECK(std::abs(presentation.controlActivation({ 0, 0, 1 }).value_or(-1.0f) - 0.5f) < 0.0001f);
        if (pulse == 1) {
            CHECK(session.activeAction().before.activeButtons == session.activeAction().after.activeButtons);
        }
        update({}, 0.05f);
        CHECK(std::abs(presentation.controlActivation({ 0, 0, 1 }).value_or(-1.0f) - 1.0f) < 0.0001f);
        update({}, 0.075f);
        CHECK(std::abs(presentation.controlActivation({ 0, 0, 1 }).value_or(-1.0f) - 0.5f) < 0.0001f);
        update({}, 0.061f);
        CHECK(!session.moving());
        CHECK(session.state().activeButtons == std::vector<GridPosition3>({ { 0, 0, 1 } }));
        CHECK(!presentation.controlActivation({ 0, 0, 1 }));
    }
    update({ .right = { .pressed = true } }, 0.03f);
    CHECK(!presentation.controlActivation({ 0, 0, 1 }));
    update({}, 0.5f);
    CHECK(session.state().activeButtons.empty());
    CHECK(!presentation.controlActivation({ 0, 0, 1 }));
}

void testLeverMotionPersistsAndReversesThroughUndoAndRestart()
{
    TEST("leverMotionPersistsAndReversesThroughUndoAndRestart");
    const Level level = Level::loadFromDefinition({
        .layers = { { "...." }, { "C   " } },
        .plates = { { { 0, 0, 1 }, TileType::LeverWest } },
    }, "lever motion");
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt) {
        (void)GameplayLoop::update(level, session, presentation, input, dt, false);
    };
    update({ .interactPressed = true }, 0.11f);
    CHECK(session.moving());
    CHECK(session.state().activeLevers.empty());
    CHECK(std::abs(presentation.controlActivation({ 0, 0, 1 }).value_or(-1.0f) - 0.5f) < 0.0001f);
    update({}, 0.12f);
    CHECK(!session.state().activeLevers.empty());
    CHECK(!presentation.controlActivation({ 0, 0, 1 }));

    // Restored history retains the immutable surface timeline used by undo.
    const auto saved = session.snapshot();
    CHECK(session.restore(level, saved));
    presentation.resetEntities(session.state());
    update({ .undoPressed = true }, 0.055f);
    CHECK(std::abs(presentation.controlActivation({ 0, 0, 1 }).value_or(-1.0f) - 0.84375f) < 0.0001f);
    update({}, 0.2f);
    CHECK(session.state().activeLevers.empty());
    CHECK(!presentation.controlActivation({ 0, 0, 1 }));

    update({ .interactPressed = true }, 0.3f);
    CHECK(!session.state().activeLevers.empty());
    update({ .restartPressed = true }, 0.11f);
    CHECK(std::abs(presentation.controlActivation({ 0, 0, 1 }).value_or(-1.0f) - 0.5f) < 0.0001f);
    update({}, 0.2f);
    CHECK(session.state().activeLevers.empty());
    CHECK(!presentation.controlActivation({ 0, 0, 1 }));
}

void testPressurePlateSoundsAreEdgesAndUndoIsSilent()
{
    TEST("pressurePlateSoundsAreEdgesAndUndoIsSilent");
    const auto level = makeLevel({ { "....." }, { "CP  E" } });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto pressed = GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.15f, false);
    CHECK(std::ranges::count(pressed.sounds, GameplaySound::PressurePlatePress) == 1);
    CHECK(GameplayLoop::update(level, session, presentation, {}, 0.2f, false).sounds.empty());
    const auto released = GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.15f, false);
    CHECK(std::ranges::count(released.sounds, GameplaySound::PressurePlateRelease) == 1);
    const auto undone = GameplayLoop::update(level, session, presentation,
        { .undoPressed = true }, 0.4f, false);
    CHECK(undone.sounds.empty());
    CHECK(session.state().players[0].cell == GridPosition3({ 1, 0, 1 }));
}

void testEveryButtonPulseHasOneSound()
{
    TEST("everyButtonPulseHasOneSound");
    const auto level = Level::loadFromDefinition({
        .layers = { { "...." }, { "C  G" } },
        .gates = { { .cell = { 3, 0, 1 }, .pressurePlates = { { 0, 0, 1 } } } },
        .plates = { { { 0, 0, 1 }, TileType::Button } },
    }, "button audio");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    for (int pulse = 0; pulse < 2; ++pulse) {
        const auto result = GameplayLoop::update(level, session, presentation,
            { .interactPressed = true }, 0.15f, false);
        CHECK(std::ranges::count(result.sounds, GameplaySound::ButtonPress) == 1);
        CHECK(std::ranges::count(result.sounds, GameplaySound::GateOpen) == (pulse == 0 ? 1 : 0));
        CHECK(std::ranges::count(result.sounds, GameplaySound::GateClose) == 0);
        CHECK(!result.mirrorActivated);
        CHECK(GameplayLoop::update(level, session, presentation, {}, 0.1f, false).sounds.empty());
    }
    const auto expired = GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.15f, false);
    CHECK(std::ranges::count(expired.sounds, GameplaySound::GateClose) == 1);
    CHECK(std::ranges::count(expired.sounds, GameplaySound::GateOpen) == 0);
}

void testGateSoundsFollowOpenState()
{
    TEST("gateSoundsFollowOpenState");
    for (const bool startOpen : { false, true }) {
        const auto level = Level::loadFromDefinition({
            .layers = { { "....." }, { "CP G " } },
            .gates = { { .cell = { 3, 0, 1 },
                .pressurePlates = { { 1, 0, 1 } }, .startOpen = startOpen } },
        }, "gate audio");
        GameplaySession session;
        session.reset(level);
        session.setStepDurationSeconds(0.1f);
        GameplayPresentation presentation;
        presentation.resetEntities(session.state());
        const auto pressed = GameplayLoop::update(level, session, presentation,
            { .right = { .pressed = true } }, 0.01f, false);
        const auto onPress = startOpen ? GameplaySound::GateClose : GameplaySound::GateOpen;
        const auto onRelease = startOpen ? GameplaySound::GateOpen : GameplaySound::GateClose;
        CHECK(std::ranges::count(pressed.sounds, onPress) == 1);
        CHECK(std::ranges::count(pressed.sounds, onRelease) == 0);
        const auto landed = GameplayLoop::update(level, session, presentation, {}, 0.2f, false);
        CHECK(std::ranges::count(landed.sounds, onPress) == 0);
        CHECK(rules::isGateOpen(level, session.state(), level.gates()[0]) != startOpen);
        const auto released = GameplayLoop::update(level, session, presentation,
            { .left = { .pressed = true } }, 0.15f, false);
        CHECK(std::ranges::count(released.sounds, onRelease) == 1);
        CHECK(std::ranges::count(released.sounds, onPress) == 0);
        CHECK(rules::isGateOpen(level, session.state(), level.gates()[0]) == startOpen);
        CHECK(GameplayLoop::update(level, session, presentation, {}, 0.2f, false).sounds.empty());
        const auto undone = GameplayLoop::update(level, session, presentation,
            { .undoPressed = true }, 0.4f, false);
        CHECK(undone.sounds.empty());
        CHECK(rules::isGateOpen(level, session.state(), level.gates()[0]) != startOpen);
    }
}

void testBlockedGateDoesNotPlayClosingSound()
{
    TEST("blockedGateDoesNotPlayClosingSound");
    const auto level = Level::loadFromDefinition({
        .layers = { { "....." }, { "PC G " }, { "   R " } },
        .gates = { { .cell = { 3, 0, 1 }, .pressurePlates = { { 0, 0, 1 } } } },
    }, "blocked gate audio");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto opened = GameplayLoop::update(level, session, presentation,
        { .left = { .pressed = true } }, 0.15f, false);
    CHECK(std::ranges::count(opened.sounds, GameplaySound::GateOpen) == 1);
    CHECK(session.state().movables[0].cell == GridPosition3({ 3, 0, 1 }));
    const auto released = GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.15f, false);
    CHECK(std::ranges::count(released.sounds, GameplaySound::PressurePlateRelease) == 1);
    CHECK(std::ranges::count(released.sounds, GameplaySound::GateClose) == 0);
    CHECK(std::ranges::count(released.sounds, GameplaySound::GateOpen) == 0);
    CHECK(rules::isGateOpen(level, session.state(), level.gates()[0]));
}

void testRotatorSoundRequiresAnActualTurn()
{
    TEST("rotatorSoundRequiresAnActualTurn");
    const auto level = Level::loadFromDefinition({
        .layers = { { "....." }, { "PC 1R" } },
        .rotators = { { .cell = { 3, 0, 1 }, .pressurePlates = { { 0, 0, 1 } } } },
        .plates = { { { 3, 0, 1 }, TileType::RotatorClockwise } },
    }, "rotator audio");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto result = GameplayLoop::update(level, session, presentation,
        { .left = { .pressed = true } }, 0.15f, false);
    CHECK(std::ranges::count(result.sounds, GameplaySound::RotatorTurn) == 1);
    CHECK(GameplayLoop::update(level, session, presentation, {}, 0.2f, false).sounds.empty());
}

void testMinecartGateSoundFollowsTheMovingCart()
{
    TEST("minecartGateSoundFollowsTheMovingCart");
    const auto level = Level::loadFromDefinition({
        .layers = { { "....." }, { "CPMg_" } },
        .plates = { { { 2, 0, 1 }, TileType::RailStopEastWest },
                    { { 3, 0, 1 }, TileType::RailStraightEastWest } },
        .minecarts = { { .cell = { 2, 0, 1 },
            .pressurePlates = { { 1, 0, 1 } }, .initialDirection = 1 } },
    }, "cart audio");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto started = GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.01f, false);
    CHECK(std::ranges::count(started.sounds, GameplaySound::MinecartGateOpen) == 0);
    CHECK(!presentation.minecarts()[0].moving);
    std::size_t gateSounds = 0;
    bool observedTravel = false;
    for (int frame = 0; frame < 100; ++frame) {
        const auto result = GameplayLoop::update(level, session, presentation, {}, 0.01f, false);
        gateSounds += static_cast<std::size_t>(std::ranges::count(
            result.sounds, GameplaySound::MinecartGateOpen));
        observedTravel |= presentation.minecarts()[0].moving;
    }
    CHECK(gateSounds == 1);
    CHECK(observedTravel);
    CHECK(!presentation.minecarts()[0].moving);
    CHECK(session.state().minecarts[0].cell == GridPosition3({ 4, 0, 1 }));
}

void testElevatorLoopUsesPlatformMotionOnly()
{
    TEST("elevatorLoopUsesPlatformMotionOnly");
    const auto level = Level::loadFromDefinition({
        .layers = { { "....=." }, { "  C P." }, { "     ." }, { "      " } },
        .elevators = { { .cell = { 4, 0, 0 },
            .pressurePlates = { { 4, 0, 1 } }, .levels = { 0, 2 } } },
    }, "elevator audio");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.1f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    static_cast<void>(GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.15f, false));
    CHECK(!presentation.elevators()[0].moving);
    static_cast<void>(GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.01f, false));
    CHECK(!presentation.elevators()[0].moving);
    bool observedTravel = false;
    std::size_t platePresses = 0;
    std::size_t plateReleases = 0;
    for (int frame = 0; frame < 100; ++frame) {
        const auto result = GameplayLoop::update(level, session, presentation, {}, 0.01f, false);
        platePresses += static_cast<std::size_t>(std::ranges::count(
            result.sounds, GameplaySound::PressurePlatePress));
        plateReleases += static_cast<std::size_t>(std::ranges::count(
            result.sounds, GameplaySound::PressurePlateRelease));
        observedTravel |= presentation.elevators()[0].moving;
    }
    CHECK(platePresses == 1);
    CHECK(plateReleases == 1);
    CHECK(observedTravel);
    CHECK(!presentation.elevators()[0].moving);
    CHECK(session.state().elevators[0].cell == GridPosition3({ 4, 0, 2 }));
}

void testLecternClosesWhenReaderWalksAway()
{
    TEST("lecternClosesWhenReaderWalksAway");
    const Level level = makeLevel({
        { ".......", ".......", "......." },
        { "   #   ", "   CT  ", "       " },
    });
    GameplaySession session;
    session.setStepDurationSeconds(0.2f);
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt = 0.01f) {
        return GameplayLoop::update(level, session, presentation, input, dt, false);
    };

    (void)update({ .right = { true, true } });
    CHECK(session.readingLectern() == GridPosition3({ 4, 1, 1 }));
    CHECK(session.playerMoveCount() == 0);
    const auto before = session.snapshot();
    // Walking into the stand or a wall keeps the reading box open.
    (void)update({ .right = { false, true } }, 0.3f);
    (void)update({ .up = { true, true } }, 0.3f);
    (void)update({}, 0.3f);
    CHECK(session.readingLectern().has_value());
    CHECK(session.snapshot() == before);

    (void)update({ .left = { true, true } });
    CHECK(!session.readingLectern());
    CHECK(session.moving());
    (void)update({}, 0.3f);
    CHECK(session.state().players[0].cell == GridPosition3({ 2, 1, 1 }));
    CHECK(session.playerMoveCount() == 1);
    CHECK(session.undoCount() == 1);
    CHECK(session.inputLog() == std::vector<PlayerInput> { PlayerInput::Left });
    (void)update({ .undoPressed = true }, 0.3f);
    CHECK(session.state().players[0].cell == GridPosition3({ 3, 1, 1 }));
    CHECK(!session.readingLectern());
    CHECK(session.playerMoveCount() == 0);

    (void)update({ .right = { true, true } });
    CHECK(session.readingLectern().has_value());
    (void)update({ .left = { true, true } });
    (void)update({ .left = { false, true } }, 0.21f);
    (void)update({}, 0.3f);
    CHECK(!session.readingLectern());
    CHECK(session.state().players[0].cell == GridPosition3({ 1, 1, 1 }));
    CHECK(session.playerMoveCount() == 2);
}

void testMovingMirrorCopyDoesNotCloseReadersLectern()
{
    TEST("movingMirrorCopyDoesNotCloseReadersLectern");
    const Level level = makeLevel({
        { ".......", ".......", "......." },
        { "   #   ", "   CT  ", "       " },
    });
    auto state = rules::initialState(level);
    auto copy = state.players[0];
    copy.id += 1000;
    copy.cell = { 1, 1, 1 };
    state.players.push_back(copy);
    GameplaySession session;
    session.resetToState(state, state.players[0].controller);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    (void)GameplayLoop::update(level, session, presentation,
        { .right = { true, true } }, 0.01f, false);
    (void)GameplayLoop::update(level, session, presentation,
        { .up = { true, true } }, 0.01f, false);
    CHECK(session.readingLectern().has_value());
    CHECK(session.moving());
    (void)GameplayLoop::update(level, session, presentation, {}, 0.3f, false);
    CHECK(!session.moving());
    CHECK(session.readingLectern().has_value());
    CHECK(session.state().players[0].cell == GridPosition3({ 3, 1, 1 }));
    CHECK(session.state().players[1].cell == GridPosition3({ 1, 0, 1 }));
    (void)GameplayLoop::update(level, session, presentation,
        { .left = { true, true } }, 0.01f, false);
    CHECK(!session.readingLectern());
}

} // namespace

void testWaterRippleStartsAtContactAndPersistsAfterCommit()
{
    TEST("waterRippleStartsAtContactAndPersistsAfterCommit");
    const Level level = makeLevel({ { "..W." }, { "CR  " } });
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.25f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt) {
        (void)GameplayLoop::update(level, session, presentation, input, dt, false);
    };
    const auto ripples = [&] {
        RenderFrameData frame;
        presentation.appendWaterRippleRenderData(frame);
        return frame;
    };
    const float entrySeconds = 0.25f * 0.5f / 1.18f;
    update({ .right = { .pressed = true } }, 0.1f);
    CHECK(ripples().waterRippleCount == 0);
    update({}, 0.05f);
    CHECK(ripples().waterRippleCount == 1);
    CHECK(!session.state().movables[0].fallen);
    CHECK(rules::isUnfilledWater(level, session.state(), { 2, 0, 1 }));
    update({}, 0.11f);
    CHECK(session.state().movables[0].fallen);
    const auto contact = ripples();
    CHECK(contact.waterRippleCount == 1);
    if (contact.waterRippleCount == 1) {
        CHECK(std::abs(contact.waterRipples[0].position.x - 2.5f) < 0.0001f);
        CHECK(std::abs(contact.waterRipples[0].position.y - 0.5f) < 0.0001f);
        CHECK(std::abs(contact.waterRipples[0].position.z - 0.82f) < 0.0001f);
        CHECK(std::abs(contact.waterRipples[0].ageSeconds -
            (0.26f - entrySeconds)) < 0.0001f);
    }
    update({}, 0.2f);
    CHECK(ripples().waterRippleCount == 1);
    CHECK(std::abs(ripples().waterRipples[0].ageSeconds -
        (0.46f - entrySeconds)) < 0.0001f);
    // Undo removes the entry effect and reversing the recorded motion cannot
    // emit another one. A later forward push still produces a fresh ripple.
    update({ .undoPressed = true }, 0.01f);
    CHECK(ripples().waterRippleCount == 0);
    update({}, 0.4f);
    CHECK(!session.state().movables[0].fallen);
    CHECK(ripples().waterRippleCount == 0);
    update({ .right = { .pressed = true } }, 0.3f);
    CHECK(ripples().waterRippleCount == 1);
    update({ .restartPressed = true }, 0.01f);
    CHECK(ripples().waterRippleCount == 0);
}

void testDeferredWaterEntryMatchesLongFrameCatchUp()
{
    TEST("deferredWaterEntryMatchesLongFrameCatchUp");
    const Level level = makeLevel({ { "....W." }, { "CI    " } });
    const auto run = [&](bool split) {
        GameplaySession session;
        session.reset(level);
        session.setStepDurationSeconds(0.25f);
        GameplayPresentation presentation;
        presentation.resetEntities(session.state());
        (void)GameplayLoop::update(level, session, presentation,
            { .right = { .pressed = true } }, split ? 0.6f : 0.9f, false);
        if (split) {
            RenderFrameData pending;
            presentation.appendWaterRippleRenderData(pending);
            CHECK(pending.waterRippleCount == 0);
            (void)GameplayLoop::update(level, session, presentation, {}, 0.3f, false);
        }
        CHECK(session.state().movables[0].fallen);
        RenderFrameData frame;
        presentation.appendWaterRippleRenderData(frame);
        return frame;
    };
    const auto split = run(true);
    const auto catchUp = run(false);
    CHECK(split.waterRippleCount == 1);
    CHECK(catchUp.waterRippleCount == 1);
    const float entrySeconds = 0.5f + 0.25f * 0.5f / 1.18f;
    CHECK(std::abs(split.waterRipples[0].ageSeconds -
        (0.9f - entrySeconds)) < 0.0001f);
    CHECK(std::abs(split.waterRipples[0].ageSeconds -
        catchUp.waterRipples[0].ageSeconds) < 0.0001f);
}

int main()
{
    TEST("lecternReadingPausesWithoutSpendingAMove");
    const Level bookLevel = Level::loadFromDefinition({
        .layers = { { "...." }, { "CT R" } },
        .lecterns = { { .cell = { 1, 0, 1 }, .text = "Welcome!" } },
    }, "book");
    GameplaySession reader;
    reader.reset(bookLevel);
    GameplayPresentation readerPresentation;
    readerPresentation.resetEntities(reader.state());
    const GameState beforeReading = reader.state();
    (void)GameplayLoop::update(bookLevel, reader, readerPresentation,
        { .right = { true, true } }, .01f, false);
    CHECK(reader.readingLectern() == GridPosition3({ 1, 0, 1 }));
    CHECK(reader.state() == beforeReading);
    CHECK(reader.playerMoveCount() == 0);
    CHECK(reader.undoCount() == 0);
    CHECK(reader.inputLog().empty());
    (void)GameplayLoop::update(bookLevel, reader, readerPresentation,
        { .right = { true, true }, .restartPressed = true }, 1.0f, false);
    CHECK(reader.readingLectern().has_value());
    CHECK(reader.state() == beforeReading);
    (void)GameplayLoop::update(bookLevel, reader, readerPresentation,
        { .right = { false, true }, .dismissPressed = true }, .01f, false);
    CHECK(!reader.readingLectern());
    (void)GameplayLoop::update(bookLevel, reader, readerPresentation,
        { .right = { false, true } }, .01f, false);
    CHECK(!reader.readingLectern());
    (void)GameplayLoop::update(bookLevel, reader, readerPresentation, {}, .01f, false);
    (void)GameplayLoop::update(bookLevel, reader, readerPresentation,
        { .right = { true, true } }, .01f, true);
    CHECK(reader.readingLectern().has_value());
    reader.reset(bookLevel);
    CHECK(!reader.readingLectern());
    TEST("bufferedLecternBumpWaitsForMovement");
    const Level bufferedBook = Level::loadFromLayers({ { "...." }, { "C T " } }, "buffered book");
    reader.reset(bufferedBook);
    readerPresentation.resetEntities(reader.state());
    (void)GameplayLoop::update(bufferedBook, reader, readerPresentation,
        { .right = { true, true } }, .01f, false);
    CHECK(reader.moving());
    (void)GameplayLoop::update(bufferedBook, reader, readerPresentation,
        { .right = { true, false } }, .01f, false);
    CHECK(!reader.readingLectern());
    for (int i = 0; i < 50 && !reader.readingLectern(); ++i) {
        (void)GameplayLoop::update(bufferedBook, reader, readerPresentation, {}, .01f, false);
    }
    CHECK(reader.readingLectern() == GridPosition3({ 2, 0, 1 }));
    CHECK(reader.state().players[0].cell == GridPosition3({ 1, 0, 1 }));
    CHECK(reader.playerMoveCount() == 1);
    testLecternClosesWhenReaderWalksAway();
    testMovingMirrorCopyDoesNotCloseReadersLectern();
    testPressurePlateSoundsAreEdgesAndUndoIsSilent();
    testEveryButtonPulseHasOneSound();
    testButtonCyclesRepeatWithoutHoldingTheCapDown();
    testLeverMotionPersistsAndReversesThroughUndoAndRestart();
    testGateSoundsFollowOpenState();
    testBlockedGateDoesNotPlayClosingSound();
    testRotatorSoundRequiresAnActualTurn();
    testMinecartGateSoundFollowsTheMovingCart();
    testElevatorLoopUsesPlatformMotionOnly();
    testPortalIceFallFiresAtStationaryHeroAndUndoesTogether();
    testPortalCrossingsReachTheLivePresentation();
    testOpposingDirectionsAreNeutral();
    testSimulationTimingClampsLongFrames();
    testSimulationTimingScalesOneSharedDelta();
    testSimulationTimingResetsAcrossMinimize();
    testSimulationTimingTracksOverlappingSuspensions();
    testSimulationTimingObservesTransientSuspendCycle();
    testRenderedPlayerNeverGoesBackwards();
    testChainedSlideIsDrawnTileByTile();
    testHeldMoveFollowsDirectlyBehindSlide();
    testCompletingActionPreservesConcurrentPresentation();
    testMoveAdvancesSessionAndPresentation();
    testMirrorInputCommitsAnInstantAction();
    testInstantMirrorActivationAnimatesPressureEdges();
    testRejectedMirrorInputDoesNotEmitActivation();
    testWitchSwapEmitsBothParticleEndpointsOnce();
    testSolvedScreenAndDraftOutcomesDiffer();
    testMirrorDuplicationRequiresEveryPlayerOnAnEnd();
    testTurretShotCueWaitsForMovementToFinish();
    testPushedTurretWaitsUntilItLandsBeforeVolleyStarts();
    testFacingTurretsStartAnAmbientMutualVolley();
    testWaterRippleStartsAtContactAndPersistsAfterCommit();
    testDeferredWaterEntryMatchesLongFrameCatchUp();

    if (failures == 0) {
        std::cout << "GameplayLoopTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GameplayLoopTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
