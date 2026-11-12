// Recorded solutions: file format, recording, readable replay failures, and a
// replay of every recorded solution against the current rules and levels.

#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/Solution.hpp"
#include "engine/SolutionCoverage.hpp"
#include "engine/SolutionStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef SOKOBAN_TEST_SOURCE_DIR
#error "SOKOBAN_TEST_SOURCE_DIR must name the repository root"
#endif

namespace {

using namespace sokoban;
using solution::Input;

const Level::Definition walkAndPushDefinition {
    .layers = {
        { "......", "......", "......" },
        { "C    E", "  R   ", "      " },
    },
};

const Level::Definition coverageWalkDefinition {
    .layers = { { "..." }, { "C E" } },
};

const Level::Definition circlingPadDefinition {
    .layers = {
        { "......", "\",....", "'`...." },
        { "Q E   ", "$     ", "      " },
    },
};

void testDruidTransformationAndUndoPreserveHeroOrder()
{
    TEST("druidTransformationAndUndoPreserveHeroOrder");
    const auto definition = Level::parseDefinition({
        "@ranger {\"cell\":[0,0,1]}",
        "@druid {\"cell\":[1,0,1]}",
        "@layer 0", ".....",
        "@layer 1", "CC  Q",
    }, "druid undo");
    const auto level = Level::loadFromDefinition(definition, "druid undo");
    solution::Driver driver(level);
    const auto before = driver.state();
    CHECK(driver.apply(Input::CycleHero));
    CHECK(driver.apply(Input::Left));
    CHECK(driver.state().players.size() == 2);
    CHECK(driver.state().movables.size() == 1);
    CHECK(driver.state().movables[0].type == TileType::SkippingCrystal);
    const auto petrified = driver.state();
    CHECK(driver.apply(Input::Left));
    CHECK(driver.state().movables.empty());
    CHECK(driver.state().players.back() == before.players[0]);
    CHECK(driver.apply(Input::Undo));
    CHECK(driver.state() == petrified);
    CHECK(driver.apply(Input::Undo));
    CHECK(driver.state() == before);

    auto puzzle = definition;
    puzzle.layers[1][0] = "CC E ";
    puzzle.layers[1][0][0] = tileTypeToChar(TileType::Ranger);
    puzzle.layers[1][0][1] = tileTypeToChar(TileType::CrystalDruid);
    const auto puzzleLevel = Level::loadFromDefinition(puzzle, "druid solution");
    const std::vector<Input> inputs {
        Input::CycleHero, Input::Left, Input::Right, Input::Right,
    };
    const auto recording = solution::record(puzzleLevel, puzzle, inputs, "druid solution");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    if (recording.solved) {
        const auto parsed = solution::parse(solution::serialize(recording.solution));
        CHECK(solution::replay(puzzleLevel, parsed).passed);
    }
}

void testRobotPatrolRecordsFiniteBeatsAndReplaysReversals()
{
    TEST("robotPatrolRecordsFiniteBeatsAndReplaysReversals");
    const Level::Definition definition {
        .layers = { { "......", "......", "......" },
            { "C E   ", "      ", " } #  " } },
    };
    const Level level = Level::loadFromDefinition(definition, "robot solution");
    const std::vector<Input> inputs { Input::Right, Input::Right };
    const auto recording = solution::record(level, definition, inputs, "robot solution");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    if (recording.solution.steps.size() != 2) return;
    CHECK(recording.solution.steps[0].state.movables[0].cell == GridPosition3({ 2, 2, 1 }));
    CHECK(recording.solution.steps[1].state.movables[0].quarterTurns == 2);
    const auto parsed = solution::parse(solution::serialize(recording.solution));
    CHECK(parsed == recording.solution);
    CHECK(solution::replay(level, parsed).passed);

    solution::Driver waiting(level);
    CHECK(waiting.apply(Input::Up));
    CHECK(waiting.state().players[0].cell == GridPosition3({ 0, 0, 1 }));
    CHECK(waiting.state().movables[0].cell == GridPosition3({ 2, 2, 1 }));
}

void testStaggeredRobotPatrolsSettleAndResumeOnEachInput()
{
    TEST("staggeredRobotPatrolsSettleAndResumeOnEachInput");
    const Level::Definition definition {
        .layers = {
            { "........", "........", "........" },
            { "       E", " }    # ", " }    # " },
            { "        ", "        ", " C      " },
        },
    };
    const Level level = Level::loadFromDefinition(definition, "staggered robots");
    solution::Driver driver(level);

    // Stepping off the second robot delays its motor admission. If motors
    // keep restarting during settling, their staggered moves and short pauses
    // overlap forever and this input never reaches an idle boundary.
    CHECK(driver.apply(Input::Right));
    CHECK(driver.state().players[0].cell == GridPosition3({ 2, 2, 1 }));
    CHECK(driver.state().movables[0].cell == GridPosition3({ 2, 1, 1 }));
    CHECK(driver.state().movables[1].cell == GridPosition3({ 1, 2, 1 }));

    // A blocked input still advances one patrol beat, including a one-unit
    // shove of the hero. The previous tile's pause must neither skip a beat
    // nor leave robot rates disabled after the preceding input settles.
    for (int beat = 1; beat <= 3; ++beat) {
        CHECK(driver.apply(Input::Down));
        CHECK(driver.state().movables[0].cell == GridPosition3({ 2 + beat, 1, 1 }));
        CHECK(driver.state().movables[1].cell == GridPosition3({ 1 + beat, 2, 1 }));
        CHECK(driver.state().players[0].cell == GridPosition3({ 2 + beat, 2, 1 }));
    }
}

void testRecordSerializeParse()
{
    TEST("recordSerializeParse");
    const Level level =
        Level::loadFromDefinition(walkAndPushDefinition, "solution test");
    const std::vector<Input> inputs {
        Input::Right, Input::Right, Input::Down, Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    const solution::Recording recording = solution::record(
        level, walkAndPushDefinition, inputs, "test level");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    CHECK(recording.solution.steps.size() == inputs.size());
    if (recording.solution.steps.size() == inputs.size()) {
        // The push moves the hero and the rock.
        CHECK(recording.solution.steps[2].changes.size() == 2);
        CHECK(recording.solution.steps[0].changes.size() == 1);
    }

    const std::string text = solution::serialize(recording.solution);
    CHECK(text.find("step down p") != std::string::npos);
    const solution::Solution parsed = solution::parse(text);
    CHECK(parsed == recording.solution);
    CHECK(solution::replay(level, parsed).passed);

    // Stopping short and solving early are both refused.
    const std::vector<Input> shortInputs(inputs.begin(), inputs.end() - 1);
    CHECK(!solution::record(level, walkAndPushDefinition, shortInputs, "")
               .solved);
    std::vector<Input> longInputs = inputs;
    longInputs.push_back(Input::Left);
    const solution::Recording tooLong =
        solution::record(level, walkAndPushDefinition, longInputs, "");
    CHECK(!tooLong.solved);
    CHECK(tooLong.error.find("solved after step 7 of 8") != std::string::npos);

    checkThrows([] {
        (void)solution::parse("format 1\nlevel-digest 0000000000000000\n"
                              "step jump\n");
    }, "unknown inputs are rejected");
    checkThrows([] {
        (void)solution::parse("format 1\nstep up\n");
    }, "a solution needs its level digest");
    checkThrows([] {
        (void)solution::parse("format 1\nlevel-digest 0000000000000000\n"
                              "step up p1=1,2\n");
    }, "changes need three coordinates");
}

void testWardrobeStateSurvivesSolutionRoundTrip()
{
    TEST("wardrobeStateSurvivesSolutionRoundTrip");
    const Level::Definition definition {
        .layers = {
            { "..." },
            { "C9E" },
        },
        .character = CharacterType::Lorekeeper,
    };
    const Level level = Level::loadFromDefinition(definition, "wardrobe solution");
    const std::vector<Input> inputs { Input::Right, Input::Right };
    const solution::Recording recording = solution::record(
        level,
        definition,
        inputs,
        "wardrobe solution");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    CHECK(recording.solution.steps.size() == 2);
    if (recording.solution.steps.size() != 2) return;
    CHECK(recording.solution.steps[0].state.players[0].character ==
        CharacterType::Rogue);
    CHECK(recording.solution.steps[0].state.wardrobes[0].character ==
        CharacterType::Lorekeeper);

    const std::string text = solution::serialize(recording.solution);
    CHECK(text.find("\"wardrobes\"") != std::string::npos);
    const solution::Solution parsed = solution::parse(text);
    CHECK(parsed == recording.solution);
    CHECK(solution::replay(level, parsed).passed);
}

void testLeverStateSurvivesSolutionRoundTrip()
{
    TEST("leverStateSurvivesSolutionRoundTrip");
    const Level::Definition definition {
        .layers = { { "...." }, { "Q GE" } },
        .gates = { { .cell = { 2, 0, 1 }, .pressurePlates = { { 0, 0, 1 } } } },
        .plates = { { { 0, 0, 1 }, TileType::LeverWest } },
    };
    const Level level = Level::loadFromDefinition(definition, "lever solution");
    const std::vector<Input> inputs { Input::Interact, Input::Interact,
        Input::Interact, Input::Right, Input::Right, Input::Right };
    const auto recording = solution::record(level, definition, inputs, "lever solution");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    CHECK(recording.solution.steps.size() == inputs.size());
    if (recording.solution.steps.size() != inputs.size()) return;
    CHECK(recording.solution.steps[0].state.activeLevers.size() == 1);
    CHECK(recording.solution.steps[1].state.activeLevers.empty());
    CHECK(recording.solution.steps[5].state.activeLevers.size() == 1);
    const std::string text = solution::serialize(recording.solution);
    CHECK(text.find("\"activeLevers\"") != std::string::npos);
    const auto parsed = solution::parse(text);
    CHECK(parsed == recording.solution);
    CHECK(solution::replay(level, parsed).passed);
}

void testDigestTracksGameplayContentOnly()
{
    TEST("digestTracksGameplayContentOnly");
    Level::Definition decorated = walkAndPushDefinition;
    decorated.decorations.push_back({ .model = "tree" });
    CHECK(solution::levelDigest(decorated) ==
        solution::levelDigest(walkAndPushDefinition));
    Level::Definition padded = walkAndPushDefinition;
    padded.layers[1][2] += "   ";
    CHECK(solution::levelDigest(padded) ==
        solution::levelDigest(walkAndPushDefinition));
    Level::Definition moved = walkAndPushDefinition;
    moved.layers[1][1] = "   R  ";
    CHECK(solution::levelDigest(moved) !=
        solution::levelDigest(walkAndPushDefinition));
    Level::Definition linked = walkAndPushDefinition;
    linked.objectLinks.push_back({
        .cell = { 2, 1, 1 },
        .color = { 0.2f, 0.4f, 1.0f },
    });
    CHECK(solution::levelDigest(linked) !=
        solution::levelDigest(walkAndPushDefinition));
    auto portal = walkAndPushDefinition;
    portal.portals.push_back(
        { .cell = { 2, 1, 1 }, .color = { 0.2f, 0.4f, 1 } });
    const auto portalDigest = solution::levelDigest(portal);
    auto lock = walkAndPushDefinition;
    lock.lockPlates.push_back({ .cell = { 2, 1, 1 } });
    const auto lockDigest = solution::levelDigest(lock);
    CHECK(lockDigest != solution::levelDigest(walkAndPushDefinition));
    lock.lockPlates[0].startEnabled = true;
    CHECK(solution::levelDigest(lock) != lockDigest);
    const auto enabledDigest = solution::levelDigest(lock);
    lock.lockPlates[0].pressurePlates.push_back({ 1, 1, 1 });
    CHECK(solution::levelDigest(lock) != enabledDigest);
    CHECK(portalDigest != solution::levelDigest(walkAndPushDefinition));
    portal.portals[0].color.x = 0.2001f;
    CHECK(solution::levelDigest(portal) == portalDigest);
    portal.portals[0].color.x = 1.0f;
    CHECK(solution::levelDigest(portal) != portalDigest);

    // Gates: only a start-open gate adds to the digest.
    auto gated = walkAndPushDefinition;
    gated.layers[1][2] = "G     ";
    gated.gates.push_back({ .cell = { 0, 2, 1 } });
    const auto gatedDigest = solution::levelDigest(gated);
    gated.gates[0].startOpen = true;
    CHECK(solution::levelDigest(gated) != gatedDigest);
}

void testLegacyCoveredButtonKeepsRecordedSolutionDigest()
{
    TEST("legacyCoveredButtonKeepsRecordedSolutionDigest");
    const auto definition = Level::parseDefinition({
        "@plate {\"cell\":[0,0,1],\"tile\":\"Button\"}",
        "@layer 0", "...", "@layer 1", "Q E",
    }, "legacy covered button");
    // FNV-1a digest of this screen before the directional button names were
    // introduced, including the original '@plate 0,0,1:Button' spelling.
    constexpr std::uint64_t legacyDigest = 0xd2de77cf99b1263fULL;
    CHECK(solution::levelDigest(definition) == legacyDigest);
    const auto saved = Level::parseDefinition(
        Level::serializeDefinition(definition), "saved covered button");
    CHECK(solution::levelDigest(saved) == legacyDigest);
    const Level level = Level::loadFromDefinition(saved, "covered button replay");
    const std::vector<Input> inputs { Input::Right, Input::Right };
    const auto recording = solution::record(level, saved, inputs, "legacy button");
    CHECK(recording.solved);
    CHECK(recording.solution.levelDigest == legacyDigest);
    CHECK(solution::replay(level, recording.solution).passed);
}

void testReplayFailuresNameTheStepAndEntity()
{
    TEST("replayFailuresNameTheStepAndEntity");
    const Level level =
        Level::loadFromDefinition(walkAndPushDefinition, "solution test");
    const std::vector<Input> inputs {
        Input::Right, Input::Right, Input::Down, Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    const solution::Solution recorded = solution::record(
        level, walkAndPushDefinition, inputs, "").solution;

    // A rule change as the replay sees it: the rock no longer moves.
    Level::Definition walled = walkAndPushDefinition;
    walled.layers[1][2] = "  #   ";
    const Level blocked = Level::loadFromDefinition(walled, "blocked");
    const solution::ReplayReport report = solution::replay(blocked, recorded);
    CHECK(!report.passed);
    CHECK_MESSAGE(report.message.find("step 3 of 7 (down)") !=
            std::string::npos,
        report.message.c_str());
    CHECK_MESSAGE(report.message.find("should be at") != std::string::npos,
        report.message.c_str());

    // Something moving that the recording did not expect is named too.
    solution::Solution silent = recorded;
    silent.steps[0].changes.clear();
    const solution::ReplayReport unexpected = solution::replay(level, silent);
    CHECK(!unexpected.passed);
    CHECK_MESSAGE(unexpected.message.find("unexpectedly changed") !=
            std::string::npos,
        unexpected.message.c_str());
}

void writeScreen(
    const std::filesystem::path& path, const Level::Definition& definition)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    for (const std::string& line : Level::serializeDefinition(definition)) {
        stream << line << '\n';
    }
}

void writeText(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    if (!stream) throw std::runtime_error("cannot write fixture " + path.string());
}

solution::Solution coverageWalkSolution()
{
    const Level level = Level::loadFromDefinition(coverageWalkDefinition, "coverage walk");
    const std::vector<Input> inputs { Input::Right, Input::Right };
    const auto recording =
        solution::record(level, coverageWalkDefinition, inputs, "coverage walk");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    return recording.solution;
}

bool hasDiagnostic(const std::vector<std::string>& messages, std::string_view text)
{
    return std::ranges::any_of(messages, [text](const auto& message) {
        return message.find(text) != std::string::npos;
    });
}

void writeDraftCoverage(
    const std::filesystem::path& solutions,
    LevelLocation location,
    const Level::Definition& definition,
    std::string_view reason)
{
    // Fixture reasons below contain no characters requiring JSON escaping.
    std::ostringstream text;
    text << "{\"format\":1,\"drafts\":[{\"level\":" << location.level
         << ",\"screen\":" << location.screen << ",\"level-digest\":\""
         << solution::digestText(solution::levelDigest(definition))
         << "\",\"reason\":\"" << reason << "\"}]}";
    writeText(solutions / "coverage.json", text.str());
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream text;
    text << stream.rdbuf();
    return text.str();
}

std::size_t stepsIn(const std::filesystem::path& path)
{
    return solution::parse(readText(path)).stepCount();
}

bool hasKind(
    const std::vector<solution::StoreChange>& changes,
    solution::StoreChange::Kind kind)
{
    return std::ranges::any_of(changes, [kind](const auto& change) {
        return change.kind == kind;
    });
}

std::vector<solution::TimedFrame> timedMoves(std::span<const Input> inputs)
{
    std::vector<solution::TimedFrame> frames;
    frames.reserve(inputs.size());
    for (const Input input : inputs) {
        solution::TimedFrame frame { .elapsedSeconds = config::stepDurationSeconds };
        switch (input) {
        case Input::Up: frame.input.up.pressed = true; break;
        case Input::Down: frame.input.down.pressed = true; break;
        case Input::Left: frame.input.left.pressed = true; break;
        case Input::Right: frame.input.right.pressed = true; break;
        default: throw std::runtime_error("test fixture expects movement inputs");
        }
        frames.push_back(frame);
    }
    return frames;
}

void testTimedSolveReplaysAndStoresWhileCurrentKeepsMoving()
{
    TEST("timed solve verifies and stores a win while a lily pad is still circling");
    ScopedTestDirectory temp("sokoban-solution-timed-store");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    const auto screen = screenFilePath(levelDirectoryPath(levels, 0), 0);
    writeScreen(screen, circlingPadDefinition);
    LevelEditor editor;
    editor.initialize(levels, temp.path() / "runtime", 0, 0);
    const bool opened = editor.openDocument(screen);
    CHECK_MESSAGE(opened, editor.status().c_str());
    if (!opened) return;
    const auto playable = editor.beginDraftPlayback();
    CHECK_MESSAGE(playable.has_value(), editor.status().c_str());
    if (!playable) return;
    const Level& level = *playable;
    const auto definition = editor.documentDefinition();
    CHECK(solution::levelDigest(definition) == solution::levelDigest(circlingPadDefinition));
    const std::vector<Input> inputs { Input::Right, Input::Right };
    const auto legacy = solution::record(level, definition, inputs, "circling lily pad");
    CHECK(!legacy.solved);
    CHECK(legacy.error.find("step 1 never came to rest") != std::string::npos);

    constexpr std::size_t winningFrameCount = 18;
    std::vector<solution::TimedFrame> frames(winningFrameCount + 1);
    for (auto& frame : frames) {
        frame.elapsedSeconds = 1.0f / 32.0f;
    }
    frames[2].input.right.pressed = true;
    // The second press queues while the hero's first move and the pad's ride
    // are both in flight.
    frames[6].input.right.pressed = true;
    frames.back().input.left.pressed = true;

    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    for (std::size_t frame = 0; frame < winningFrameCount; ++frame) {
        const auto update = GameplayLoop::update(level, session, presentation,
            frames[frame].input, frames[frame].elapsedSeconds, true);
        CHECK(update.draftSolved == (frame + 1 == winningFrameCount));
        if (frame == 6) {
            CHECK(session.inFlight().size() == 2);
        }
    }
    CHECK(session.state().players[0].cell == GridPosition3({ 2, 0, 1 }));
    CHECK(session.state().movables[0].cell == GridPosition3({ 1, 1, 1 }));
    CHECK(session.inFlight().size() == 1);

    const auto recorded = solution::recordTimed(level, definition, frames, "screen0.scr (draft)");
    CHECK_MESSAGE(recorded.solved, recorded.error.c_str());
    CHECK(recorded.solution.timed.has_value());
    if (!recorded.solution.timed) return;
    CHECK(recorded.solution.stepCount() == inputs.size());
    CHECK(recorded.solution.timed->frames.size() == winningFrameCount);
    CHECK(recorded.solution.timed->finalState == session.state());

    const std::string text = solution::serialize(recorded.solution);
    CHECK(text.find("format 3\n") != std::string::npos);
    const auto parsed = solution::parse(text);
    CHECK(parsed == recorded.solution);
    CHECK(solution::replay(level, parsed).passed);
    if (!parsed.timed) return;

    auto changed = parsed;
    changed.timed->finalState.movables[0].cell = { 0, 1, 1 };
    const auto wrongState = solution::replay(level, changed);
    CHECK(!wrongState.passed);
    CHECK(wrongState.message.find("frame 18") != std::string::npos);
    changed = parsed;
    changed.timed->frames[6].input.right.pressed = false;
    CHECK(!solution::replay(level, changed).passed);
    changed = parsed;
    ++changed.timed->inputCount;
    CHECK(!solution::replay(level, changed).passed);

    const auto changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr (draft)", definition, frames });
    CHECK(std::ranges::any_of(changes, [&](const auto& change) {
        return change.kind == solution::StoreChange::Kind::Saved && change.steps == inputs.size();
    }));
    const auto file = solutions / solution::fileNameFor({ 0, 0 });
    CHECK(std::filesystem::is_regular_file(file));
    if (!std::filesystem::is_regular_file(file)) return;
    const auto stored = solution::parse(readText(file));
    CHECK(stored.recordedFor == "level0/screen0");
    CHECK(stored.timed == parsed.timed);
    const auto coverage = solution::auditCoverage(levels, solutions);
    CHECK(coverage.passed());
    CHECK(coverage.screens == 1 && coverage.replayed == 1);

    auto draft = definition;
    draft.layers[0][0].back() = 'W';
    const auto draftLevel = Level::loadFromDefinition(draft, "edited circling lily pad");
    const auto draftDigest = solution::levelDigest(draft);
    CHECK(draftDigest != stored.levelDigest);
    CHECK(hasKind(solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr (draft)", draft, frames }),
        solution::StoreChange::Kind::Saved));
    const auto draftFile = solutions / "drafts" / (solution::digestText(draftDigest) + ".solution");
    CHECK(std::filesystem::is_regular_file(draftFile));
    if (!std::filesystem::is_regular_file(draftFile)) return;
    const std::string draftText = readText(draftFile);
    const auto draftProof = solution::parse(draftText);
    CHECK(solution::replay(draftLevel, draftProof).passed);
    CHECK(solution::parse(readText(file)) == stored);
    if (!draftProof.timed) return;

    auto invalidDraft = draftProof;
    ++invalidDraft.timed->inputCount;
    writeText(draftFile, solution::serialize(invalidDraft));
    writeScreen(screen, draft);
    CHECK(!hasKind(solution::reconcileStore(levels, solutions), solution::StoreChange::Kind::Moved));
    CHECK(solution::parse(readText(file)) == stored);
    CHECK(!solution::auditCoverage(levels, solutions).passed());

    writeText(draftFile, draftText);
    CHECK(hasKind(solution::reconcileStore(levels, solutions), solution::StoreChange::Kind::Moved));
    CHECK(!std::filesystem::exists(draftFile));
    const auto promoted = solution::parse(readText(file));
    CHECK(promoted.recordedFor == "level0/screen0");
    CHECK(promoted.levelDigest == draftDigest);
    CHECK(promoted.timed == draftProof.timed);
    CHECK(solution::auditCoverage(levels, solutions).passed());
}

void testCampaignTimedSolveStopsOnWinningCommit()
{
    TEST("campaign timed replay stops at the winning commit during continuous motion");
    const auto level = Level::loadFromDefinition(circlingPadDefinition, "campaign current");
    constexpr std::size_t winningFrameCount = 31;
    std::vector<solution::TimedFrame> frames(winningFrameCount + 1);
    for (auto& frame : frames) {
        frame.elapsedSeconds = 1.0f / 32.0f;
    }
    frames[15].input.right.pressed = true;
    frames[19].input.right.pressed = true;
    // The hero reaches the End at 31/32 second. The pad's next leg ends
    // at one second, also within the last frame.
    frames[winningFrameCount - 1].elapsedSeconds *= 2.0f;
    frames.back().input.left.pressed = true;

    const auto play = [&](bool playingDraft) {
        GameplaySession session;
        session.reset(level);
        GameplayPresentation presentation;
        presentation.resetEntities(session.state());
        for (std::size_t frame = 0; frame < winningFrameCount; ++frame) {
            const auto update = GameplayLoop::update(level, session, presentation,
                frames[frame].input, frames[frame].elapsedSeconds, playingDraft);
            const bool winningFrame = frame + 1 == winningFrameCount;
            CHECK(update.screenSolved == (winningFrame && !playingDraft));
            CHECK(update.draftSolved == (winningFrame && playingDraft));
        }
        CHECK(session.playerMoveCount() == 2);
        return session.state();
    };
    const auto campaign = play(false);
    CHECK(campaign.players[0].cell == GridPosition3({ 2, 0, 1 }));
    CHECK(campaign.movables[0].cell == GridPosition3({ 1, 1, 1 }));
    CHECK(play(true).movables[0].cell == GridPosition3({ 1, 2, 1 }));

    ScopedTestDirectory temp("sokoban-solution-campaign-timing");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    writeScreen(screenFilePath(levelDirectoryPath(levels, 0), 0), circlingPadDefinition);
    const auto changes = solution::reconcileStore(levels, solutions, solution::SolveToStore {
        .name = "level0/screen0",
        .definition = circlingPadDefinition,
        .frames = frames,
        .playingDraft = false,
    });
    CHECK(hasKind(changes, solution::StoreChange::Kind::Saved));
    const auto file = solutions / solution::fileNameFor({ 0, 0 });
    CHECK(std::filesystem::is_regular_file(file));
    if (!std::filesystem::is_regular_file(file)) return;
    const auto stored = solution::parse(readText(file));
    CHECK(stored.timed.has_value());
    if (!stored.timed) return;
    CHECK(stored.stepCount() == 2);
    CHECK(stored.timed->frames.size() == winningFrameCount);
    CHECK(stored.timed->finalState == campaign);
    CHECK(!stored.timed->playingDraft);
    CHECK(solution::replay(level, stored).passed);
    auto wrongMode = stored;
    wrongMode.timed->playingDraft = true;
    CHECK(!solution::replay(level, wrongMode).passed);
    CHECK(solution::auditCoverage(levels, solutions).passed());
}

void testStoreRejectsIncompleteTimedCapture()
{
    TEST("the store only publishes a complete timed capture");
    const auto level = Level::loadFromDefinition(coverageWalkDefinition, "timed walk");
    ScopedTestDirectory temp("sokoban-solution-timed-first");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    writeScreen(screenFilePath(levelDirectoryPath(levels, 0), 0), coverageWalkDefinition);
    solution::SolveToStore solve {
        .name = "level0/screen0",
        .definition = coverageWalkDefinition,
        .playingDraft = false,
    };
    const auto file = solutions / solution::fileNameFor({ 0, 0 });
    CHECK(hasKind(solution::reconcileStore(levels, solutions, solve),
        solution::StoreChange::Kind::NotRecorded));
    CHECK(!std::filesystem::exists(file));
    solve.frames = timedMoves(std::array { Input::Right });
    CHECK(hasKind(solution::reconcileStore(levels, solutions, solve),
        solution::StoreChange::Kind::NotRecorded));
    CHECK(!std::filesystem::exists(file));

    solve.frames = timedMoves(std::array { Input::Right, Input::Right });
    CHECK(hasKind(solution::reconcileStore(levels, solutions, solve),
        solution::StoreChange::Kind::Saved));
    CHECK(std::filesystem::is_regular_file(file));
    if (!std::filesystem::is_regular_file(file)) return;
    const auto stored = solution::parse(readText(file));
    CHECK(stored.timed.has_value());
    CHECK(stored.stepCount() == 2);
    CHECK(solution::replay(level, stored).passed);
}

void testStoreKeepsOneShortestRecordingPerScreen()
{
    TEST("storeKeepsOneShortestRecordingPerScreen");
    using Kind = solution::StoreChange::Kind;
    ScopedTestDirectory temp("sokoban-solution-store");
    const std::filesystem::path levels = temp.path() / "levels";
    const std::filesystem::path solutions = temp.path() / "solutions";
    const std::filesystem::path screen =
        screenFilePath(levelDirectoryPath(levels, 0), 0);
    const std::filesystem::path screenFile =
        solutions / solution::fileNameFor({ .level = 0, .screen = 0 });
    writeScreen(screen, walkAndPushDefinition);

    const std::vector<Input> shortest {
        Input::Right, Input::Right, Input::Down, Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    std::vector<Input> longer { Input::Right, Input::Left };
    longer.insert(longer.end(), shortest.begin(), shortest.end());

    // A longer solve is saved when nothing is stored yet...
    auto changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition, timedMoves(longer) });
    CHECK(hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == longer.size());
    // ...replaced by a shorter one, which is then kept over a longer one.
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition, timedMoves(shortest) });
    CHECK(hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == shortest.size());
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition, timedMoves(longer) });
    CHECK(hasKind(changes, Kind::KeptExisting));
    CHECK(!hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == shortest.size());

    // A solve that does not replay is reported and stores nothing.
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition,
            timedMoves(std::array { Input::Right }) });
    CHECK(hasKind(changes, Kind::NotRecorded));

    // A draft that is not on disk waits in drafts/...
    Level::Definition draft = walkAndPushDefinition;
    draft.layers[1][0] = "C     ";
    draft.layers[1][2] = "     E";
    const std::vector<Input> draftInputs {
        Input::Down, Input::Down, Input::Right, Input::Right,
        Input::Right, Input::Right, Input::Right,
    };
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr (draft)", draft, timedMoves(draftInputs) });
    CHECK(hasKind(changes, Kind::Saved));
    const std::filesystem::path draftFile = solutions / "drafts" /
        (solution::digestText(solution::levelDigest(draft)) + ".solution");
    CHECK(std::filesystem::is_regular_file(draftFile));
    CHECK(stepsIn(screenFile) == shortest.size());

    // ...and takes the screen's file once the draft is saved. The old
    // recording is parked in drafts/ rather than lost.
    writeScreen(screen, draft);
    changes = solution::reconcileStore(levels, solutions);
    CHECK(hasKind(changes, Kind::Moved));
    CHECK(stepsIn(screenFile) == draftInputs.size());
    CHECK(!std::filesystem::exists(draftFile));
    const std::filesystem::path parked = solutions / "drafts" /
        (solution::digestText(solution::levelDigest(walkAndPushDefinition)) +
            ".solution");
    CHECK(std::filesystem::is_regular_file(parked));

    // Undoing the edit brings the original recording back.
    writeScreen(screen, walkAndPushDefinition);
    changes = solution::reconcileStore(levels, solutions);
    CHECK(stepsIn(screenFile) == shortest.size());
    CHECK(std::filesystem::is_regular_file(draftFile));
    CHECK(!std::filesystem::exists(parked));

    // Nothing to do leaves everything alone.
    CHECK(solution::reconcileStore(levels, solutions).empty());
}

