#include "engine/AsyncSaveStore.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SaveStore.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

template <typename Fn>
void checkThrows(Fn&& fn, const char* label)
{
    try {
        fn();
        check(false, label);
    } catch (const std::exception&) {
    }
}

void writeFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
}

const sokoban::KeyboardBinding* keyboardBinding(
    const sokoban::InputBindings& bindings,
    sokoban::InputAction action)
{
    for (const sokoban::InputBinding& binding : bindings.forAction(action)) {
        if (const auto* keyboard = std::get_if<sokoban::KeyboardBinding>(&binding)) {
            return keyboard;
        }
    }
    return nullptr;
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("sokoban-profile-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void testRoundTripAndBests()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 3;
    profile.setCurrentLevel(2);
    profile.audio = { .masterVolume = 0.8f, .musicVolume = 0.4f, .soundVolume = 0.6f };
    profile.video = { .fullscreen = true, .vsync = true };
    profile.input.forAction(sokoban::InputAction::MoveUp) = {
        sokoban::KeyboardBinding { "Up" },
        sokoban::GamepadButtonBinding { "dpup" },
        sokoban::GamepadAxisBinding {
            "lefty", sokoban::AxisDirection::Negative, 0.6f },
    };
    profile.input.forAction(sokoban::InputAction::Undo) = {
        sokoban::KeyboardBinding { "Backspace" },
        sokoban::GamepadButtonBinding { "south" },
    };
    profile.accessibility.reducedMotion = true;
    profile.accessibility.highContrast = true;
    profile.recordLevelCompletion(0, 30, 48.5, true);
    profile.recordLevelCompletion(0, 35, 40.0, true);

    const sokoban::PlayerProfile::LevelProgress* progress = profile.progressForLevel(0);
    check(progress != nullptr && progress->completed, "completion status recorded");
    check(progress != nullptr && progress->bestMoves == 30, "worse move count ignored");
    check(progress != nullptr && progress->bestTimeSeconds == 40.0, "better time recorded independently");

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    check(decoded.sourceFormat == sokoban::currentPlayerProfileFormat, "current format decoded");
    check(decoded.profile == profile, "current profile round-trips");
}

void testActiveScreenCheckpointRoundTrip()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 2;
    profile.setCurrentScreen(2, 3);

    sokoban::GameState before;
    before.player = { 1, 0, 1 };
    before.movables.push_back({
        .type = sokoban::TileType::Rock,
        .cell = { 2, 0, 1 },
    });
    sokoban::GameState after = before;
    after.player = { 2, 0, 1 };
    after.playerSliding = sokoban::MoveDirection::Right;
    after.movables.front().cell = { 3, 0, 1 };
    after.movables.front().sliding = sokoban::MoveDirection::Right;

    sokoban::GameplaySession::Action move {
        .before = before,
        .after = after,
        .playerPushing = true,
        .playerMoveCountBefore = 0,
        .playerMoveCountAfter = 1,
    };
    profile.activeScreen = sokoban::PlayerProfile::ActiveScreen {
        .level = 2,
        .screen = 3,
        .completedLevelMoveCount = 17,
        .levelElapsedSeconds = 42.25,
        .session = {
            .state = after,
            .undoStack = { move },
            .playerMoveCount = 1,
            .automaticMotionPaused = true,
        },
    };

    const std::string serialized = profile.serialize();
    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(serialized);
    check(decoded.profile == profile, "active screen checkpoint round-trips exactly");
    check(decoded.profile.currentScreen == 3, "current screen round-trips");
    check(decoded.profile.activeScreen->session.undoStack.size() == 1,
        "undo stack round-trips");
    check(decoded.profile.activeScreen->session.state == after,
        "exact committed game state round-trips");

    std::string mismatched = serialized;
    const std::string screen = "\"screen\": 3";
    mismatched.replace(mismatched.find(screen), screen.size(), "\"screen\": 1");
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(mismatched);
    }, "checkpoint for a different screen is rejected");
}

