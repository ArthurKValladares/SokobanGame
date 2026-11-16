#include "TestHarness.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/MakerDraftStore.hpp"
#include "engine/MakerRecoveryStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

using namespace sokoban;
namespace fs = std::filesystem;

struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() / ("sokoban_maker_recovery_tests_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

Level::Definition unfinishedPuzzle()
{
    return {
        .layers = { { "...", "..." }, { "   ", "   " } },
        .cameraAngles = CameraAngles { .pitchDegrees = 36.0f, .yawDegrees = 12.0f },
    };
}

OverworldDraftOverride unfinishedOverworld()
{
    auto entry = unfinishedPuzzle();
    entry.selectors = { { .id = 1, .cell = { 1, 1, 1 }, .draftName = "Puzzle",
        .draftId = "0123456789abcdef0123456789abcdef" } };
    auto second = unfinishedPuzzle();
    second.selectors = { { .id = 1, .cell = { 1, 1, 1 }, .prerequisites = { { 1, 1 } } } };
    return {
        .layout = OverworldLayout {
            .screenWidth = 3,
            .screenHeight = 2,
            .screens = { { 1, "screen1.scr", { 0, 0 } }, { 2, "screen2.scr", { -1, 0 } } },
        },
        .definitions = { { 1, std::move(entry) }, { 2, std::move(second) } },
    };
}

void testRecoveryPersistsBothModesWithoutUpdatingSavedDrafts()
{
    TEST("recoveryPersistsBothModesWithoutUpdatingSavedDrafts");
    TemporaryDirectory directory;
    const MakerDraftStore saved(directory.path / "drafts");
    const auto recoveryRoot = directory.path / "recovery";
    const MakerRecoveryStore writer(recoveryRoot);
    CHECK(!writer.load(MakerRecoveryKind::Puzzle));
    writer.discard(MakerRecoveryKind::Puzzle);
    CHECK(!fs::exists(recoveryRoot));

    const auto puzzle = unfinishedPuzzle();
    saved.save("Puzzle", puzzle);
    auto edited = puzzle;
    edited.layers[1][0][0] = tileTypeToChar(TileType::Wall);
    writer.save({ .draftName = "Puzzle", .draftId = "0123456789abcdef0123456789abcdef", .document = edited });
    const auto world = unfinishedOverworld();
    writer.save({ .activeScreen = 2, .document = world });

    const MakerRecoveryStore reopened(recoveryRoot);
    const auto restoredPuzzle = reopened.load(MakerRecoveryKind::Puzzle);
    CHECK(restoredPuzzle && restoredPuzzle->draftName == "Puzzle");
    CHECK(restoredPuzzle && restoredPuzzle->draftId == "0123456789abcdef0123456789abcdef");
    CHECK(restoredPuzzle && std::get<Level::Definition>(restoredPuzzle->document) == edited);
    CHECK(saved.load("Puzzle") == puzzle);
    const auto restoredOverworld = reopened.load(MakerRecoveryKind::Overworld);
    CHECK(restoredOverworld && restoredOverworld->draftName.empty() && restoredOverworld->activeScreen == OverworldScreenId { 2 });
    if (restoredOverworld) {
        const auto& document = std::get<OverworldDraftOverride>(restoredOverworld->document);
        CHECK(document.layout == world.layout);
        CHECK(document.definitions.size() == 2);
        if (document.definitions.size() == 2) {
            CHECK(document.definitions[0].definition == world.definitions[0].definition);
            CHECK(document.definitions[1].definition == world.definitions[1].definition);
        }
    }

    reopened.discard(MakerRecoveryKind::Puzzle);
    CHECK(!writer.load(MakerRecoveryKind::Puzzle));
    CHECK(writer.load(MakerRecoveryKind::Overworld).has_value());
    CHECK(saved.list() == std::vector<std::string> { "Puzzle" });
}

void testInterruptedRecoveryWritePreservesPreviousSnapshot()
{
    TEST("interruptedRecoveryWritePreservesPreviousSnapshot");
    TemporaryDirectory directory;
    const MakerRecoveryStore store(directory.path);
    const auto puzzle = unfinishedPuzzle();
    store.save({ .document = puzzle });
    auto edited = puzzle;
    edited.layers[1][0][0] = tileTypeToChar(TileType::Wall);
    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { store.save({ .document = edited }); }, "interrupted autosave");
    const auto recovered = store.load(MakerRecoveryKind::Puzzle);
    CHECK(recovered && std::get<Level::Definition>(recovered->document) == puzzle);

    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { store.discard(MakerRecoveryKind::Puzzle); }, "interrupted discard");
    CHECK(store.load(MakerRecoveryKind::Puzzle).has_value());
    store.save({ .document = edited });
    const auto newer = store.load(MakerRecoveryKind::Puzzle);
    CHECK(newer && std::get<Level::Definition>(newer->document) == edited);
}

void testCorruptAutosaveRemainsIsolated()
{
    TEST("corruptAutosaveRemainsIsolated");
    TemporaryDirectory directory;
    const MakerRecoveryStore store(directory.path);
    store.save({ .document = unfinishedOverworld() });
    const auto corrupt = directory.path / "puzzle.autosave.json";
    std::ofstream(corrupt, std::ios::binary) << "{ incomplete json";
    const auto bytes = fs::file_size(corrupt);
    checkThrows([&] { (void)store.load(MakerRecoveryKind::Puzzle); }, "corrupt puzzle recovery");
    CHECK(fs::file_size(corrupt) == bytes);
    CHECK(store.load(MakerRecoveryKind::Overworld).has_value());
}

} // namespace

int main()
{
    testRecoveryPersistsBothModesWithoutUpdatingSavedDrafts();
    testInterruptedRecoveryWritePreservesPreviousSnapshot();
    testCorruptAutosaveRemainsIsolated();
    return failures == 0 ? 0 : 1;
}