void testStorePrefersPassingRecordingsOverShorterInvalidOnes()
{
    TEST("storePrefersPassingRecordingsOverShorterInvalidOnes");
    using Kind = solution::StoreChange::Kind;
    const auto level = Level::loadFromDefinition(coverageWalkDefinition, "coverage walk");
    auto invalid = coverageWalkSolution();
    invalid.steps.front().changes.clear();
    CHECK(!solution::replay(level, invalid).passed);
    const auto invalidText = solution::serialize(invalid);
    const std::vector<Input> longer { Input::Right, Input::Left, Input::Right, Input::Right };

    for (const bool savedScreen : { false, true }) {
        ScopedTestDirectory temp("sokoban-solution-store-verify");
        const auto levels = temp.path() / "levels";
        const auto solutions = temp.path() / "solutions";
        const auto draft = solutions / "drafts" /
            (solution::digestText(invalid.levelDigest) + ".solution");
        const auto canonical = solutions / solution::fileNameFor({ 0, 0 });
        const auto& path = savedScreen ? canonical : draft;
        if (savedScreen) {
            writeScreen(screenFilePath(levelDirectoryPath(levels, 0), 0), coverageWalkDefinition);
        }
        writeText(path, invalidText);

        CHECK(solution::reconcileStore(levels, solutions).empty());
        CHECK(readText(path) == invalidText);

        const auto changes = solution::reconcileStore(levels, solutions,
            solution::SolveToStore { "coverage walk", coverageWalkDefinition, timedMoves(longer) });
        CHECK(hasKind(changes, Kind::Saved));
        CHECK(!hasKind(changes, Kind::KeptExisting));
        const auto passing = readText(path);
        CHECK(solution::replay(level, solution::parse(passing)).passed);
        CHECK(stepsIn(path) == longer.size());

        if (savedScreen) {
            // Reconcile-only must promote a passing draft even when the
            // current screen already has a shorter, broken recording.
            writeText(draft, passing);
            writeText(canonical, invalidText);
            CHECK(hasKind(solution::reconcileStore(levels, solutions), Kind::Moved));
            CHECK(solution::replay(level, solution::parse(readText(canonical))).passed);
            CHECK(!std::filesystem::exists(draft));
        }
    }
}

