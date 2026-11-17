#include "TestHarness.hpp"

#include "engine/MakerAutosaveWriter.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

using namespace sokoban;
namespace fs = std::filesystem;

struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() / ("sokoban_maker_autosave_writer_tests_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

Level::Definition puzzle(char marker)
{
    Level::Definition definition { .layers = { { "...", "..." }, { "   ", "   " } } };
    definition.layers[1][0][0] = marker;
    return definition;
}

OverworldDraftOverride overworld()
{
    return {
        .layout = OverworldLayout {
            .screenWidth = 3,
            .screenHeight = 2,
            .screens = { { 1, "screen1.scr", { 0, 0 } } },
        },
        .definitions = { { 1, puzzle(' ') } },
    };
}

void testWritesHappenOffThreadAndReportTheirRevision()
{
    TEST("autosaveWriterSavesAndDiscardsInRequestOrder");
    TemporaryDirectory directory;
    const MakerRecoveryStore store(directory.path);
    const char wall = tileTypeToChar(TileType::Wall);
    {
        MakerAutosaveWriter writer(store);
        writer.flush(); // Nothing requested: returns without a worker.
        CHECK(writer.takeResults().empty());

        writer.save(7, { .draftName = "Puzzle", .document = puzzle(wall) });
        writer.save(3, { .activeScreen = 1, .document = overworld() });
        writer.flush();
        const auto saved = writer.takeResults();
        CHECK(saved.size() == 2);
        if (saved.size() == 2) {
            CHECK(saved[0].kind == MakerRecoveryKind::Puzzle && saved[0].revision == 7);
            CHECK(saved[1].kind == MakerRecoveryKind::Overworld && saved[1].revision == 3);
            CHECK(saved[0].journalPresent && saved[0].error.empty());
            CHECK(saved[1].journalPresent && saved[1].error.empty());
        }
        CHECK(writer.takeResults().empty());
        const auto restored = store.load(MakerRecoveryKind::Puzzle);
        CHECK(restored && std::get<Level::Definition>(restored->document) == puzzle(wall));
        CHECK(store.load(MakerRecoveryKind::Overworld).has_value());

        writer.discard(MakerRecoveryKind::Puzzle, 8);
        writer.flush();
        const auto discarded = writer.takeResults();
        CHECK(discarded.size() == 1);
        if (discarded.size() == 1) {
            CHECK(discarded[0].kind == MakerRecoveryKind::Puzzle && discarded[0].revision == 8);
            CHECK(!discarded[0].journalPresent && discarded[0].error.empty());
        }
        CHECK(!store.load(MakerRecoveryKind::Puzzle));
        CHECK(store.load(MakerRecoveryKind::Overworld).has_value());
    }
}

void testLatestRequestWinsAndShutdownCompletesIt()
{
    TEST("autosaveWriterKeepsTheNewestRequestAndFinishesItOnShutdown");
    TemporaryDirectory directory;
    const MakerRecoveryStore store(directory.path);
    const char wall = tileTypeToChar(TileType::Wall);
    const char rock = tileTypeToChar(TileType::Rock);
    constexpr uint64_t requests = 64;
    {
        MakerAutosaveWriter writer(store);
        for (uint64_t revision = 1; revision <= requests; ++revision) {
            writer.save(revision, { .draftName = "Puzzle",
                .document = puzzle(revision == requests ? rock : wall) });
        }
        writer.flush();
        const auto results = writer.takeResults();
        // Requests that were replaced before starting produce no result, and
        // the ones that ran did so in order.
        CHECK(!results.empty() && results.size() <= requests);
        CHECK(!results.empty() && results.back().revision == requests);
        for (std::size_t index = 1; index < results.size(); ++index) {
            CHECK(results[index - 1].revision < results[index].revision);
        }
        const auto restored = store.load(MakerRecoveryKind::Puzzle);
        CHECK(restored && std::get<Level::Definition>(restored->document) == puzzle(rock));

        // Left waiting for the destructor.
        writer.save(requests + 1, { .draftName = "Final", .document = puzzle(wall) });
    }
    const auto final = store.load(MakerRecoveryKind::Puzzle);
    CHECK(final && final->draftName == "Final");
    CHECK(final && std::get<Level::Definition>(final->document) == puzzle(wall));
}

void testFailuresAreReportedAndLaterWritesStillRun()
{
    TEST("autosaveWriterReportsFailuresWithoutStopping");
    TemporaryDirectory directory;
    fs::create_directories(directory.path);
    // A file where the recovery directory should be makes every write fail.
    const fs::path blocked = directory.path / "blocked";
    { std::ofstream(blocked) << "not a directory"; }
    MakerAutosaveWriter writer { MakerRecoveryStore(blocked / "recovery") };
    writer.save(1, { .draftName = "Puzzle", .document = puzzle(' ') });
    writer.flush();
    auto results = writer.takeResults();
    CHECK(results.size() == 1 && !results[0].error.empty() && results[0].revision == 1);

    // An invalid snapshot is rejected by the store on the worker as well.
    writer.save(2, { .draftName = "Puzzle", .document = Level::Definition {} });
    writer.flush();
    results = writer.takeResults();
    CHECK(results.size() == 1 && !results[0].error.empty() && results[0].revision == 2);
}

} // namespace

int main()
{
    testWritesHappenOffThreadAndReportTheirRevision();
    testLatestRequestWinsAndShutdownCompletesIt();
    testFailuresAreReportedAndLaterWritesStillRun();
    return failures == 0 ? 0 : 1;
}
