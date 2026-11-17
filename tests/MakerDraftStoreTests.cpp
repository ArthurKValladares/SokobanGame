#include "TestHarness.hpp"

#include "engine/AtomicFile.hpp"
#include "engine/MakerDraftStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <latch>
#include <utility>

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

void testLevelIdSurvivesRenameAndOverwrite()
{
    TEST("levelIdSurvivesRenameAndOverwrite");
    TemporaryDirectory directory;
    const auto rock = draftDefinition(TileType::Rock);
    const auto wall = draftDefinition(TileType::Wall);
    const MakerDraftStore store(directory.root);
    const auto first = store.save("Original", rock);
    CHECK(!first.id.empty() && !first.legacyName);
    CHECK(store.save("Original", wall, true).id == first.id);
    CHECK(store.draftVersionById(first.id).has_value());
    CHECK(store.loadById(first.id) == wall);

    const auto other = store.save("Occupied", rock);
    checkThrows([&] { (void)store.renameLevel(first.id, other.name); }, "rename to occupied name");
    CHECK(store.findLevelById(other.id) == other);
    CHECK(store.findLevelById(first.id) == first);
    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { (void)store.renameLevel(first.id, "Renamed"); }, "interrupted rename");
    CHECK(store.findLevelById(first.id) == first);

    const auto renamed = store.renameLevel(first.id, "Renamed");
    const MakerDraftStore reopened(directory.root);
    CHECK(renamed.id == first.id && renamed.name == "Renamed" && renamed.legacyName == "Original");
    CHECK(reopened.findLevelById(first.id) == renamed);
    CHECK(reopened.resolveLevel(first.id, "Original") == renamed);
    CHECK(!reopened.resolveLevel("missing", "Renamed"));
    CHECK(!reopened.findLevelByName("Original"));
    CHECK(reopened.list() == (std::vector<std::string> { "Occupied", "Renamed" }));
    CHECK(reopened.saveById(first.id, rock) == renamed);
    CHECK(reopened.load("Renamed") == rock);
    const auto replacement = reopened.save("Original", wall);
    CHECK(replacement.id != first.id && reopened.findLevelByName("Original") == replacement);
    CHECK(reopened.resolveLevel(std::nullopt, "Original") == renamed);
    checkThrows([&] { (void)reopened.saveById("missing", wall); }, "unknown identity must not create a level");
}

void testLegacyLevelRenameReservesLinkAlias()
{
    TEST("legacyLevelRenameReservesLinkAlias");
    TemporaryDirectory directory;
    fs::create_directories(directory.root);
    const auto old = draftDefinition(TileType::Rock);
    const auto another = draftDefinition(TileType::Wall);
    const fs::path legacyPath = directory.root / "draft-4f6c64.draft";
    {
        std::ofstream legacy(legacyPath);
        for (const auto& line : Level::serializeDefinition(old)) legacy << line << '\n';
    }

    const MakerDraftStore store(directory.root);
    const auto legacy = store.findLevelByName("Old");
    CHECK(legacy.has_value() && legacy->legacyName == "Old");
    if (!legacy) return;
    const auto renamed = store.renameLevel(legacy->id, "New Display Name");
    CHECK(renamed.id == legacy->id && renamed.legacyName == "Old");
    CHECK(fs::is_regular_file(legacyPath));
    CHECK(store.list() == std::vector<std::string> { "New Display Name" });

    const auto reused = store.save("Old", another);
    const MakerDraftStore reopened(directory.root);
    CHECK(reused.id != legacy->id);
    CHECK(reopened.findLevelByName("Old") == reused);
    CHECK(reopened.resolveLevel(std::nullopt, "Old") == renamed);
    CHECK(reopened.loadById(legacy->id) == old);
    CHECK(reopened.load("Old") == another);
    CHECK(reopened.renameLevel(renamed.id, "Third Name").id == legacy->id);
    const auto resolved = reopened.resolveLevel(std::nullopt, "Old");
    CHECK(resolved.has_value() && resolved->id == renamed.id && resolved->name == "Third Name");
}