void testStoreRestoresOriginalsWhenPublicationFails()
{
    TEST("storeRestoresOriginalsWhenPublicationFails");
    const Level::Definition revised { .layers = { { "...." }, { "C  E" } } };
    const auto level = Level::loadFromDefinition(revised, "revised coverage walk");
    const std::vector<Input> inputs { Input::Right, Input::Right, Input::Right };
    const auto recording = solution::record(level, revised, inputs, "draft");
    CHECK_MESSAGE(recording.solved, recording.error.c_str());
    const auto original = coverageWalkSolution();
    const auto originalText = solution::serialize(original);
    const auto draftText = solution::serialize(recording.solution);

    for (const bool afterInstall : { false, true }) {
        ScopedTestDirectory temp("sokoban-solution-store-rollback");
        const auto levels = temp.path() / "levels";
        const auto solutions = temp.path() / "solutions";
        const auto canonical = solutions / solution::fileNameFor({ 0, 0 });
        const auto draft = solutions / "drafts" /
            (solution::digestText(recording.solution.levelDigest) + ".solution");
        const auto parked = solutions / "drafts" /
            (solution::digestText(original.levelDigest) + ".solution");
        const auto coverage = solutions / "coverage.json";
        writeScreen(screenFilePath(levelDirectoryPath(levels, 0), 0), revised);
        writeText(canonical, originalText);
        writeText(draft, draftText);
        writeText(coverage, "{\"format\":1,\"drafts\":[]}\n");

        // Two replacements and two backups are staged first. One live file
        // is then installed before the second publication reports failure.
        atomicFile::failReplaceAfterForTesting(5, std::errc::permission_denied, afterInstall);
        bool failed = false;
        try {
            (void)solution::reconcileStore(levels, solutions);
        } catch (const std::system_error& error) {
            failed = error.code() == std::make_error_code(std::errc::permission_denied);
        }
        CHECK(failed);
        CHECK(readText(canonical) == originalText);
        CHECK(readText(draft) == draftText);
        CHECK(!std::filesystem::exists(parked));
        CHECK(readText(coverage) == "{\"format\":1,\"drafts\":[]}\n");

        CHECK(hasKind(solution::reconcileStore(levels, solutions), solution::StoreChange::Kind::Moved));
        CHECK(solution::replay(level, solution::parse(readText(canonical))).passed);
        CHECK(readText(parked) == originalText);
        CHECK(!std::filesystem::exists(draft));
    }
}

