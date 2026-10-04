// Recorded solutions: file format, recording, readable replay failures, and a
// replay of every recorded solution against the current rules and levels.

#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Solution.hpp"
#include "engine/SolutionCoverage.hpp"
#include "engine/SolutionStore.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
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

void testRecordSerializeParse()
{
    TEST("recordSerializeParse");
    const Level level =
        Level::loadFromDefinition(walkAndPushDefinition, "solution test");
    const std::vector<Input> inputs {
        Input::Right, Input::Right, Input::Down,  Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    const solution::Recording recording =
        solution::record(level, walkAndPushDefinition, inputs, "test level");
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

    checkThrows(
        [] {
            (void)solution::parse(
                "format 2\nlevel-digest 0000000000000000\n"
                "step jump\n");
        },
        "unknown inputs are rejected");
    checkThrows(
        [] { (void)solution::parse("format 2\nstep up\n"); },
        "a solution needs its level digest");
    checkThrows(
        [] {
            (void)solution::parse(
                "format 2\nlevel-digest 0000000000000000\n"
                "step up p1=1,2\n");
        },
        "changes need three coordinates");
    checkThrows(
        [] {
            (void)solution::parse("format 1\nlevel-digest 0000000000000000\n");
        },
        "old formats require re-recording");
    checkThrows(
        [] {
            (void)solution::parse(
                "format 2\nlevel-digest 0000000000000000\nstep up\n");
        },
        "every step needs a complete state");
    checkThrows(
        [] {
            (void)solution::parse(
                "format 2\nlevel-digest 0000000000000000\nstep up\nstate {}\n");
        },
        "incomplete states are rejected");
    checkThrows(
        [&] { (void)solution::parse(text + "state {}\n"); },
        "duplicate state lines are rejected");
}

void testDigestTracksGameplayContentOnly()
{
    TEST("digestTracksGameplayContentOnly");
    Level::Definition decorated = walkAndPushDefinition;
    decorated.decorations.push_back({ .model = "tree" });
    CHECK(
        solution::levelDigest(decorated) ==
        solution::levelDigest(walkAndPushDefinition));
    Level::Definition padded = walkAndPushDefinition;
    padded.layers[1][2] += "   ";
    CHECK(
        solution::levelDigest(padded) ==
        solution::levelDigest(walkAndPushDefinition));
    Level::Definition moved = walkAndPushDefinition;
    moved.layers[1][1] = "   R  ";
    CHECK(
        solution::levelDigest(moved) !=
        solution::levelDigest(walkAndPushDefinition));
    Level::Definition linked = walkAndPushDefinition;
    linked.objectLinks.push_back(
        {
            .cell = { 2, 1, 1 },
            .color = { 0.2f, 0.4f, 1.0f },
        });
    CHECK(
        solution::levelDigest(linked) !=
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

    // Gate links are gameplay; color and record/link order are bookkeeping.
    auto gated = walkAndPushDefinition;
    gated.layers[1][2] = "G     ";
    gated.gates.push_back({ .cell = { 0, 2, 1 } });
    const auto gatedDigest = solution::levelDigest(gated);
    gated.gates[0].color = { 0.1f, 0.2f, 0.3f };
    CHECK(solution::levelDigest(gated) == gatedDigest);
    gated.gates[0].pressurePlates = { { 1, 2, 1 }, { 2, 2, 1 } };
    CHECK(solution::levelDigest(gated) != gatedDigest);
    const auto linkedDigest = solution::levelDigest(gated);
    std::ranges::reverse(gated.gates[0].pressurePlates);
    CHECK(solution::levelDigest(gated) == linkedDigest);
    gated.gates[0].pressurePlates[0].x = 3;
    CHECK(solution::levelDigest(gated) != linkedDigest);
    gated.gates[0].pressurePlates.clear();
    gated.gates[0].startOpen = true;
    CHECK(solution::levelDigest(gated) != gatedDigest);
    gated.gates.push_back({ .cell = { 3, 2, 1 }, .startOpen = true });
    const auto twoGates = solution::levelDigest(gated);
    std::ranges::reverse(gated.gates);
    CHECK(solution::levelDigest(gated) == twoGates);
}

void testCompleteStateRoundTripAndReplay()
{
    TEST("completeStateRoundTripAndReplay");
    const Level level =
        Level::loadFromDefinition(walkAndPushDefinition, "state test");
    const std::vector<Input> inputs {
        Input::Right, Input::Right, Input::Down,  Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    const auto recorded =
        solution::record(level, walkAndPushDefinition, inputs, "").solution;
    const std::vector<std::function<void(solution::Step&)>> mutations {
        [](auto& s) { s.automaticMotionPaused = true; },
        [](auto& s) { ++s.activeHeroController; },
        [](auto& s) { ++s.state.players[0].id; },
        [](auto& s) { ++s.state.players[0].controller; },
        [](auto& s) { s.state.players[0].character = CharacterType::Bard; },
        [](auto& s) { s.state.players[0].quarterTurns = 1; },
        [](auto& s) { s.state.players[0].sliding = MoveDirection::Down; },
        [](auto& s) { s.state.players[0].dead = true; },
        [](auto& s) { s.state.players[0].drowned = true; },
        [](auto& s) { ++s.state.players[0].cell.z; },
        [](auto& s) { ++s.state.movables[0].id; },
        [](auto& s) { s.state.movables[0].type = TileType::TurretNorth; },
        [](auto& s) { s.state.movables[0].quarterTurns = 1; },
        [](auto& s) { s.state.movables[0].sliding = MoveDirection::Left; },
        [](auto& s) { s.state.movables[0].dead = true; },
        [](auto& s) { s.state.movables[0].fallen = true; },
        [](auto& s) { ++s.state.movables[0].cell.z; },
        [](auto& s) {
            s.state.enemies.push_back({ .id = 99, .cell = { 4, 2, 1 } });
        },
        [](auto& s) {
            s.state.turnedMirrors.push_back(
                { .cell = { 2, 2, 1 }, .quarterTurns = 1 });
        },
        [](auto& s) {
            s.state.elevators.push_back({ .cell = { 2, 2, 1 }, .phase = 1 });
        },
        [](auto& s) {
            s.state.minecarts.push_back({ .cell = { 2, 2, 1 }, .phase = 2 });
        },
        [](auto& s) { s.state.activeButtons.push_back({ 2, 2, 1 }); },
    };
    for (const auto& mutate : mutations) {
        auto changed = recorded;
        mutate(changed.steps[0]);
        const auto result = solution::replay(
            level, solution::parse(solution::serialize(changed)));
        CHECK(!result.passed);
        CHECK_MESSAGE(
            result.message.find("step 1 of 7 (right): state /") !=
                std::string::npos,
            result.message.c_str());
    }
    // Exercise every serialized field, including fields absent from this
    // puzzle.
    auto allFields = recorded;
    auto& step = allFields.steps[0];
    step.activeHeroController = std::numeric_limits<EntityId>::max();
    step.state.players[0].id = std::numeric_limits<EntityId>::max();
    step.changes[0].id = step.state.players[0].id;
    step.state.players[0].character.reset();
    step.state.enemies.push_back(
        { .id = 99,
          .cell = { -1, 2, 3 },
          .fallen = true,
          .dead = true,
          .sliding = MoveDirection::Up,
          .quarterTurns = 3 });
    step.state.turnedMirrors.push_back(
        { .cell = { 1, 2, 3 }, .quarterTurns = 2 });
    step.state.elevators.push_back({ .cell = { 1, 2, 3 }, .phase = 255 });
    step.state.minecarts.push_back({ .cell = { 1, 2, 3 }, .phase = 65535 });
    step.state.activeButtons = { { -2, 4, 1 }, { 5, 6, 2 } };
    const auto serialized = solution::serialize(allFields);
    CHECK(solution::parse(serialized) == allFields);
    checkThrows(
        [&] {
            auto malformed = serialized;
            const auto position = malformed.find("\"phase\":255");
            malformed.replace(position, 11, "\"phase\":256");
            (void)solution::parse(malformed);
        },
        "device phases must not truncate");
    checkThrows(
        [&] {
            auto malformed = serialized;
            const auto position = malformed.find("\"quarterTurns\":0");
            malformed.replace(position, 16, "\"quarterTurns\":4");
            (void)solution::parse(malformed);
        },
        "turns are bounded");
    checkThrows(
        [&] {
            auto malformed = serialized;
            const auto position = malformed.find("\"activeHeroController\":");
            const auto end = malformed.find(',', position);
            malformed.replace(
                position, end - position, "\"activeHeroController\":-1");
            (void)solution::parse(malformed);
        },
        "negative identities must not wrap to uint64");
}

void testReplayFailuresNameTheStepAndEntity()
{
    TEST("replayFailuresNameTheStepAndEntity");
    const Level level =
        Level::loadFromDefinition(walkAndPushDefinition, "solution test");
    const std::vector<Input> inputs {
        Input::Right, Input::Right, Input::Down,  Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    const solution::Solution recorded =
        solution::record(level, walkAndPushDefinition, inputs, "").solution;

    // A rule change as the replay sees it: the rock no longer moves.
    Level::Definition walled = walkAndPushDefinition;
    walled.layers[1][2] = "  #   ";
    const Level blocked = Level::loadFromDefinition(walled, "blocked");
    const solution::ReplayReport report = solution::replay(blocked, recorded);
    CHECK(!report.passed);
    CHECK_MESSAGE(
        report.message.find("step 3 of 7 (down)") != std::string::npos,
        report.message.c_str());
    CHECK_MESSAGE(
        report.message.find("should be at") != std::string::npos,
        report.message.c_str());

    // Something moving that the recording did not expect is named too.
    solution::Solution silent = recorded;
    silent.steps[0].changes.clear();
    const solution::ReplayReport unexpected = solution::replay(level, silent);
    CHECK(!unexpected.passed);
    CHECK_MESSAGE(
        unexpected.message.find("unexpectedly changed") != std::string::npos,
        unexpected.message.c_str());
}

void writeScreen(
    const std::filesystem::path& path,
    const Level::Definition& definition)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    for (const std::string& line : Level::serializeDefinition(definition)) {
        stream << line << '\n';
    }
}

std::size_t stepsIn(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream text;
    text << stream.rdbuf();
    return solution::parse(text.str()).steps.size();
}

bool hasKind(
    const std::vector<solution::StoreChange>& changes,
    solution::StoreChange::Kind kind)
{
    return std::ranges::any_of(
        changes, [kind](const auto& change) { return change.kind == kind; });
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
        Input::Right, Input::Right, Input::Down,  Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    std::vector<Input> longer { Input::Left };
    longer.insert(longer.end(), shortest.begin(), shortest.end());

    // A longer solve is saved when nothing is stored yet...
    auto changes = solution::reconcileStore(
        levels,
        solutions,
        solution::SolveToStore {
            "screen0.scr", walkAndPushDefinition, longer });
    CHECK(hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == longer.size());
    // ...replaced by a shorter one, which is then kept over a longer one.
    changes = solution::reconcileStore(
        levels,
        solutions,
        solution::SolveToStore {
            "screen0.scr", walkAndPushDefinition, shortest });
    CHECK(hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == shortest.size());
    changes = solution::reconcileStore(
        levels,
        solutions,
        solution::SolveToStore {
            "screen0.scr", walkAndPushDefinition, longer });
    CHECK(hasKind(changes, Kind::KeptExisting));
    CHECK(!hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == shortest.size());

    // A solve that does not replay is reported and stores nothing.
    changes = solution::reconcileStore(
        levels,
        solutions,
        solution::SolveToStore {
            "screen0.scr", walkAndPushDefinition, { Input::Right } });
    CHECK(hasKind(changes, Kind::NotRecorded));

    // A draft that is not on disk waits in drafts/...
    Level::Definition draft = walkAndPushDefinition;
    draft.layers[1][0] = "C     ";
    draft.layers[1][2] = "     E";
    const std::vector<Input> draftInputs {
        Input::Down,  Input::Down,  Input::Right, Input::Right,
        Input::Right, Input::Right, Input::Right,
    };
    changes = solution::reconcileStore(
        levels,
        solutions,
        solution::SolveToStore { "screen0.scr (draft)", draft, draftInputs });
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

void testCoverageRequiresRecordingsAndReviewedDrafts()
{
    TEST("coverageRequiresRecordingsAndReviewedDrafts");
    ScopedTestDirectory directory("solution-coverage");
    const auto levels = directory.path() / "levels";
    const auto recordings = directory.path() / "solutions";
    const auto screen = levels / "level2" / "screen4.scr";
    writeScreen(screen, walkAndPushDefinition);
    std::filesystem::create_directories(recordings);
    auto report = solution::auditCoverage(levels, recordings);
    CHECK(!report.passed());
    CHECK(report.screens == 1); // A numbering gap cannot hide content.
    CHECK(
        report.errors[0].find("level2/screen4: missing current recording") !=
        std::string::npos);
    const Level level =
        Level::loadFromDefinition(walkAndPushDefinition, "coverage");
    const std::vector<Input> inputs {
        Input::Right, Input::Right, Input::Down,  Input::Up,
        Input::Right, Input::Right, Input::Right,
    };
    const auto recorded =
        solution::record(level, walkAndPushDefinition, inputs, "").solution;
    const auto file = recordings / "level2-screen4.solution";
    std::ofstream(file, std::ios::binary) << solution::serialize(recorded);
    report = solution::auditCoverage(levels, recordings);
    CHECK(report.passed());
    CHECK(report.replayed == 1);
    auto broken = recorded;
    broken.steps[0].state.players[0].quarterTurns = 1;
    std::ofstream(file, std::ios::binary) << solution::serialize(broken);
    CHECK(!solution::auditCoverage(levels, recordings).passed());
    std::ofstream(file, std::ios::binary) << solution::serialize(recorded);
    auto edited = walkAndPushDefinition;
    edited.layers[1][1] = " R    ";
    writeScreen(screen, edited);
    report = solution::auditCoverage(levels, recordings);
    CHECK(!report.passed());
    CHECK(
        report.notes.size() == 1); // A stale recording cannot satisfy coverage.
    const auto exemptionFile = recordings / "coverage.json";
    nlohmann::json exemptions = {
        { "format", 1 },
        { "drafts",
          nlohmann::json::array(
              { {
                  { "level", 2 },
                  { "screen", 4 },
                  { "level-digest",
                    solution::digestText(solution::levelDigest(edited)) },
                  { "reason", "Unfinished mechanic fixture" },
              } }) },
    };
    const auto writeExemptions = [&] {
        std::ofstream(exemptionFile, std::ios::binary) << exemptions.dump(2);
    };
    writeExemptions();
    report = solution::auditCoverage(levels, recordings);
    CHECK(report.passed());
    CHECK(report.drafts == 1);
    exemptions["drafts"][0]["reason"] = "  ";
    writeExemptions();
    CHECK(!solution::auditCoverage(levels, recordings).passed());
    exemptions["drafts"][0]["reason"] = "Unfinished mechanic fixture";
    writeExemptions();
    writeScreen(screen, walkAndPushDefinition);
    report = solution::auditCoverage(levels, recordings);
    CHECK(!report.passed());
    CHECK(report.errors[0].find("draft changed") != std::string::npos);
    std::filesystem::remove(screen);
    writeScreen(levels / "level4" / "screen9.scr", walkAndPushDefinition);
    report = solution::auditCoverage(levels, recordings);
    CHECK(!report.passed());
    CHECK(report.screens == 1);
    CHECK(report.errors[0].find("missing screen") != std::string::npos);
    std::filesystem::remove(exemptionFile);
    std::ofstream(file, std::ios::binary)
        << "format 1\nlevel-digest 0000000000000000\n";
    CHECK(!solution::auditCoverage(levels, recordings).passed());
}

// All finished puzzles must have a matching, passing recording. Only reviewed,
// digest-pinned drafts in solutions/coverage.json may remain unrecorded.
void testRecordedSolutionsStillSolveTheirScreens()
{
    TEST("recordedSolutionsStillSolveTheirScreens");
    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path root = SOKOBAN_TEST_SOURCE_DIR;
    const auto report =
        solution::auditCoverage(root / "levels", root / "solutions");
    for (const auto& error : report.errors) {
        CHECK_MESSAGE(false, error.c_str());
    }
    for (const auto& note : report.notes) {
        std::cout << "  note: " << note << '\n';
    }
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "  replayed " << report.replayed << " of " << report.screens
              << " screens, " << report.drafts << " reviewed drafts in "
              << seconds << " s\n";
}
} // namespace

int main()
{
    try {
        testRecordSerializeParse();
        testDigestTracksGameplayContentOnly();
        testCompleteStateRoundTripAndReplay();
        testReplayFailuresNameTheStepAndEntity();
        testStoreKeepsOneShortestRecordingPerScreen();
        testCoverageRequiresRecordingsAndReviewedDrafts();
        testRecordedSolutionsStillSolveTheirScreens();
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