void testDeletedLevelsNeverResolveToReplacementDrafts()
{
    TEST("deletedLevelsNeverResolveToReplacementDrafts");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    const auto definition = draftDefinition(TileType::Wall);
    const auto saved = store.save("Original", definition);
    (void)store.renameLevel(saved.id, "Renamed");
    const auto reusedAlias = store.save("Original", definition);

    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { store.deleteLevel(saved.id); }, "interrupted delete");
    CHECK(store.loadById(saved.id) == definition);
    const auto oldAlias = store.resolveLevel(std::nullopt, "Original");
    CHECK(oldAlias && oldAlias->id == saved.id);

    store.deleteLevel(saved.id);
    const MakerDraftStore reopened(directory.root);
    const auto reusedName = reopened.save("Renamed", definition);
    CHECK(reusedName.id != saved.id);
    CHECK(reopened.findLevelByName("Renamed") == reusedName);
    CHECK(reopened.findLevelByName("Original") == reusedAlias);
    CHECK(!reopened.findLevelById(saved.id));
    CHECK(!reopened.draftVersionById(saved.id));
    CHECK(!reopened.resolveLevel(saved.id, "Renamed"));
    CHECK(!reopened.resolveLevel(std::nullopt, "Original"));
    CHECK(!reopened.resolveLevel(std::nullopt, "Renamed"));
    CHECK(reopened.resolveLevel(reusedName.id, "Renamed") == reusedName);
    checkThrows([&] { (void)reopened.loadById(saved.id); }, "load deleted identity");
    checkThrows([&] { reopened.deleteLevel(saved.id); }, "delete missing level");
    std::vector<std::string> retired;
    CHECK(reopened.listLevels(&retired).size() == 2);
    CHECK(retired == (std::vector<std::string> { "Original", "Renamed" }));
}

void testDeleteLegacyLevelKeepsItsOldFileHidden()
{
    TEST("deleteLegacyLevelKeepsItsOldFileHidden");
    TemporaryDirectory directory;
    fs::create_directories(directory.root);
    const auto definition = draftDefinition(TileType::Rock);
    const auto legacyPath = directory.root / "draft-4f6c64.draft";
    const auto writeOldDraft = [&] {
        std::ofstream file(legacyPath);
        for (const auto& line : Level::serializeDefinition(definition)) file << line << '\n';
    };
    writeOldDraft();
    const MakerDraftStore store(directory.root);
    const auto old = store.findLevelByName("Old");
    CHECK(old.has_value());
    if (!old) return;
    (void)store.renameLevel(old->id, "New");
    store.deleteLevel(old->id);
    CHECK(!fs::exists(legacyPath));

    // Emulate another process or an interrupted cleanup leaving an old file.
    writeOldDraft();
    const MakerDraftStore reopened(directory.root);
    CHECK(reopened.list().empty());
    CHECK(!reopened.resolveLevel(std::nullopt, "Old"));
    const auto replacement = reopened.save("Old", definition);
    CHECK(replacement.id != old->id);
    CHECK(reopened.findLevelByName("Old") == replacement);
    CHECK(!reopened.resolveLevel(std::nullopt, "Old"));
    CHECK(!reopened.findLevelById(old->id));
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

OverworldDraftOverride overworldDraft()
{
    auto home = draftDefinition(TileType::Player);
    home.character = CharacterType::Lorekeeper;
    home.layers[1][0][1] = tileTypeToChar(TileType::WardrobeLorekeeper);
    home.layers[1][1][0] = tileTypeToChar(TileType::LecternNorth);
    home.lecterns.push_back({ .cell = { 0, 1, 1 }, .text = "Explore the workshop." });
    home.selectors.push_back({
        .id = 4, .cell = { 1, 1, 1 }, .target = LevelLocation { 2, 1 },
    });
    return {
        .layout = OverworldLayout {
            .screenWidth = 2,
            .screenHeight = 2,
            .screens = { { 1, "screen1.scr", { 0, 0 } }, { 3, "screen3.scr", { -1, 0 } } },
        },
        .definitions = { { 1, std::move(home) }, { 3, draftDefinition(TileType::Rock) } },
    };
}

void testOverworldDraftsRoundTrip()
{
    TEST("overworldDraftsRoundTrip");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    const auto overworld = overworldDraft();
    CHECK(store.listOverworlds().empty());
    store.save("Shared name", draftDefinition());
    store.saveOverworld("Shared name", overworld);
    checkThrows([&] { store.saveOverworld("Shared name", overworld); }, "overworld name collision");

    const MakerDraftStore reopened(directory.root);
    CHECK(reopened.list() == std::vector<std::string> { "Shared name" });
    CHECK(reopened.listOverworlds() == std::vector<std::string> { "Shared name" });
    const auto loaded = reopened.loadOverworld("Shared name");
    CHECK(loaded.layout == overworld.layout);
    CHECK(loaded.definitions.size() == overworld.definitions.size());
    for (std::size_t i = 0; i < loaded.definitions.size(); ++i) {
        CHECK(loaded.definitions[i].screen == overworld.definitions[i].screen);
        CHECK(loaded.definitions[i].definition == overworld.definitions[i].definition);
    }

    auto incomplete = overworld;
    incomplete.definitions[0].definition.layers[1][0][0] = tileTypeToChar(TileType::Air);
    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { store.saveOverworld("Shared name", incomplete, true); }, "failed overworld replacement");
    CHECK(reopened.loadOverworld("Shared name").definitions[0].definition == overworld.definitions[0].definition);
    store.saveOverworld("Shared name", incomplete, true);
    CHECK(reopened.loadOverworld("Shared name").definitions[0].definition == incomplete.definitions[0].definition);
}