void testCoverageRequiresCurrentPassingRecording()
{
    TEST("coverageRequiresCurrentPassingRecording");
    ScopedTestDirectory temp("sokoban-solution-coverage-current");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    const auto screen = screenFilePath(levelDirectoryPath(levels, 0), 0);
    const auto recordingFile = solutions / solution::fileNameFor({ 0, 0 });
    writeScreen(screen, coverageWalkDefinition);

    const auto missing = solution::auditCoverage(levels, solutions);
    CHECK(!missing.passed());
    CHECK(missing.screens == 1 && missing.replayed == 0 && missing.drafts == 0);
    CHECK(missing.errors.size() == 1);
    CHECK(hasDiagnostic(missing.errors, "level0/screen0: missing current recording"));

    writeText(recordingFile, solution::serialize(coverageWalkSolution()));
    const auto current = solution::auditCoverage(levels, solutions);
    CHECK(current.passed());
    CHECK(current.screens == 1 && current.replayed == 1 && current.drafts == 0);
    CHECK(current.notes.empty());

    // The old recording still has this screen's filename and valid syntax,
    // but changing gameplay content must make coverage fail until re-recorded.
    Level::Definition changed { .layers = { { "...." }, { "C  E" } } };
    CHECK(solution::levelDigest(changed) != solution::levelDigest(coverageWalkDefinition));
    writeScreen(screen, changed);
    const auto stale = solution::auditCoverage(levels, solutions);
    CHECK(!stale.passed());
    CHECK(stale.screens == 1 && stale.replayed == 0 && stale.drafts == 0);
    CHECK(stale.errors.size() == 1);
    CHECK(hasDiagnostic(stale.errors, "level0/screen0: missing current recording"));
    CHECK(hasDiagnostic(stale.notes, "level0-screen0.solution: matches no current screen"));
}

