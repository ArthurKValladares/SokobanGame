#include "TestHarness.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/MakerDraftStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <latch>

namespace {

using namespace sokoban;
namespace fs = std::filesystem;

struct TemporaryDirectory {
    fs::path root = fs::temp_directory_path() / ("sokoban_maker_draft_store_tests_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

Level::Definition draftDefinition(TileType tile = TileType::Air)
{
    const char ground = tileTypeToChar(TileType::Ground);
    const char air = tileTypeToChar(TileType::Air);
    return {
        .layers = {
            { std::string(2, ground), std::string(2, ground) },
            { std::string { tileTypeToChar(tile), air }, std::string(2, air) },
        },
        .cameraAngles = CameraAngles { .pitchDegrees = 36.0f, .yawDegrees = 12.0f },
    };
}

void testDraftsRoundTrip()
{
    TEST("draftsRoundTrip");
    TemporaryDirectory directory;
    const auto root = directory.root / u8"drafts-\u00e9";
    constexpr std::string_view unicodeName = "Caf\xC3\xA9";
    const auto unfinished = draftDefinition();
    checkThrows([&] { (void)Level::loadFromDefinition(unfinished, "incomplete"); },
        "draft deliberately has no hero");
    {
        MakerDraftStore store(root);
        CHECK(store.list().empty());
        CHECK(!fs::exists(root));
        store.save(unicodeName, unfinished);
        store.save("CON", draftDefinition(TileType::Wall));
        store.save("con", draftDefinition(TileType::Rock));
    }

    MakerDraftStore reopened(root);
    CHECK(reopened.list() == (std::vector<std::string> { "CON", std::string(unicodeName), "con" }));
    const auto loaded = reopened.load(unicodeName);
    CHECK(loaded.layers == unfinished.layers);
    CHECK(loaded.cameraAngles == unfinished.cameraAngles);
    CHECK(reopened.load("CON").layers == draftDefinition(TileType::Wall).layers);
    CHECK(reopened.load("con").layers == draftDefinition(TileType::Rock).layers);
}

void testCollisionAndInterruptedOverwrite()
{
    TEST("collisionAndInterruptedOverwrite");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    const auto before = draftDefinition(TileType::Rock);
    const auto after = draftDefinition(TileType::Wall);
    store.save("Puzzle", before);

    bool collision = false;
    try {
        store.save("Puzzle", after);
    } catch (const MakerDraftStore::NameExists&) {
        collision = true;
    }
    CHECK(collision);
    CHECK(store.load("Puzzle").layers == before.layers);

    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { store.save("Puzzle", after, true); }, "failed atomic replacement");
    CHECK(store.load("Puzzle").layers == before.layers);
    CHECK(store.list() == std::vector<std::string> { "Puzzle" });
    store.save("Puzzle", after, true);
    CHECK(store.load("Puzzle").layers == after.layers);
}

void testConcurrentSameNameCreatesOneDraft()
{
    TEST("concurrentSameNameCreatesOneDraft");
    TemporaryDirectory directory;
    const MakerDraftStore first(directory.root);
    const MakerDraftStore second(directory.root);
    const auto before = draftDefinition(TileType::Rock);
    const auto after = draftDefinition(TileType::Wall);
    std::latch ready(2);
    const auto save = [&](const MakerDraftStore& store, const Level::Definition& draft) {
        ready.arrive_and_wait();
        try {
            store.save("Same name", draft);
            return true;
        } catch (const MakerDraftStore::NameExists&) {
            return false;
        }
    };

    auto firstSave = std::async(std::launch::async, [&] { return save(first, before); });
    auto secondSave = std::async(std::launch::async, [&] { return save(second, after); });
    const bool firstWon = firstSave.get();
    const bool secondWon = secondSave.get();
    CHECK(firstWon != secondWon);
    CHECK(first.list() == std::vector<std::string> { "Same name" });
    CHECK(first.load("Same name").layers == (firstWon ? before.layers : after.layers));
}

void testInvalidNamesAndForeignEntries()
{
    TEST("invalidNamesAndForeignEntries");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    const auto draft = draftDefinition();
    for (const std::string_view name : { "", " ", "..", "../outside", "a\\b", "a\nb", "\xff" }) {
        checkThrows([&] { store.save(name, draft); }, "invalid draft name");
    }
    checkThrows([&] { store.save(std::string(MakerDraftStore::maxNameBytes + 1, 'a'), draft); },
        "oversized draft name");
    CHECK(!fs::exists(directory.root));
    store.save("Good", draft);
    std::ofstream(directory.root / "notes.txt") << "Not a draft";
    std::ofstream(directory.root / "draft-41.draft.tmp") << "Interrupted write";
    std::ofstream(directory.root / "draft-nothex.draft") << "Foreign file";
    fs::create_directory(directory.root / "draft-42.draft");
    CHECK(store.list() == std::vector<std::string> { "Good" });
    checkThrows([&] { (void)store.load("Absent"); }, "missing draft");
    checkThrows([&] { store.save("B", draft, true); }, "draft name occupied by directory");

    // A recognizable draft stays visible even if its contents are damaged.
    std::ofstream(directory.root / "draft-43.draft") << "@layer 9\n";
    std::ofstream(directory.root / "draft-44.draft") << '\xff';
    CHECK(store.list() == (std::vector<std::string> { "C", "D", "Good" }));
    checkThrows([&] { (void)store.load("C"); }, "damaged draft");
    checkThrows([&] { (void)store.load("D"); }, "unknown tile in a draft");
}

} // namespace

int main()
{
    testDraftsRoundTrip();
    testCollisionAndInterruptedOverwrite();
    testConcurrentSameNameCreatesOneDraft();
    testInvalidNamesAndForeignEntries();
    if (failures != 0) {
        std::cerr << "MakerDraftStoreTests: " << failures << " failures of " << checks << " checks\n";
        return 1;
    }
    std::cout << "MakerDraftStoreTests: " << checks << " checks passed\n";
    return 0;
}