void testRenameAndDeleteOverworldDraft()
{
    TEST("renameAndDeleteOverworldDraft");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    auto world = overworldDraft();
    const auto puzzle = store.save("World", draftDefinition());
    auto& selector = world.definitions.front().definition.selectors.front();
    selector.target.reset();
    selector.draftId = puzzle.id;
    selector.draftName = puzzle.name;
    store.saveOverworld("World", world);
    store.saveOverworld("Taken", overworldDraft());
    checkThrows([&] { store.renameOverworld("World", "Taken"); }, "overworld rename collision");
    checkThrows([&] { store.renameOverworld("World", "../World"); }, "invalid overworld name");
    atomicFile::failReplaceAfterForTesting(0, std::errc::permission_denied);
    checkThrows([&] { store.renameOverworld("World", "Renamed"); }, "interrupted overworld rename");
    CHECK(store.listOverworlds() == (std::vector<std::string> { "Taken", "World" }));

    store.renameOverworld("World", "Renamed");
    const MakerDraftStore reopened(directory.root);
    CHECK(reopened.listOverworlds() == (std::vector<std::string> { "Renamed", "Taken" }));
    CHECK(reopened.findLevelByName("World") == puzzle);
    const auto loaded = reopened.loadOverworld("Renamed");
    CHECK(loaded.layout == world.layout);
    CHECK(loaded.definitions.size() == world.definitions.size());
    for (std::size_t index = 0; index < loaded.definitions.size(); ++index) {
        CHECK(loaded.definitions[index].screen == world.definitions[index].screen);
        CHECK(loaded.definitions[index].definition == world.definitions[index].definition);
    }

    reopened.deleteOverworld("Renamed");
    CHECK(store.listOverworlds() == std::vector<std::string> { "Taken" });
    checkThrows([&] { (void)reopened.loadOverworld("Renamed"); }, "load deleted overworld");
    checkThrows([&] { reopened.deleteOverworld("Renamed"); }, "delete missing overworld");
    CHECK(reopened.findLevelByName("World") == puzzle);
}