void testCoverageRejectsMatchingDigestWithInvalidReplay()
{
    TEST("coverageRejectsMatchingDigestWithInvalidReplay");
    ScopedTestDirectory temp("sokoban-solution-coverage-replay");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    writeScreen(screenFilePath(levelDirectoryPath(levels, 0), 0), coverageWalkDefinition);
    auto invalid = coverageWalkSolution();
    CHECK(!invalid.steps.empty());
    if (invalid.steps.empty()) return;
    invalid.steps[0].changes.clear();
    writeText(solutions / solution::fileNameFor({ 0, 0 }), solution::serialize(invalid));
    writeText(solutions / "zz-passing.solution", solution::serialize(coverageWalkSolution()));

    const auto report = solution::auditCoverage(levels, solutions);
    CHECK(!report.passed());
    CHECK(report.screens == 1 && report.replayed == 0 && report.drafts == 0);
    CHECK(report.errors.size() == 1);
    CHECK(hasDiagnostic(report.errors, "level0/screen0 (level0-screen0.solution)"));
    CHECK(hasDiagnostic(report.errors, "step 1 of 2 (right)"));
    CHECK(hasDiagnostic(report.errors, "unexpectedly changed"));

    // A draft exempts missing coverage, not a matching recording that now
    // disagrees with the rules.
    writeDraftCoverage(solutions, { 0, 0 }, coverageWalkDefinition,
        "Puzzle draft awaiting review.");
    const auto exempted = solution::auditCoverage(levels, solutions);
    CHECK(!exempted.passed());
    CHECK(exempted.screens == 1 && exempted.replayed == 0 && exempted.drafts == 1);
    CHECK(hasDiagnostic(exempted.errors, "step 1 of 2 (right)"));
    CHECK(hasDiagnostic(exempted.errors, "unexpectedly changed"));
}