void testNormalizationAndMigration()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 2;
    profile.currentLevel = 9;
    profile.audio = { .masterVolume = -1.0f, .musicVolume = 3.0f, .soundVolume = 0.5f };
    profile.normalize();
    check(profile.currentLevel == 2, "current level clamps to unlocked level");
    check(profile.audio.masterVolume == 0.0f, "master volume clamps low");
    check(profile.audio.musicVolume == 1.0f, "music volume clamps high");

    constexpr std::string_view format1 = R"json({
  "format": 1,
  "unlockedLevel": 3,
  "currentLevel": 2,
  "completedLevels": [0, 1],
  "masterVolume": 0.7,
  "musicVolume": 0.4
})json";
    const sokoban::DecodedPlayerProfile migrated = sokoban::decodePlayerProfile(format1);
    check(migrated.sourceFormat == 1, "format 1 source reported");
    check(migrated.profile.currentLevel == 2, "format 1 current level migrated");
    check(migrated.profile.progressForLevel(0) != nullptr, "format 1 completion migrated");
    check(migrated.profile.audio.soundVolume == 1.0f, "new setting receives migration default");
    check(sokoban::decodePlayerProfile(migrated.profile.serialize()).sourceFormat ==
            sokoban::currentPlayerProfileFormat,
        "migrated profile serializes as current format");

    nlohmann::json legacyInput = {
        { "moveUp", "Up" },
        { "moveDown", "Down" },
        { "moveLeft", "Left" },
        { "moveRight", "Right" },
        { "undo", "Backspace" },
        { "restart", "R" },
    };
    nlohmann::json format2Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format2Root["format"] = 2;
    format2Root["progress"].erase("currentScreen");
    format2Root["progress"].erase("activeScreen");
    format2Root["settings"]["input"] = legacyInput;
    const sokoban::DecodedPlayerProfile migratedFormat2 =
        sokoban::decodePlayerProfile(format2Root.dump());
    check(migratedFormat2.sourceFormat == 2, "format 2 source reported");
    check(migratedFormat2.profile.currentScreen == 0,
        "format 2 receives default screen");
    check(!migratedFormat2.profile.activeScreen,
        "format 2 receives no gameplay checkpoint");
    const sokoban::KeyboardBinding* migratedKeyboard = keyboardBinding(
        migratedFormat2.profile.input, sokoban::InputAction::MoveUp);
    check(migratedKeyboard && migratedKeyboard->scancode == "Up",
        "format 2 keyboard binding migrates");
    check(migratedFormat2.profile.input.forAction(
            sokoban::InputAction::MoveUp).size() == 3,
        "format 2 migration adds controller defaults");

    nlohmann::json format3Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format3Root["format"] = 3;
    format3Root["settings"]["input"] = legacyInput;
    const sokoban::DecodedPlayerProfile migratedFormat3 =
        sokoban::decodePlayerProfile(format3Root.dump());
    check(migratedFormat3.sourceFormat == 3, "format 3 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat3.profile.input, sokoban::InputAction::Undo);
    check(migratedKeyboard && migratedKeyboard->scancode == "Backspace",
        "format 3 keyboard binding migrates");

    checkThrows([] {
        (void)sokoban::decodePlayerProfile(R"json({ "format": 99 })json");
    }, "unsupported profile format rejected");

    std::string duplicateLevels = sokoban::PlayerProfile {}.serialize();
    const std::string emptyLevels = "\"levels\": []";
    const std::string duplicateEntries =
        "\"levels\": [{\"level\":0,\"completed\":false},"
        "{\"level\":0,\"completed\":false}]";
    duplicateLevels.replace(
        duplicateLevels.find(emptyLevels),
        emptyLevels.size(),
        duplicateEntries);
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(duplicateLevels);
    }, "duplicate level progress rejected");

    std::string incompleteBest = sokoban::PlayerProfile {}.serialize();
    const std::string incompleteEntry =
        "\"levels\": [{\"level\":0,\"completed\":false,\"bestMoves\":2}]";
    incompleteBest.replace(
        incompleteBest.find(emptyLevels),
        emptyLevels.size(),
        incompleteEntry);
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(incompleteBest);
    }, "incomplete level best rejected");

    nlohmann::json invalidBindings = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    invalidBindings["settings"]["input"]["moveUp"] = nlohmann::json::array();
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalidBindings.dump());
    }, "actions without bindings are rejected");

    invalidBindings = nlohmann::json::parse(sokoban::PlayerProfile {}.serialize());
    invalidBindings["settings"]["input"]["undo"].push_back(
        invalidBindings["settings"]["input"]["undo"].front());
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalidBindings.dump());
    }, "duplicate action bindings are rejected");

    invalidBindings = nlohmann::json::parse(sokoban::PlayerProfile {}.serialize());
    invalidBindings["settings"]["input"]["moveLeft"][2]["threshold"] = 0.01;
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalidBindings.dump());
    }, "invalid gamepad axis threshold is rejected");
}