void testInvalidOverworldDrafts()
{
    TEST("invalidOverworldDrafts");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    const auto valid = overworldDraft();

    auto invalid = valid;
    invalid.layout->screens[1].slot = invalid.layout->screens[0].slot;
    checkThrows([&] { store.saveOverworld("Invalid", invalid); }, "duplicate slots");
    invalid = valid;
    invalid.definitions[1].definition.layers[1][0][0] = tileTypeToChar(TileType::Player);
    checkThrows([&] { store.saveOverworld("Invalid", invalid); }, "duplicate starts");
    invalid = valid;
    invalid.definitions[1].screen = 9;
    checkThrows([&] { store.saveOverworld("Invalid", invalid); }, "unmatched definitions");
    invalid = valid;
    invalid.layout->screens[1].slot = { 128, 0 };
    checkThrows([&] { store.saveOverworld("Invalid", invalid); }, "unsupported slot span");
    invalid = valid;
    invalid.definitions[1].definition.layers[1][0].push_back(tileTypeToChar(TileType::Air));
    checkThrows([&] { store.saveOverworld("Invalid", invalid); }, "incorrect screen size");
    CHECK(store.listOverworlds().empty());
    std::ofstream(directory.root / "draft-41.world") << R"({"format":1,"width":4294967297,"height":1,"screens":[]})";
    checkThrows([&] { (void)store.loadOverworld("A"); }, "integer out of range");
}

void testSavedLevelSelectorLinksRoundTrip()
{
    TEST("savedLevelSelectorLinksRoundTrip");
    TemporaryDirectory directory;
    const MakerDraftStore store(directory.root);
    auto world = overworldDraft();
    auto& first = world.definitions[0].definition.selectors.front();
    first.target.reset();
    first.draftName = "Caf\xc3\xa9 \"Puzzle\"";
    auto& otherSelectors = world.definitions[1].definition.selectors;
    otherSelectors.push_back({
        .id = first.id,
        .cell = { 1, 1, 1 },
        .draftName = "Next Puzzle",
        .prerequisites = { { 1, first.id } },
    });
    store.save(*first.draftName, draftDefinition());
    store.save(*otherSelectors[0].draftName, draftDefinition());
    store.saveOverworld("New format", world);
    const auto reopened = store.loadOverworld("New format");
    CHECK(reopened.definitions[0].definition.selectors == world.definitions[0].definition.selectors);
    CHECK(reopened.definitions[1].definition.selectors == otherSelectors);
    CHECK(store.list() == (std::vector<std::string> { *first.draftName, *otherSelectors[0].draftName }));

    otherSelectors[0].prerequisites.push_back({ 1, first.id + 1 });
    checkThrows([&] { store.saveOverworld("Dangling selector", world); }, "missing prerequisite selector");
    otherSelectors[0].prerequisites.pop_back();
    first.prerequisites.push_back({ 3, first.id });
    checkThrows([&] { store.saveOverworld("Cyclic selectors", world); }, "cyclic prerequisite selectors");
}

} // namespace

int main()
{
    testDraftsRoundTrip();
    testCollisionAndInterruptedOverwrite();
    testLevelIdSurvivesRenameAndOverwrite();
    testLegacyLevelRenameReservesLinkAlias();
    testDeletedLevelsNeverResolveToReplacementDrafts();
    testDeleteLegacyLevelKeepsItsOldFileHidden();
    testConcurrentSameNameCreatesOneDraft();
    testInvalidNamesAndForeignEntries();
    testOverworldDraftsRoundTrip();
    testRenameAndDeleteOverworldDraft();
    testInvalidOverworldDrafts();
    testSavedLevelSelectorLinksRoundTrip();
    if (failures != 0) {
        std::cerr << "MakerDraftStoreTests: " << failures << " failures of " << checks << " checks\n";
        return 1;
    }
    std::cout << "MakerDraftStoreTests: " << checks << " checks passed\n";
    return 0;
}