void testCoverageDraftMustMatchCurrentScreenAndHaveReason()
{
    TEST("coverageDraftMustMatchCurrentScreenAndHaveReason");
    ScopedTestDirectory temp("sokoban-solution-coverage-draft");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    const auto screen = screenFilePath(levelDirectoryPath(levels, 0), 0);
    const Level::Definition draft { .layers = { { "..." }, { "C  " } } };
    constexpr std::string_view reason = "Mechanics draft has no completion objective.";
    writeScreen(screen, draft);
    writeDraftCoverage(solutions, { 0, 0 }, draft, reason);

    const auto current = solution::auditCoverage(levels, solutions);
    CHECK(current.passed());
    CHECK(current.screens == 1 && current.replayed == 0 && current.drafts == 1);
    CHECK(current.notes.size() == 1);
    CHECK(hasDiagnostic(current.notes, "level0/screen0: draft: Mechanics draft"));

    CHECK(std::filesystem::remove(solutions / "coverage.json"));
    const auto unlisted = solution::auditCoverage(levels, solutions);
    CHECK(!unlisted.passed());
    CHECK(unlisted.screens == 1 && unlisted.drafts == 0);
    CHECK(hasDiagnostic(unlisted.errors, "level0/screen0: missing current recording"));
    writeDraftCoverage(solutions, { 0, 0 }, draft, reason);

    Level::Definition changed = draft;
    changed.layers[1][0] = "CI ";
    CHECK(solution::levelDigest(changed) != solution::levelDigest(draft));
    writeScreen(screen, changed);
    const auto stale = solution::auditCoverage(levels, solutions);
    CHECK(!stale.passed());
    CHECK(stale.screens == 1 && stale.replayed == 0 && stale.drafts == 0);
    CHECK(hasDiagnostic(stale.errors, "level0/screen0: draft changed"));
    CHECK(hasDiagnostic(stale.errors, "level0/screen0: missing current recording"));

    writeScreen(screen, draft);
    writeDraftCoverage(solutions, { 0, 0 }, draft, "   ");
    const auto unexplained = solution::auditCoverage(levels, solutions);
    CHECK(!unexplained.passed());
    CHECK(unexplained.drafts == 0);
    CHECK(hasDiagnostic(unexplained.errors, "coverage.json: draft needs a nonempty reason"));
    CHECK(hasDiagnostic(unexplained.errors, "level0/screen0: missing current recording"));

    writeDraftCoverage(solutions, { 0, 0 }, draft, reason);
    CHECK(std::filesystem::remove(screen));
    const auto missing = solution::auditCoverage(levels, solutions);
    CHECK(!missing.passed());
    CHECK(missing.screens == 0 && missing.drafts == 0);
    CHECK(hasDiagnostic(missing.errors,
        "level0/screen0: coverage exception names a missing screen"));
}

