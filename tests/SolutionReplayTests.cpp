// Recorded solutions: file format, recording, readable replay failures, and a
// replay of every recorded solution against the current rules and levels.

#include "ScopedTestDirectory.hpp"
#include "TestHarness.hpp"

#include "engine/Level.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/Solution.hpp"
#include "engine/SolutionStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
    return std::ranges::any_of(changes, [kind](const auto& change) {
        return change.kind == kind;
    });
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
    std::vector<Input> longer { Input::Left };
    longer.insert(longer.end(), shortest.begin(), shortest.end());

    // A longer solve is saved when nothing is stored yet...
    auto changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition, longer });
    CHECK(hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == longer.size());
    // ...replaced by a shorter one, which is then kept over a longer one.
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition, shortest });
    CHECK(hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == shortest.size());
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition, longer });
    CHECK(hasKind(changes, Kind::KeptExisting));
    CHECK(!hasKind(changes, Kind::Saved));
    CHECK(stepsIn(screenFile) == shortest.size());

    // A solve that does not replay is reported and stores nothing.
    changes = solution::reconcileStore(levels, solutions,
        solution::SolveToStore { "screen0.scr", walkAndPushDefinition,
            { Input::Right } });
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

// Every puzzle screen with a recorded solution must still be solved by it.
// Screens without one, and solutions whose screen has changed, are listed but
// do not fail: recording is a deliberate step (see README.md > Solutions).
void testRecordedSolutionsStillSolveTheirScreens()
{
    TEST("recordedSolutionsStillSolveTheirScreens");
    const auto started = std::chrono::steady_clock::now();
    const std::filesystem::path root = SOKOBAN_TEST_SOURCE_DIR;
    std::map<std::uint64_t, std::pair<std::filesystem::path, solution::Solution>>
        solutions;
    const std::filesystem::path solutionsDir = root / "solutions";
    if (std::filesystem::is_directory(solutionsDir)) {
        for (const auto& entry :
             std::filesystem::directory_iterator(solutionsDir)) {
            if (entry.path().extension() != ".solution") {
                continue;
            }
            std::ifstream stream(entry.path(), std::ios::binary);
            std::stringstream text;
            text << stream.rdbuf();
            try {
                solution::Solution parsed = solution::parse(text.str());
                solutions.emplace(
                    parsed.levelDigest,
                    std::pair { entry.path(), std::move(parsed) });
            } catch (const std::exception& error) {
                CHECK_MESSAGE(false,
                    (entry.path().filename().string() + ": " + error.what())
                        .c_str());
            }
        }
    }

    int replayed = 0;
    std::vector<std::string> missing;
    std::map<std::uint64_t, bool> used;
    for (int levelIndex = 0;; ++levelIndex) {
        const std::filesystem::path directory =
            levelDirectoryPath(root / "levels", levelIndex);
        if (!std::filesystem::is_directory(directory)) {
            break;
        }
        for (int screenIndex = 0;; ++screenIndex) {
            const std::filesystem::path path =
                screenFilePath(directory, screenIndex);
            if (!std::filesystem::is_regular_file(path)) {
                break;
            }
            const std::string name = "level" + std::to_string(levelIndex) +
                "/screen" + std::to_string(screenIndex);
            const Level::Definition definition =
                Level::loadDefinitionFromFile(path);
            const auto found =
                solutions.find(solution::levelDigest(definition));
            if (found == solutions.end()) {
                missing.push_back(name);
                continue;
            }
            used[found->first] = true;
            const Level level = Level::loadFromDefinition(definition, name);
            const solution::ReplayReport report =
                solution::replay(level, found->second.second);
            CHECK_MESSAGE(report.passed,
                (name + " (" + found->second.first.filename().string() +
                    "): " + report.message).c_str());
            ++replayed;
        }
    }
    for (const auto& [digest, entry] : solutions) {
        if (!used.contains(digest)) {
            std::cout << "  note: " << entry.first.filename().string()
                      << " matches no current screen (the screen changed; "
                         "re-record it)\n";
        }
    }
    for (const std::string& name : missing) {
        std::cout << "  note: " << name << " has no recorded solution\n";
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "  replayed " << replayed << " recorded solutions in "
              << seconds << " s\n";
}

} // namespace

int main()
{
    try {
        testRecordSerializeParse();
        testDigestTracksGameplayContentOnly();
        testReplayFailuresNameTheStepAndEntity();
        testStoreKeepsOneShortestRecordingPerScreen();
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