void testStoreBackupsAndRecovery()
{
    TemporaryDirectory temporary;
    sokoban::SaveStore store(temporary.path());
    sokoban::SaveStore::LoadResult created = store.load();
    check(created.disposition == sokoban::SaveStore::LoadDisposition::CreatedDefault,
        "missing profile creates defaults");
    check(std::filesystem::is_regular_file(store.primaryPath()), "default primary written");

    sokoban::PlayerProfile first = created.profile;
    first.unlockedLevel = 1;
    first.setCurrentLevel(1);
    first.audio.musicVolume = 0.25f;
    check(store.save(first), "first profile saves");

    sokoban::PlayerProfile second = first;
    second.audio.musicVolume = 0.75f;
    check(store.save(second), "second profile saves");
    check(std::filesystem::is_regular_file(store.backupPath()), "backup written");
    check(sokoban::decodePlayerProfile(
        [&] {
            std::ifstream stream(store.backupPath(), std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }()).profile.audio.musicVolume == 0.25f,
        "backup contains prior valid profile");

    writeFile(store.primaryPath(), "{ definitely not json");
    const sokoban::SaveStore::LoadResult recovered = store.load();
    check(recovered.disposition == sokoban::SaveStore::LoadDisposition::RecoveredBackup,
        "corrupt primary recovers backup");
    check(recovered.profile.audio.musicVolume == 0.25f, "recovered backup data returned");
    check(!std::filesystem::exists(store.primaryPath().string() + ".tmp"),
        "recovery leaves no temporary primary");

    bool foundCorruptArchive = false;
    for (const auto& entry : std::filesystem::directory_iterator(temporary.path())) {
        foundCorruptArchive = foundCorruptArchive ||
            entry.path().filename().string().starts_with("profile.json.corrupt-");
    }
    check(foundCorruptArchive, "corrupt primary archived for diagnostics");
}

void testMigrationAndDoubleCorruption()
{
    TemporaryDirectory migrationDirectory;
    sokoban::SaveStore migrationStore(migrationDirectory.path());
    writeFile(migrationStore.primaryPath(), R"json({
  "format": 1,
  "unlockedLevel": 1,
  "currentLevel": 1,
  "completedLevels": [0],
  "masterVolume": 0.5,
  "musicVolume": 0.25,
  "soundVolume": 0.75
})json");
    const sokoban::SaveStore::LoadResult migrated = migrationStore.load();
    check(migrated.disposition == sokoban::SaveStore::LoadDisposition::Migrated,
        "store migrates old primary");
    check(sokoban::decodePlayerProfile(
        [&] {
            std::ifstream stream(migrationStore.primaryPath(), std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }()).sourceFormat == sokoban::currentPlayerProfileFormat,
        "migration rewrites current format");

    TemporaryDirectory corruptDirectory;
    sokoban::SaveStore corruptStore(corruptDirectory.path());
    writeFile(corruptStore.primaryPath(), "bad primary");
    writeFile(corruptStore.backupPath(), "bad backup");
    const sokoban::SaveStore::LoadResult reset = corruptStore.load();
    check(reset.disposition == sokoban::SaveStore::LoadDisposition::ResetCorrupt,
        "double corruption resets defaults");
    check(reset.profile == sokoban::PlayerProfile {}, "double corruption returns defaults");
    check(sokoban::decodePlayerProfile(
        [&] {
            std::ifstream stream(corruptStore.primaryPath(), std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }()).sourceFormat == sokoban::currentPlayerProfileFormat,
        "double corruption writes valid replacement");
}

void testAsyncSaveCoalescingAndFlush()
{
    TemporaryDirectory temporary;
    sokoban::AsyncSaveStore store(temporary.path(), std::chrono::seconds(5));
    sokoban::SaveStore::LoadResult created = store.load();

    sokoban::PlayerProfile first = created.profile;
    first.audio.musicVolume = 0.25f;
    sokoban::PlayerProfile latest = first;
    latest.audio.musicVolume = 0.75f;

    store.requestSave(first);
    store.requestSave(latest);
    const sokoban::AsyncSaveStore::Diagnostics queued = store.diagnostics();
    check(queued.requests == 2, "async save requests counted");
    check(queued.pending, "deferred save remains off the calling thread");
    check(queued.coalescedRequests == 1, "pending saves coalesce");

    store.flush();
    const sokoban::AsyncSaveStore::Diagnostics flushed = store.diagnostics();
    check(flushed.completedWrites == 1, "coalesced profiles produce one write");
    check(!flushed.pending && !flushed.writing, "flush drains background writer");
    check(flushed.lastWriteSucceeded, "background save succeeds");

    std::ifstream stream(store.primaryPath(), std::ios::binary);
    const std::string contents {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char> {}
    };
    check(sokoban::decodePlayerProfile(contents).profile.audio.musicVolume == 0.75f,
        "coalesced save writes newest profile");

    latest.audio.musicVolume = 0.5f;
    store.requestSave(latest, sokoban::AsyncSaveStore::Urgency::Immediate);
    store.flush();
    check(store.diagnostics().completedWrites == 2,
        "immediate request is written by background worker");
}

void testAsyncSaveDestructorFlushesNewestProfile()
{
    TemporaryDirectory temporary;
    {
        sokoban::AsyncSaveStore store(temporary.path(), std::chrono::hours(1));
        sokoban::PlayerProfile profile = store.load().profile;
        profile.audio.soundVolume = 0.35f;
        store.requestSave(profile);
    }

    std::ifstream stream(temporary.path() / "profile.json", std::ios::binary);
    const std::string contents {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char> {}
    };
    check(sokoban::decodePlayerProfile(contents).profile.audio.soundVolume == 0.35f,
        "async store destructor flushes newest profile");
}

} // namespace

int main()
{
    testRoundTripAndBests();
    testActiveScreenCheckpointRoundTrip();
    testNormalizationAndMigration();
    testStoreBackupsAndRecovery();
    testMigrationAndDoubleCorruption();
    testAsyncSaveCoalescingAndFlush();
    testAsyncSaveDestructorFlushesNewestProfile();

    if (failures != 0) {
        std::cerr << failures << " player profile checks failed\n";
        return 1;
    }
    std::cout << "All player profile checks passed\n";
    return 0;
}