void testCoverageEnumeratesBeyondLevelAndScreenGaps()
{
    TEST("coverageEnumeratesBeyondLevelAndScreenGaps");
    ScopedTestDirectory temp("sokoban-solution-coverage-gaps");
    const auto levels = temp.path() / "levels";
    const auto solutions = temp.path() / "solutions";
    const std::vector<LevelLocation> locations { { 0, 0 }, { 0, 2 }, { 2, 0 }, { 4, 3 } };
    for (const auto location : locations) {
        writeScreen(screenFilePath(levelDirectoryPath(levels, location.level), location.screen),
            coverageWalkDefinition);
    }
    const auto missing = solution::auditCoverage(levels, solutions);
    CHECK(!missing.passed());
    CHECK(missing.screens == locations.size());
    CHECK(missing.replayed == 0 && missing.drafts == 0);
    CHECK(missing.errors.size() == locations.size());
    for (const auto location : locations) {
        CHECK(hasDiagnostic(missing.errors,
            "level" + std::to_string(location.level) + "/screen" +
                std::to_string(location.screen) + ": missing current recording"));
    }

    // One matching gameplay digest covers each identical screen regardless of
    // gaps or filenames; every discovered screen must still be replayed.
    writeText(solutions / solution::fileNameFor({ 0, 0 }),
        solution::serialize(coverageWalkSolution()));
    const auto covered = solution::auditCoverage(levels, solutions);
    CHECK(covered.passed());
    CHECK(covered.screens == locations.size() && covered.replayed == locations.size());
    CHECK(covered.drafts == 0);
}

void testCoverageRejectsDuplicateNumericScreenLocations()
{
    TEST("coverageRejectsDuplicateNumericScreenLocations");
    for (const bool aliasLevel : { false, true }) {
        ScopedTestDirectory temp("sokoban-solution-coverage-alias");
        const auto levels = temp.path() / "levels";
        const auto solutions = temp.path() / "solutions";
        const auto screen = screenFilePath(levelDirectoryPath(levels, 1), 0);
        const auto alias = aliasLevel
            ? levels / "level01" / "screen0.scr"
            : levels / "level1" / "screen00.scr";
        writeScreen(screen, coverageWalkDefinition);
        writeScreen(alias, coverageWalkDefinition);
        writeText(solutions / solution::fileNameFor({ 1, 0 }),
            solution::serialize(coverageWalkSolution()));

        const auto report = solution::auditCoverage(levels, solutions);
        CHECK(!report.passed());
        CHECK(report.screens == 1 && report.replayed == 1 && report.drafts == 0);
        CHECK(report.errors.size() == 1);
        CHECK(hasDiagnostic(report.errors, "level1/screen0: duplicate screen location"));
        CHECK(hasDiagnostic(report.errors, screen.string()));
        CHECK(hasDiagnostic(report.errors, alias.string()));
    }
}

void testCurrentCampaignHasCompletePassingSolutionCoverage()
{
    TEST("currentCampaignHasCompletePassingSolutionCoverage");
    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path root = SOKOBAN_TEST_SOURCE_DIR;
    const auto report = solution::auditCoverage(root / "levels", root / "solutions");
    std::string diagnostics = "Current campaign solution coverage failed:";
    for (const auto& error : report.errors) diagnostics += "\n  " + error;
    CHECK_MESSAGE(report.passed(), diagnostics.c_str());
    CHECK(report.screens > 0);
    for (const auto& note : report.notes) std::cout << "  note: " << note << '\n';
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "  audited " << report.screens << " puzzle screens: "
              << report.replayed << " current recordings replayed, " << report.drafts
              << " reviewed drafts in " << seconds << " s\n";
}

} // namespace

int main()
{
    try {
        testDruidTransformationAndUndoPreserveHeroOrder();
        testRobotPatrolRecordsFiniteBeatsAndReplaysReversals();
        testStaggeredRobotPatrolsSettleAndResumeOnEachInput();
        testRecordSerializeParse();
        testWardrobeStateSurvivesSolutionRoundTrip();
        testLeverStateSurvivesSolutionRoundTrip();
        testDigestTracksGameplayContentOnly();
        testLegacyCoveredButtonKeepsRecordedSolutionDigest();
        testReplayFailuresNameTheStepAndEntity();
        testTimedSolveReplaysAndStoresWhileCurrentKeepsMoving();
        testCampaignTimedSolveStopsOnWinningCommit();
        testStoreRejectsIncompleteTimedCapture();
        testStoreKeepsOneShortestRecordingPerScreen();
        testStorePrefersPassingRecordingsOverShorterInvalidOnes();
        testStoreRestoresOriginalsWhenPublicationFails();
        testCoverageRequiresCurrentPassingRecording();
        testCoverageRejectsMatchingDigestWithInvalidReplay();
        testCoverageDraftMustMatchCurrentScreenAndHaveReason();
        testCoverageEnumeratesBeyondLevelAndScreenGaps();
        testCoverageRejectsDuplicateNumericScreenLocations();
        testCurrentCampaignHasCompletePassingSolutionCoverage();
    } catch (const std::exception& error) {
        std::cerr << "SolutionReplayTests: unexpected exception: "
                  << error.what() << "\n";
        return 2;
    }
    if (failures == 0) {
        std::cout << "SolutionReplayTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SolutionReplayTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
