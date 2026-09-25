#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/AsyncSaveStore.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/CampaignSession.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SaveStore.hpp"
#include "engine/UserSettingsConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace allocationTracking {

struct AllocationHeader {
    void* base = nullptr;
    std::size_t size = 0;
};

static_assert(sizeof(AllocationHeader) % alignof(std::max_align_t) == 0);

std::atomic_uint64_t liveBytes = 0;
std::atomic_uint64_t peakBytes = 0;
std::atomic_bool measuring = false;

void recordPeak(uint64_t value)
{
    if (!measuring.load(std::memory_order_relaxed)) {
        return;
    }
    uint64_t peak = peakBytes.load(std::memory_order_relaxed);
    while (peak < value && !peakBytes.compare_exchange_weak(
               peak, value, std::memory_order_relaxed)) {
    }
}

void* allocate(std::size_t size, std::size_t alignment)
{
    const std::size_t storedSize = std::max(size, std::size_t { 1 });
    if (storedSize > std::numeric_limits<std::size_t>::max() -
            sizeof(AllocationHeader) - alignment) {
        throw std::bad_alloc();
    }
    void* base = std::malloc(
        storedSize + sizeof(AllocationHeader) + alignment - 1);
    if (base == nullptr) {
        throw std::bad_alloc();
    }
    const auto first = reinterpret_cast<std::uintptr_t>(base) +
        sizeof(AllocationHeader);
    const auto aligned = (first + alignment - 1) & ~(alignment - 1);
    auto* header = reinterpret_cast<AllocationHeader*>(aligned) - 1;
    header->base = base;
    header->size = size;
    const uint64_t live = liveBytes.fetch_add(
        size, std::memory_order_relaxed) + size;
    recordPeak(live);
    return reinterpret_cast<void*>(aligned);
}

void deallocate(void* pointer) noexcept
{
    if (pointer == nullptr) {
        return;
    }
    const auto* header =
        reinterpret_cast<const AllocationHeader*>(pointer) - 1;
    liveBytes.fetch_sub(header->size, std::memory_order_relaxed);
    std::free(header->base);
}

struct Sample {
    uint64_t elapsedMicroseconds = 0;
    uint64_t peakAdditionalBytes = 0;
    uint64_t retainedAdditionalBytes = 0;
};

template <typename Function>
Sample measure(Function&& function)
{
    const uint64_t baseline = liveBytes.load(std::memory_order_relaxed);
    peakBytes.store(baseline, std::memory_order_relaxed);
    measuring.store(true, std::memory_order_relaxed);
    const auto started = std::chrono::steady_clock::now();
    try {
        std::forward<Function>(function)();
    } catch (...) {
        measuring.store(false, std::memory_order_relaxed);
        throw;
    }
    const uint64_t elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    measuring.store(false, std::memory_order_relaxed);
    const uint64_t retained = liveBytes.load(std::memory_order_relaxed);
    const uint64_t peak = peakBytes.load(std::memory_order_relaxed);
    return {
        .elapsedMicroseconds = elapsed,
        .peakAdditionalBytes = peak > baseline ? peak - baseline : 0,
        .retainedAdditionalBytes =
            retained > baseline ? retained - baseline : 0,
    };
}

} // namespace allocationTracking

void* operator new(std::size_t size)
{
    return allocationTracking::allocate(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size)
{
    return allocationTracking::allocate(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocationTracking::allocate(
        size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocationTracking::allocate(
        size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

namespace {

void writeFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

bool hasCorruptArchive(
    const std::filesystem::path& directory,
    std::string_view filenamePrefix)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().starts_with(filenamePrefix)) {
            return true;
        }
    }
    return false;
}

using TemporaryDirectory = ScopedTestDirectory;

void testRoundTripAndBests()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 3;
    profile.setCurrentLevel(2);
    profile.settings.audio = { .masterVolume = 0.8f, .musicVolume = 0.4f, .soundVolume = 0.6f };
    profile.settings.video = {
        .fullscreen = true,
        .vsync = true,
        .allowTearing = false,
        .frameRateLimit = 120,
        .antiAliasingSamples = 4,
        .renderScalePercent = 50,
        .customRenderScale = true,
        .customRenderScalePercent = 63,
        .ambientOcclusion = false,
        .ambientOcclusionStrength = 0.35f,
        .exposureEv = 1.25f,
        .windowWidth = 1600,
        .windowHeight = 900,
    };
    profile.settings.input.forAction(sokoban::InputAction::MoveUp) = {
        sokoban::KeyboardBinding { "Up" },
        sokoban::GamepadButtonBinding { "dpup" },
        sokoban::GamepadAxisBinding {
            "lefty", sokoban::AxisDirection::Negative, 0.6f },
    };
    profile.settings.input.forAction(sokoban::InputAction::Undo) = {
        sokoban::KeyboardBinding { "Backspace" },
        sokoban::GamepadButtonBinding { "south" },
    };
    profile.recordLevelCompletion(0, 30, 48.5, true);
    profile.recordLevelCompletion(0, 35, 40.0, true);

    const sokoban::PlayerProfile::LevelProgress* progress = profile.progressForLevel(0);
    CHECK_MESSAGE(progress != nullptr && progress->completed, "completion status recorded");
    CHECK_MESSAGE(progress != nullptr && progress->bestMoves == 30, "worse move count ignored");
    CHECK_MESSAGE(progress != nullptr && progress->bestTimeSeconds == 40.0, "better time recorded independently");

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    CHECK_MESSAGE(decoded.profile == profile, "current profile round-trips");
}

void testReachedScreensAndProgressReset()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 1;
    profile.recordReachedScreen(0, 0);
    profile.recordReachedScreen(0, 2);
    profile.recordReachedScreen(0, 1);
    profile.recordReachedScreen(1, 0);
    profile.recordReachedScreen(-1, 0);
    profile.recordReachedScreen(0, -2);

    const sokoban::PlayerProfile::LevelProgress* first = profile.progressForLevel(0);
    CHECK_MESSAGE(first != nullptr && first->reachedScreens == 3, "reached screens track the max");
    CHECK_MESSAGE(first != nullptr && !first->completed, "reaching screens does not complete");
    const sokoban::PlayerProfile::LevelProgress* second = profile.progressForLevel(1);
    CHECK_MESSAGE(second != nullptr && second->reachedScreens == 1, "second level entry created");
    CHECK_MESSAGE(profile.progressForLevel(-1) == nullptr, "negative level ignored");

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    CHECK_MESSAGE(decoded.profile == profile, "reached screens round-trip");

    // Completing without recordBests keeps completion but no records.
    profile.recordLevelCompletion(1, 12, 5.0, true, false);
    const sokoban::PlayerProfile::LevelProgress* partial = profile.progressForLevel(1);
    CHECK_MESSAGE(partial != nullptr && partial->completed, "partial run still completes");
    CHECK_MESSAGE(partial != nullptr && !partial->bestMoves && !partial->bestTimeSeconds,
        "partial run records no bests");

    sokoban::PlayerProfile populated = profile;
    populated.settings.audio.musicVolume = 0.25f;
    CHECK_MESSAGE(!populated.progressEmpty(), "populated profile has progress");
    populated.resetProgress();
    CHECK_MESSAGE(populated.unlockedLevel == 0 && populated.currentLevel == 0 &&
            populated.currentScreen == 0,
        "reset clears position");
    CHECK_MESSAGE(populated.levels.empty() && !populated.activeScreen, "reset clears records");
    CHECK_MESSAGE(populated.settings.audio.musicVolume == 0.25f, "reset keeps audio settings");
    CHECK_MESSAGE(populated.progressEmpty(), "reset profile reads as empty");

    // Settings split: settingsOnly strips progress, adoptSettingsFrom keeps it.
    const sokoban::PlayerProfile settings = populated.settingsOnly();
    CHECK_MESSAGE(settings.progressEmpty(), "settingsOnly has no progress");
    CHECK_MESSAGE(settings.settings.audio.musicVolume == 0.25f, "settingsOnly keeps audio");

    sokoban::PlayerProfile target = profile;
    const int levelsBefore = static_cast<int>(target.levels.size());
    target.adoptSettingsFrom(settings);
    CHECK_MESSAGE(target.settings.audio.musicVolume == 0.25f, "adopt applies audio settings");
    CHECK_MESSAGE(static_cast<int>(target.levels.size()) == levelsBefore,
        "adopt keeps progress records");
}

void testSectionedSerialization()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 1;
    profile.recordReachedScreen(0, 1);
    profile.recordLevelCompletion(0, 12, 30.0, true);
    profile.settings.audio.musicVolume = 0.25f;
    profile.normalize();

    // Progress-only files carry no settings section and decode with default
    // settings but identical progress.
    const std::string progressOnly =
        profile.serialize(sokoban::ProfileSections::ProgressOnly);
    CHECK_MESSAGE(progressOnly.find("\"settings\"") == std::string::npos,
        "progress-only file has no settings section");
    const sokoban::PlayerProfile progressDecoded =
        sokoban::decodePlayerProfile(progressOnly).profile;
    CHECK_MESSAGE(progressDecoded.progressForLevel(0) != nullptr &&
            progressDecoded.progressForLevel(0)->bestMoves == 12,
        "progress-only round-trips progress");
    CHECK_MESSAGE(progressDecoded.settings.audio.musicVolume ==
            sokoban::PlayerProfile {}.settings.audio.musicVolume,
        "progress-only decodes default settings");

    // Settings-only files carry no progress section.
    const std::string settingsOnly =
        profile.serialize(sokoban::ProfileSections::SettingsOnly);
    CHECK_MESSAGE(settingsOnly.find("\"progress\"") == std::string::npos,
        "settings-only file has no progress section");
    const sokoban::PlayerProfile settingsDecoded =
        sokoban::decodePlayerProfile(settingsOnly).profile;
    CHECK_MESSAGE(settingsDecoded.progressEmpty(), "settings-only decodes empty progress");
    CHECK_MESSAGE(settingsDecoded.settings.audio.musicVolume == 0.25f,
        "settings-only round-trips settings");

    // A bare current-format document decodes as a fully default profile.
    const sokoban::PlayerProfile bare = sokoban::decodePlayerProfile(
        "{\"format\": " +
        std::to_string(sokoban::currentPlayerProfileFormat) + "}").profile;
    CHECK_MESSAGE(bare == sokoban::PlayerProfile {}, "sections are optional on read");
}

void testActiveScreenCheckpointRoundTrip()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 2;
    profile.setCurrentScreen(2, 3);

    sokoban::GameState before;
    before.players.push_back({ .id = 1, .cell = { 1, 0, 1 } });
    before.movables.push_back({
        .id = 2,
        .type = sokoban::TileType::TurretEast,
        .cell = { 2, 0, 1 },
    });
    before.enemies.push_back({ .id = 3, .cell = { 4, 0, 1 } });
    sokoban::GameState after = before;
    after.players[0].cell = { 2, 0, 1 };
    after.players[0].sliding = sokoban::MoveDirection::Right;
    after.players.push_back({
        .id = 4,
        .cell = { 4, 2, 1 },
        .sliding = sokoban::MoveDirection::Left,
    });
    after.movables.front().cell = { 3, 0, 1 };
    after.movables.front().dead = true;
    after.movables.front().sliding = sokoban::MoveDirection::Right;
    after.enemies.front().cell = { 5, 0, 1 };

    sokoban::GameplaySession::Action move {
        .before = before,
        .after = after,
        .playerPushing = true,
        .playerPulling = true,
        .playerMoveCountBefore = 0,
        .playerMoveCountAfter = 1,
        .presentation = {
            .durationSeconds = 1.25f,
            .motions = {
                {
                    .target = { sokoban::EntityKind::Player, 1 },
                    .from = { 1.0f, 0.0f, 1.0f },
                    .to = { 2.0f, 0.0f, 1.0f },
                    .durationSeconds = 0.15f,
                },
            },
            .animations = {
                {
                    .target = { sokoban::EntityKind::Player, 1 },
                    .initialUse = sokoban::AnimationUse::PlayerIdle,
                    .segments = {
                        {
                            .use = sokoban::AnimationUse::PlayerPush,
                            .completionUse = sokoban::AnimationUse::PlayerIdle,
                            .durationSeconds = 0.15f,
                            .clipStartSeconds = 0.4f,
                            .loops = true,
                        },
                    },
                },
                {
                    .target = { sokoban::EntityKind::Enemy, 3 },
                    .initialUse = sokoban::AnimationUse::EnemyIdle,
                    .segments = {
                        {
                            .use = sokoban::AnimationUse::EnemyAttack,
                            .completionUse = sokoban::AnimationUse::EnemyIdle,
                            .fallbackUse = sokoban::AnimationUse::EnemyIdle,
                            .durationSeconds = 1.0f,
                        },
                    },
                },
            },
        },
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
    profile.worldContext = sokoban::PlayerProfile::WorldContext::Puzzle;

    const std::string serialized = profile.serialize();
    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(serialized);
    CHECK_MESSAGE(decoded.profile == profile, "active screen checkpoint round-trips exactly");
    CHECK_MESSAGE(decoded.profile.currentScreen == 3, "current screen round-trips");
    CHECK_MESSAGE(decoded.profile.activeScreen->session.undoStack.size() == 1,
        "undo stack round-trips");
    CHECK_MESSAGE(decoded.profile.activeScreen->session.state == after,
        "exact committed game state round-trips");

    const nlohmann::json current = nlohmann::json::parse(serialized);
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["state"]
            .contains("players"),
        "checkpoint state uses the players array");
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["state"]
            .contains("enemies"),
        "checkpoint state persists enemies");
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["state"]
            ["enemies"][0].contains("dead"),
        "checkpoint state persists enemy death");
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["state"]
            ["movables"][0]["dead"].get<bool>(),
        "checkpoint state persists turret death");
    CHECK_MESSAGE(!current["progress"]["activeScreen"]["session"]["state"]
            .contains("playerClones"),
        "checkpoint state has no primary/clone compatibility fields");
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["undoStack"][0]
            ["presentation"]["animations"].size() == 2,
        "undo presentation timeline is persisted");
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["undoStack"][0]
            ["playerPulling"].get<bool>(),
        "undo action pull presentation state is persisted");
    CHECK_MESSAGE(
        current["progress"]["activeScreen"]["session"].contains(
            "undoBaseState") &&
            !current["progress"]["activeScreen"]["session"]["undoStack"][0]
                .contains("before"),
        "current undo schema stores the chain base without duplicate before states");

    nlohmann::json missingUndoBase = current;
    missingUndoBase["progress"]["activeScreen"]["session"].erase(
        "undoBaseState");
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(missingUndoBase.dump());
    }, "current undo history requires its base state");

    nlohmann::json nullUndoBase = current;
    nullUndoBase["progress"]["activeScreen"]["session"]["undoBaseState"] =
        nullptr;
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(nullUndoBase.dump());
    }, "non-empty current undo history rejects a null base state");

    nlohmann::json emptyPlayers = current;
    emptyPlayers["progress"]["activeScreen"]["session"]["state"]
        ["players"] = nlohmann::json::array();
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(emptyPlayers.dump());
    }, "checkpoint rejects an empty players array");

    nlohmann::json mismatched = nlohmann::json::parse(serialized);
    mismatched["progress"]["activeScreen"]["screen"] = 1;
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(mismatched.dump());
    }, "checkpoint for a different screen is rejected");
}

void testNormalizationAndValidation()
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 2;
    profile.currentLevel = 9;
    profile.settings.audio = { .masterVolume = -1.0f, .musicVolume = 3.0f, .soundVolume = 0.5f };
    profile.settings.video.antiAliasingSamples = 3;
    profile.settings.video.renderScalePercent = 42;
    profile.settings.video.customRenderScale = true;
    profile.settings.video.customRenderScalePercent = 10;
    profile.settings.video.exposureEv = -99.0f;
    profile.settings.video.windowWidth = 20;
    profile.settings.video.windowHeight = 30;
    profile.normalize();
    CHECK_MESSAGE(profile.currentLevel == 9,
        "current level is independent of legacy unlock progression");
    CHECK_MESSAGE(profile.settings.audio.masterVolume == 0.0f, "master volume clamps low");
    CHECK_MESSAGE(profile.settings.audio.musicVolume == 1.0f, "music volume clamps high");
    CHECK_MESSAGE(profile.settings.video.antiAliasingSamples ==
            sokoban::config::antiAliasingSamples,
        "invalid MSAA receives default");
    CHECK_MESSAGE(profile.settings.video.renderScalePercent == 100, "invalid render scale receives default");
    CHECK_MESSAGE(profile.settings.video.customRenderScalePercent == 25,
        "custom render scale clamps to its minimum");
    CHECK_MESSAGE(profile.settings.video.effectiveRenderScalePercent() == 25,
        "enabled custom render scale is effective");
    CHECK_MESSAGE(profile.settings.video.exposureEv ==
            sokoban::minimumExposureEv,
        "exposure clamps to its safe minimum");
    CHECK_MESSAGE(profile.settings.video.windowWidth == 640, "window width clamps low");
    CHECK_MESSAGE(profile.settings.video.windowHeight == 480, "window height clamps low");

    checkThrows([] {
        (void)sokoban::decodePlayerProfile(R"json({ "format": 99 })json");
    }, "unsupported profile format rejected");

    nlohmann::json duplicateLevels = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    duplicateLevels["progress"]["levels"] = nlohmann::json::array({
        {
            { "level", 0 },
            { "completed", false },
            { "reachedScreens", 0 },
        },
        {
            { "level", 0 },
            { "completed", false },
            { "reachedScreens", 0 },
        },
    });
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(duplicateLevels.dump());
    }, "duplicate level progress rejected");

    nlohmann::json incompleteBest = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    incompleteBest["progress"]["levels"] = nlohmann::json::array({
        {
            { "level", 0 },
            { "completed", false },
            { "reachedScreens", 0 },
            { "bestMoves", 2 },
        },
    });
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(incompleteBest.dump());
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

void testScreenProgressAndOverworldCheckpoint()
{
    sokoban::PlayerProfile progression;
    CHECK_MESSAGE(progression.selectorStatus({ .level = 0, .screen = 0 }) ==
            sokoban::ScreenSelectorStatus::Playable,
        "screen zero is immediately playable");
    CHECK_MESSAGE(progression.selectorStatus({ .level = 0, .screen = 1 }) ==
            sokoban::ScreenSelectorStatus::Unavailable,
        "later screen waits for its predecessor");
    CHECK_MESSAGE(progression.selectorStatus({ .level = 7, .screen = 0 }) ==
            sokoban::ScreenSelectorStatus::Playable,
        "a different level's first screen is independently playable");
    progression.recordScreenCompletion({ .level = 0, .screen = 0 }, 3, 2.0);
    CHECK_MESSAGE(progression.selectorStatus({ .level = 0, .screen = 0 }) ==
            sokoban::ScreenSelectorStatus::Solved,
        "completed screen is solved");
    CHECK_MESSAGE(progression.selectorStatus({ .level = 0, .screen = 1 }) ==
            sokoban::ScreenSelectorStatus::Playable,
        "solving a screen unlocks only its successor");
    CHECK_MESSAGE(progression.selectorStatus({ .level = 0, .screen = 2 }) ==
            sokoban::ScreenSelectorStatus::Unavailable,
        "unlocking does not skip a screen");

    sokoban::PlayerProfile profile;
    profile.recordScreenCompletion({ .level = 2, .screen = 3 }, 18, 12.5);
    profile.recordScreenCompletion({ .level = 2, .screen = 3 }, 14, 13.0);
    profile.recordScreenCompletion({ .level = 1, .screen = 0 }, 7, 4.0);
    CHECK_MESSAGE(profile.screenCompleted({ .level = 2, .screen = 3 }),
        "screen completion is queryable");
    CHECK_MESSAGE(profile.progressForScreen({ .level = 2, .screen = 3 })->bestMoves == 14,
        "screen best moves improve independently");
    CHECK_MESSAGE(profile.progressForScreen({ .level = 2, .screen = 3 })->bestTimeSeconds == 12.5,
        "screen best time does not regress");

    const sokoban::Level overworld = sokoban::Level::loadFromLayers({
        { ".." },
        { "C " },
    }, "profile overworld checkpoint");
    sokoban::GameplaySession session;
    session.reset(overworld);
    profile.overworldCheckpoint = sokoban::PlayerProfile::OverworldCheckpoint {
        .topologyFingerprint = 0x123456789abcdef0ULL,
        .activeScreen = 7,
        .session = session.snapshot(),
    };
    profile.overworldDiscovery = {
        .topologyFingerprint = 0x123456789abcdef0ULL,
        .screens = { 2, 7 },
    };
    profile.worldContext = sokoban::PlayerProfile::WorldContext::Overworld;

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    CHECK_MESSAGE(decoded.profile == profile,
        "screen progress and overworld checkpoint round-trip");
    CHECK_MESSAGE(decoded.profile.overworldDiscovery.screens ==
            std::vector<uint32_t>({ 2, 7 }),
        "overworld fog discovery round-trips");

}

void testStoreBackupsAndRecovery()
{
    TemporaryDirectory temporary;
    sokoban::SaveStore store(temporary.path());
    sokoban::SaveStore::LoadResult created = store.load();
    CHECK_MESSAGE(created.disposition == sokoban::SaveStore::LoadDisposition::CreatedDefault,
        "missing profile returns defaults");
    CHECK_MESSAGE(!std::filesystem::is_regular_file(store.primaryPath()),
        "fresh start writes no file");

    sokoban::PlayerProfile first = created.profile;
    first.unlockedLevel = 1;
    first.setCurrentLevel(1);
    first.settings.audio.musicVolume = 0.25f;
    CHECK_MESSAGE(store.save(first), "first profile saves");

    sokoban::PlayerProfile second = first;
    second.settings.audio.musicVolume = 0.75f;
    CHECK_MESSAGE(store.save(second), "second profile saves");
    CHECK_MESSAGE(std::filesystem::is_regular_file(store.backupPath()), "backup written");
    CHECK_MESSAGE(sokoban::decodePlayerProfile(
        [&] {
            std::ifstream stream(store.backupPath(), std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }()).profile.settings.audio.musicVolume == 0.25f,
        "backup contains prior valid profile");

    writeFile(store.primaryPath(), "{ definitely not json");
    const sokoban::SaveStore::LoadResult recovered = store.load();
    CHECK_MESSAGE(recovered.disposition == sokoban::SaveStore::LoadDisposition::RecoveredBackup,
        "corrupt primary recovers backup");
    CHECK_MESSAGE(recovered.profile.settings.audio.musicVolume == 0.25f, "recovered backup data returned");
    CHECK_MESSAGE(!std::filesystem::exists(store.primaryPath().string() + ".tmp"),
        "recovery leaves no temporary primary");

    bool foundCorruptArchive = false;
    for (const auto& entry : std::filesystem::directory_iterator(temporary.path())) {
        foundCorruptArchive = foundCorruptArchive ||
            entry.path().filename().string().starts_with("profile.json.corrupt-");
    }
    CHECK_MESSAGE(foundCorruptArchive, "corrupt primary archived for diagnostics");
}

void testInterruptedWriteRecovery()
{
    TemporaryDirectory temporary;
    sokoban::SaveStore store(temporary.path());

    sokoban::PlayerProfile interrupted;
    interrupted.unlockedLevel = 3;
    interrupted.setCurrentLevel(3);
    writeFile(store.primaryPath().string() + ".tmp", interrupted.serialize());

    const sokoban::SaveStore::LoadResult temporaryRecovered = store.load();
    CHECK_MESSAGE(temporaryRecovered.disposition ==
            sokoban::SaveStore::LoadDisposition::RecoveredInterruptedWrite,
        "valid temporary profile is promoted at startup");
    CHECK_MESSAGE(temporaryRecovered.profile == interrupted,
        "temporary recovery returns its saved profile");
    CHECK_MESSAGE(!std::filesystem::exists(store.primaryPath().string() + ".tmp"),
        "temporary recovery removes consumed artifact");

    TemporaryDirectory displacedDirectory;
    sokoban::SaveStore displacedStore(displacedDirectory.path());
    sokoban::PlayerProfile displaced;
    displaced.unlockedLevel = 2;
    displaced.setCurrentLevel(2);
    writeFile(
        displacedStore.primaryPath().string() + ".replace-old",
        displaced.serialize());

    const sokoban::SaveStore::LoadResult displacedRecovered = displacedStore.load();
    CHECK_MESSAGE(displacedRecovered.disposition ==
            sokoban::SaveStore::LoadDisposition::RecoveredInterruptedWrite,
        "displaced profile is restored when the live file is absent");
    CHECK_MESSAGE(displacedRecovered.profile == displaced,
        "displaced recovery returns its saved profile");
    CHECK_MESSAGE(!std::filesystem::exists(
            displacedStore.primaryPath().string() + ".replace-old"),
        "displaced recovery removes consumed artifact");

    TemporaryDirectory fallbackDirectory;
    sokoban::SaveStore fallbackStore(fallbackDirectory.path());
    sokoban::PlayerProfile fallback;
    fallback.unlockedLevel = 1;
    fallback.setCurrentLevel(1);
    writeFile(fallbackStore.primaryPath().string() + ".tmp", "truncated");
    writeFile(
        fallbackStore.primaryPath().string() + ".replace-old",
        fallback.serialize());

    const sokoban::SaveStore::LoadResult fallbackRecovered = fallbackStore.load();
    CHECK_MESSAGE(fallbackRecovered.disposition ==
            sokoban::SaveStore::LoadDisposition::RecoveredInterruptedWrite,
        "valid displaced profile is used when the temporary file is corrupt");
    CHECK_MESSAGE(fallbackRecovered.profile == fallback,
        "displaced fallback returns its saved profile");
    CHECK_MESSAGE(!std::filesystem::exists(fallbackStore.primaryPath().string() + ".tmp") &&
            !std::filesystem::exists(
                fallbackStore.primaryPath().string() + ".replace-old"),
        "fallback recovery cleans both artifacts");

    TemporaryDirectory backupDirectory;
    sokoban::SaveStore backupStore(backupDirectory.path());
    sokoban::PlayerProfile backup;
    backup.unlockedLevel = 6;
    backup.setCurrentLevel(6);
    writeFile(backupStore.backupPath().string() + ".tmp", backup.serialize());

    const sokoban::SaveStore::LoadResult backupRecovered = backupStore.load();
    CHECK_MESSAGE(backupRecovered.disposition == sokoban::SaveStore::LoadDisposition::RecoveredBackup,
        "interrupted backup write remains available for normal backup recovery");
    CHECK_MESSAGE(backupRecovered.profile == backup,
        "recovered backup temporary returns its saved profile");
    CHECK_MESSAGE(!std::filesystem::exists(backupStore.backupPath().string() + ".tmp") &&
            std::filesystem::is_regular_file(backupStore.primaryPath()),
        "backup recovery removes its artifact and repairs the primary");

    TemporaryDirectory liveDirectory;
    sokoban::SaveStore liveStore(liveDirectory.path());
    sokoban::PlayerProfile live;
    live.unlockedLevel = 4;
    live.setCurrentLevel(4);
    CHECK_MESSAGE(liveStore.save(live), "live profile saves before stale-artifact recovery");
    sokoban::PlayerProfile stale = live;
    stale.unlockedLevel = 5;
    stale.setCurrentLevel(5);
    writeFile(liveStore.primaryPath().string() + ".tmp", stale.serialize());
    writeFile(
        liveStore.primaryPath().string() + ".replace-old",
        stale.serialize());

    const sokoban::SaveStore::LoadResult liveLoaded = liveStore.load();
    CHECK_MESSAGE(liveLoaded.disposition == sokoban::SaveStore::LoadDisposition::Loaded,
        "valid live profile remains authoritative");
    CHECK_MESSAGE(liveLoaded.profile == live,
        "stale artifacts never overwrite a valid live profile");
    CHECK_MESSAGE(!std::filesystem::exists(liveStore.primaryPath().string() + ".tmp") &&
            !std::filesystem::exists(
                liveStore.primaryPath().string() + ".replace-old"),
        "valid live profile cleans stale artifacts");
}

void testUsableProfilesSurviveMaintenanceFailures()
{
    TemporaryDirectory temporary;
    sokoban::SaveStore store(temporary.path());
    sokoban::PlayerProfile expected;
    expected.unlockedLevel = 7;
    expected.setCurrentLevel(7);
    expected.normalize();
    CHECK_MESSAGE(store.save(expected),
        "primary saves before auxiliary-artifact fault injection");
    const std::string primaryContents = readFile(store.primaryPath());

    std::filesystem::create_directory(store.backupPath());
    const sokoban::SaveStore::LoadResult loaded = store.load();

    CHECK_MESSAGE(loaded.disposition ==
            sokoban::SaveStore::LoadDisposition::LoadedWithPersistenceError,
        "auxiliary maintenance failure has a distinct load disposition");
    CHECK_MESSAGE(loaded.profile == expected,
        "auxiliary maintenance failure returns the valid primary profile");
    CHECK_MESSAGE(readFile(store.primaryPath()) == primaryContents,
        "auxiliary maintenance failure preserves the primary bytes");
    CHECK_MESSAGE(std::filesystem::is_directory(store.backupPath()),
        "failed auxiliary artifact remains available for later repair");
    CHECK_MESSAGE(loaded.message.starts_with(
              "Loaded player profile, but save artifact maintenance failed:"),
        "auxiliary maintenance failure reports an accurate diagnostic");

    TemporaryDirectory interruptedDirectory;
    sokoban::SaveStore interruptedStore(interruptedDirectory.path());
    sokoban::PlayerProfile interrupted;
    interrupted.unlockedLevel = 5;
    interrupted.setCurrentLevel(5);
    interrupted.normalize();
    writeFile(
        interruptedStore.primaryPath().string() + ".tmp",
        interrupted.serialize());
    std::filesystem::create_directory(interruptedStore.primaryPath());

    const sokoban::SaveStore::LoadResult pendingPromotion =
        interruptedStore.load();
    CHECK_MESSAGE(pendingPromotion.disposition ==
            sokoban::SaveStore::LoadDisposition::LoadedWithPersistenceError,
        "failed interrupted-write promotion has a distinct load disposition");
    CHECK_MESSAGE(pendingPromotion.profile == interrupted,
        "failed interrupted-write promotion returns the usable temporary profile");
    CHECK_MESSAGE(std::filesystem::is_regular_file(
              interruptedStore.primaryPath().string() + ".tmp"),
        "failed interrupted-write promotion preserves its valid source");
    CHECK_MESSAGE(std::filesystem::is_directory(interruptedStore.primaryPath()),
        "failed interrupted-write promotion preserves the blocking artifact");
}

void testStorageFailuresPreserveCommittedProfile()
{
    TemporaryDirectory temporary;
    sokoban::SaveStore store(temporary.path());
    sokoban::PlayerProfile committed;
    committed.unlockedLevel = 2;
    committed.setCurrentLevel(2);
    committed.settings.audio.musicVolume = 0.25f;
    committed.normalize();
    CHECK_MESSAGE(store.save(committed), "committed profile saves before fault injection");

    sokoban::PlayerProfile replacement = committed;
    replacement.unlockedLevel = 4;
    replacement.setCurrentLevel(4);
    replacement.settings.audio.musicVolume = 0.8f;
    replacement.normalize();

    sokoban::atomicFile::failWriteAfterForTesting(
        0, std::errc::permission_denied);
    CHECK_MESSAGE(!store.save(replacement), "permission-denied save reports failure");
    CHECK_MESSAGE(store.status().starts_with("Player profile save failed:"),
        "permission-denied save records a diagnostic");
    CHECK_MESSAGE(store.load().profile == committed,
        "permission-denied save preserves committed profile");
    CHECK_MESSAGE(!std::filesystem::exists(store.primaryPath().string() + ".tmp") &&
            !std::filesystem::exists(store.backupPath().string() + ".tmp"),
        "permission-denied save cleans temporary artifacts");

    // A replacement save writes the prior primary to the backup first. Let
    // that write finish, then simulate ENOSPC while writing the live file.
    sokoban::atomicFile::failWriteAfterForTesting(
        1, std::errc::no_space_on_device);
    CHECK_MESSAGE(!store.save(replacement), "disk-full save reports failure");
    CHECK_MESSAGE(store.load().profile == committed,
        "disk-full save preserves committed profile");
    CHECK_MESSAGE(std::filesystem::is_regular_file(store.backupPath()),
        "disk-full save retains the valid backup");
    CHECK_MESSAGE(!std::filesystem::exists(store.primaryPath().string() + ".tmp") &&
            !std::filesystem::exists(store.backupPath().string() + ".tmp"),
        "disk-full save cleans temporary artifacts");
}

void testSaveSlotStems()
{
    TemporaryDirectory directory;
    sokoban::SaveStore first(directory.path()); // historical "profile" stem
    sokoban::SaveStore second(directory.path(), "profile-slot2");
    CHECK_MESSAGE(first.primaryPath() != second.primaryPath(),
        "slot stems use separate primaries");
    CHECK_MESSAGE(first.backupPath() != second.backupPath(),
        "slot stems use separate backups");

    sokoban::PlayerProfile firstProfile;
    firstProfile.unlockedLevel = 1;
    firstProfile.normalize();
    CHECK_MESSAGE(first.save(firstProfile), "slot 1 saves");

    sokoban::PlayerProfile secondProfile;
    secondProfile.settings.audio.musicVolume = 0.25f;
    secondProfile.normalize();
    CHECK_MESSAGE(second.save(secondProfile), "slot 2 saves");

    CHECK_MESSAGE(first.load().profile == firstProfile, "slot 1 reloads its own data");
    CHECK_MESSAGE(second.load().profile == secondProfile, "slot 2 reloads its own data");

    // A corrupt neighbour slot does not disturb this slot's load.
    writeFile(second.primaryPath(), "not json");
    writeFile(second.backupPath(), "also not json");
    CHECK_MESSAGE(first.load().profile == firstProfile,
        "slot 1 unaffected by corrupt slot 2");
    const sokoban::SaveStore::LoadResult recovered = second.load();
    CHECK_MESSAGE(recovered.disposition == sokoban::SaveStore::LoadDisposition::ResetCorrupt,
        "corrupt slot resets independently");
}

void testObsoleteProfilesAreSetAsideAndDoubleCorruption()
{
    nlohmann::json obsoleteJson =
        nlohmann::json::parse(sokoban::PlayerProfile {}.serialize());
    obsoleteJson["format"] = sokoban::currentPlayerProfileFormat - 1;
    const std::string obsolete = obsoleteJson.dump();
    try {
        (void)sokoban::decodePlayerProfile(obsolete);
        CHECK_MESSAGE(false, "an older format is not decoded");
    } catch (const sokoban::ObsoletePlayerProfileFormat& error) {
        CHECK_MESSAGE(error.format() == sokoban::currentPlayerProfileFormat - 1,
            "the obsolete format is reported");
    }
    checkThrows([] {
        (void)sokoban::decodePlayerProfile(R"json({ "format": 1 })json");
    }, "format 1 is obsolete too");

    const auto obsoleteArchives = [](const std::filesystem::path& directory) {
        int count = 0;
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename().string().find(
                    ".obsolete-format-" + std::to_string(
                        sokoban::currentPlayerProfileFormat - 1) + "-") !=
                std::string::npos) {
                ++count;
            }
        }
        return count;
    };

    {
        TemporaryDirectory directory;
        sokoban::SaveStore store(directory.path());
        writeFile(store.primaryPath(), obsolete);
        writeFile(store.backupPath(), obsolete);
        writeFile(store.primaryPath().string() + ".tmp", obsolete);
        const sokoban::SaveStore::InspectionResult inspected = store.inspect();
        CHECK_MESSAGE(inspected.disposition ==
                sokoban::SaveStore::InspectionDisposition::Missing,
            "an obsolete slot inspects as empty");
        CHECK_MESSAGE(std::filesystem::exists(store.primaryPath()),
            "inspection leaves obsolete files alone");

        const sokoban::SaveStore::LoadResult loaded = store.load();
        CHECK_MESSAGE(loaded.disposition ==
                sokoban::SaveStore::LoadDisposition::SetAsideObsolete,
            "loading an obsolete profile starts fresh");
        CHECK_MESSAGE(loaded.profile == sokoban::PlayerProfile {},
            "a set-aside profile returns defaults");
        CHECK_MESSAGE(loaded.message.find("older build") != std::string::npos,
            "the status explains why progress is gone");
        CHECK_MESSAGE(!std::filesystem::exists(store.primaryPath()) &&
                !std::filesystem::exists(store.backupPath()) &&
                !std::filesystem::exists(
                    store.primaryPath().string() + ".tmp"),
            "every obsolete artifact is moved out of the way");
        CHECK_MESSAGE(obsoleteArchives(directory.path()) == 3,
            "obsolete artifacts are renamed, not deleted");
        CHECK_MESSAGE(!hasCorruptArchive(directory.path(), "profile.json.corrupt-"),
            "obsolete files are not reported as corrupt");
        CHECK_MESSAGE(store.save(loaded.profile),
            "a fresh profile saves over the set-aside slot");
        CHECK_MESSAGE(store.load().disposition ==
                sokoban::SaveStore::LoadDisposition::Loaded,
            "the new profile loads normally");
    }
    {
        // A current backup still recovers when only the primary is old.
        TemporaryDirectory directory;
        sokoban::SaveStore store(directory.path());
        sokoban::PlayerProfile backup;
        backup.unlockedLevel = 2;
        backup.normalize();
        writeFile(store.primaryPath(), obsolete);
        writeFile(store.backupPath(), backup.serialize());
        const sokoban::SaveStore::LoadResult loaded = store.load();
        CHECK_MESSAGE(loaded.disposition ==
                sokoban::SaveStore::LoadDisposition::RecoveredBackup,
            "a current backup is recovered over an obsolete primary");
        CHECK_MESSAGE(loaded.profile == backup, "the backup's data is used");
        CHECK_MESSAGE(obsoleteArchives(directory.path()) == 1,
            "the obsolete primary is set aside");
    }
    {
        // Saving without loading first must not back up an unreadable file.
        TemporaryDirectory directory;
        sokoban::SaveStore store(directory.path());
        writeFile(store.primaryPath(), obsolete);
        sokoban::PlayerProfile fresh;
        fresh.unlockedLevel = 1;
        fresh.normalize();
        CHECK_MESSAGE(store.save(fresh), "saving over an obsolete primary works");
        CHECK_MESSAGE(!std::filesystem::exists(store.backupPath()),
            "the obsolete primary is not copied into the backup");
        CHECK_MESSAGE(obsoleteArchives(directory.path()) == 1,
            "the obsolete primary is set aside instead");
        CHECK_MESSAGE(store.load().profile == fresh, "the saved profile loads");
    }

    TemporaryDirectory corruptDirectory;
    sokoban::SaveStore corruptStore(corruptDirectory.path());
    writeFile(corruptStore.primaryPath(), "bad primary");
    writeFile(corruptStore.backupPath(), "bad backup");
    const sokoban::SaveStore::LoadResult reset = corruptStore.load();
    CHECK_MESSAGE(reset.disposition == sokoban::SaveStore::LoadDisposition::ResetCorrupt,
        "double corruption resets defaults");
    CHECK_MESSAGE(reset.profile == sokoban::PlayerProfile {}, "double corruption returns defaults");
    CHECK_MESSAGE(sokoban::decodePlayerProfile(
        readFile(corruptStore.primaryPath())).profile ==
            sokoban::PlayerProfile {},
        "double corruption writes valid replacement");
}

void testBackupRepairFailuresPreserveDecodedProfiles()
{
    TemporaryDirectory backupDirectory;
    sokoban::SaveStore backupStore(backupDirectory.path());
    sokoban::PlayerProfile backup;
    backup.unlockedLevel = 3;
    backup.setCurrentLevel(3);
    backup.normalize();
    writeFile(backupStore.backupPath(), backup.serialize());

    sokoban::atomicFile::failWriteAfterForTesting(
        0, std::errc::no_space_on_device);
    const sokoban::SaveStore::LoadResult recovered = backupStore.load();
    CHECK_MESSAGE(recovered.disposition ==
            sokoban::SaveStore::LoadDisposition::LoadedWithPersistenceError,
        "backup repair failure has a distinct load disposition");
    CHECK_MESSAGE(recovered.profile == backup,
        "backup repair failure returns the decoded backup profile");
    CHECK_MESSAGE(readFile(backupStore.backupPath()) == backup.serialize(),
        "backup repair failure preserves the valid backup");
    CHECK_MESSAGE(!hasCorruptArchive(
              backupDirectory.path(), "profile.backup.json.corrupt-"),
        "backup repair failure does not archive the valid backup");
    CHECK_MESSAGE(recovered.message.starts_with(
              "Recovered player profile from backup in memory, but primary repair failed:"),
        "backup repair failure reports an accurate storage diagnostic");
}

void testUnsupportedProfileFormatsArePreserved()
{
    TemporaryDirectory directory;
    sokoban::SaveStore store(directory.path());
    nlohmann::json future =
        nlohmann::json::parse(sokoban::PlayerProfile {}.serialize());
    future["format"] = sokoban::currentPlayerProfileFormat + 1;
    const std::string contents = future.dump();
    writeFile(store.primaryPath(), contents);
    const std::string staleTemporary = sokoban::PlayerProfile {}.serialize();
    writeFile(store.primaryPath().string() + ".tmp", staleTemporary);

    const sokoban::SaveStore::InspectionResult inspected = store.inspect();
    CHECK_MESSAGE(inspected.disposition ==
            sokoban::SaveStore::InspectionDisposition::UnsupportedFormat,
        "inspection distinguishes an unsupported profile format from corruption");

    const sokoban::SaveStore::LoadResult loaded = store.load();
    CHECK_MESSAGE(loaded.disposition ==
            sokoban::SaveStore::LoadDisposition::UnsupportedFormat,
        "loading distinguishes an unsupported profile format from corruption");
    CHECK_MESSAGE(readFile(store.primaryPath()) == contents,
        "unsupported profile remains byte-for-byte intact");
    CHECK_MESSAGE(readFile(store.primaryPath().string() + ".tmp") == staleTemporary,
        "unsupported profile prevents recovery from modifying ambiguous artifacts");
    CHECK_MESSAGE(!hasCorruptArchive(directory.path(), "profile.json.corrupt-"),
        "unsupported profile is not archived as corrupt");
}

void testAsyncSaveCoalescingAndFlush()
{
    TemporaryDirectory temporary;
    sokoban::AsyncSaveStore store(temporary.path(), std::chrono::seconds(5));
    sokoban::SaveStore::LoadResult created = store.load();

    sokoban::PlayerProfile first = created.profile;
    first.settings.audio.musicVolume = 0.25f;
    sokoban::PlayerProfile latest = first;
    latest.settings.audio.musicVolume = 0.75f;

    const auto firstRevision = store.requestSave(first);
    const auto latestRevision = store.requestSave(latest);
    CHECK_MESSAGE(firstRevision == 1 && latestRevision == 2,
        "save requests receive ordered channel revisions");
    const sokoban::AsyncSaveStore::Diagnostics queued = store.diagnostics();
    CHECK_MESSAGE(queued.requests == 2, "async save requests counted");
    CHECK_MESSAGE(queued.pending, "deferred save remains off the calling thread");
    CHECK_MESSAGE(queued.coalescedRequests == 1, "pending saves coalesce");

    const sokoban::AsyncSaveStore::FlushResult coalesced = store.flush();
    CHECK_MESSAGE(coalesced.allPersisted(),
        "successful coalesced flush reports success");
    CHECK_MESSAGE(coalesced.forChannel(0).requestedRevision == latestRevision &&
            coalesced.forChannel(0).persistedRevision == latestRevision,
        "coalesced flush identifies the newest durable revision");
    const sokoban::AsyncSaveStore::Diagnostics flushed = store.diagnostics();
    CHECK_MESSAGE(flushed.completedWrites == 1, "coalesced profiles produce one write");
    CHECK_MESSAGE(!flushed.pending && !flushed.writing, "flush drains background writer");
    CHECK_MESSAGE(flushed.lastWriteSucceeded, "background save succeeds");

    std::ifstream stream(store.primaryPath(), std::ios::binary);
    const std::string contents {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char> {}
    };
    stream.close();
    CHECK_MESSAGE(sokoban::decodePlayerProfile(contents).profile.settings.audio.musicVolume == 0.75f,
        "coalesced save writes newest profile");

    latest.settings.audio.musicVolume = 0.5f;
    store.requestSave(latest, sokoban::AsyncSaveStore::Urgency::Immediate);
    CHECK_MESSAGE(store.flush().allPersisted(),
        "successful immediate flush reports success");
    CHECK_MESSAGE(store.diagnostics().completedWrites == 2,
        "immediate request is written by background worker");
}

void testAsyncSaveDestructorFlushesNewestProfile()
{
    TemporaryDirectory temporary;
    {
        sokoban::AsyncSaveStore store(temporary.path(), std::chrono::hours(1));
        sokoban::PlayerProfile profile = store.load().profile;
        profile.settings.audio.soundVolume = 0.35f;
        store.requestSave(profile);
    }

    std::ifstream stream(temporary.path() / "profile.json", std::ios::binary);
    const std::string contents {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char> {}
    };
    CHECK_MESSAGE(sokoban::decodePlayerProfile(contents).profile.settings.audio.soundVolume == 0.35f,
        "async store destructor flushes newest profile");
}

void testAsyncSaveFailureRetainsRetryableSnapshot()
{
    TemporaryDirectory temporary;
    sokoban::AsyncSaveStore store(
        temporary.path(), std::chrono::hours(1));
    const int independentChannel = store.addChannel(
        temporary.path(), "settings", sokoban::ProfileSections::SettingsOnly);
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 4;
    profile.setCurrentLevel(4);
    profile.normalize();

    sokoban::atomicFile::failWriteAfterForTesting(
        0, std::errc::no_space_on_device);
    store.requestSave(profile, sokoban::AsyncSaveStore::Urgency::Immediate);
    const sokoban::AsyncSaveStore::FlushResult failedFlush = store.flush();
    CHECK_MESSAGE(!failedFlush.allPersisted(),
        "failed asynchronous write makes flush report failure");
    CHECK_MESSAGE(failedFlush.forChannel(0).outcome ==
            sokoban::AsyncSaveStore::PersistenceOutcome::RetryableFailure &&
            failedFlush.forChannel(0).requestedRevision == 1 &&
            failedFlush.forChannel(0).persistedRevision == 0,
        "failed flush identifies the requested and last durable revisions");

    const sokoban::AsyncSaveStore::Diagnostics failed = store.diagnostics();
    CHECK_MESSAGE(failed.pending && !failed.writing,
        "failed asynchronous write retains its snapshot as pending");
    CHECK_MESSAGE(!failed.lastWriteSucceeded,
        "failed asynchronous write remains visible in diagnostics");
    CHECK_MESSAGE(failedFlush.forChannel(0).message.starts_with(
            "Player profile save failed:"),
        "failed flush returns its storage error without a separate status lookup");

    const sokoban::AsyncSaveStore::PersistenceResult rejectedReplacement =
        store.replaceChannel(
            0, temporary.path(), "replacement",
            sokoban::ProfileSections::All);
    CHECK_MESSAGE(rejectedReplacement.outcome ==
            sokoban::AsyncSaveStore::PersistenceOutcome::RetryableFailure &&
            rejectedReplacement.requestedRevision == 1 &&
            rejectedReplacement.persistedRevision == 0,
        "channel replacement returns the retained persistence failure");
    CHECK_MESSAGE(store.primaryPath() == temporary.path() / "profile.json",
        "failed channel replacement keeps the original destination");

    sokoban::PlayerProfile settings;
    settings.settings.audio.musicVolume = 0.25f;
    store.requestSave(
        independentChannel, settings, sokoban::AsyncSaveStore::Urgency::Immediate);
    const sokoban::AsyncSaveStore::FlushResult independentFlush = store.flush();
    CHECK_MESSAGE(!independentFlush.allPersisted(),
        "another channel does not implicitly retry a retained failure");
    CHECK_MESSAGE(independentFlush.forChannel(independentChannel).outcome ==
            sokoban::AsyncSaveStore::PersistenceOutcome::Persisted,
        "flush reports successful channels independently");
    CHECK_MESSAGE(store.diagnostics().completedWrites == failed.completedWrites,
        "blocked failed snapshot is not retried by another channel's wakeup");
    CHECK_MESSAGE(store.load(independentChannel).profile.settings.audio.musicVolume == 0.25f,
        "independent channel still persists while a failed snapshot is retained");

    CHECK_MESSAGE(store.retryFailedSave(), "retained asynchronous snapshot can be retried");
    const sokoban::AsyncSaveStore::FlushResult retried = store.flush();
    CHECK_MESSAGE(retried.allPersisted(),
        "successful retry makes flush report success");
    CHECK_MESSAGE(retried.forChannel(0).persistedRevision == 1,
        "successful retry publishes the retained revision");
    CHECK_MESSAGE(!store.diagnostics().pending &&
            store.diagnostics().lastWriteSucceeded,
        "successful retry clears pending failure state");
    CHECK_MESSAGE(store.load().profile == profile,
        "retry persists the exact retained snapshot");
    CHECK_MESSAGE(!store.retryFailedSave(),
        "successful channel has no failed snapshot left to retry");
}

} // namespace

void testAsyncStoreMultipleChannels()
{
    TemporaryDirectory temporary;
    {
        // Channel 0 = "profile" (progress only); add channel 1 = "settings".
        sokoban::AsyncSaveStore store(
            temporary.path(), std::chrono::milliseconds(0), "profile",
            sokoban::ProfileSections::ProgressOnly);
        const int settings = store.addChannel(
            temporary.path(), "settings", sokoban::ProfileSections::SettingsOnly);
        CHECK_MESSAGE(settings == 1, "added channel gets the next id");

        sokoban::PlayerProfile progress;
        progress.unlockedLevel = 2;
        progress.setCurrentScreen(2, 0);
        progress.normalize();
        sokoban::PlayerProfile config;
        config.settings.audio.musicVolume = 0.2f;
        config.normalize();

        store.requestSave(progress);
        store.requestSave(settings, config);
        CHECK_MESSAGE(store.flush().allPersisted(),
            "multi-channel flush reports success");

        // One worker wrote both channels to their own files.
        CHECK_MESSAGE(store.diagnostics(0).completedWrites >= 1, "channel 0 wrote");
        CHECK_MESSAGE(store.diagnostics(settings).completedWrites >= 1, "channel 1 wrote");
        CHECK_MESSAGE(std::filesystem::is_regular_file(temporary.path() / "profile.json"),
            "progress channel wrote its file");
        CHECK_MESSAGE(std::filesystem::is_regular_file(temporary.path() / "settings.json"),
            "settings channel wrote its own file");

        // Each channel round-trips only its own sections.
        CHECK_MESSAGE(store.load(0).profile.unlockedLevel == 2, "channel 0 has progress");
        CHECK_MESSAGE(store.load(settings).profile.settings.audio.musicVolume == 0.2f,
            "channel 1 has settings");

        // Repointing a channel drains it then targets a new file.
        const int repointed = store.addChannel(
            temporary.path(), "profile-slot2",
            sokoban::ProfileSections::ProgressOnly);
        sokoban::PlayerProfile slot2;
        slot2.unlockedLevel = 5;
        slot2.setCurrentScreen(5, 0);
        slot2.normalize();
        store.requestSave(repointed, slot2);
        CHECK_MESSAGE(store.flush().allPersisted(),
            "repointed channel flush reports success");
        CHECK_MESSAGE(std::filesystem::is_regular_file(temporary.path() / "profile-slot2.json"),
            "third channel wrote a distinct file");
        const sokoban::AsyncSaveStore::PersistenceResult replacement =
            store.replaceChannel(
                repointed, temporary.path(), "profile-slot3",
                sokoban::ProfileSections::ProgressOnly);
        CHECK_MESSAGE(replacement.outcome ==
                sokoban::AsyncSaveStore::PersistenceOutcome::Persisted &&
                replacement.requestedRevision ==
                    replacement.persistedRevision,
            "persisted channel can be replaced");
        const sokoban::AsyncSaveStore::PersistenceResult freshDestination =
            store.flush().forChannel(repointed);
        CHECK_MESSAGE(freshDestination.requestedRevision == 0 &&
                freshDestination.persistedRevision == 0,
            "replaced channel starts a fresh destination revision epoch");
        CHECK_MESSAGE(store.load(repointed).profile.progressEmpty(),
            "replaced channel points at a fresh (empty) store");
    }
    // The single worker joined cleanly at destruction with all channels drained.
}

void testKeyboardChordsAndEditorBindingsRoundTrip()
{
    sokoban::PlayerProfile profile;
    sokoban::InputBindings& input = profile.settings.input;
    input.forAction(sokoban::InputAction::EditorSave) = {
        sokoban::KeyboardBinding {
            "S",
            sokoban::keyModifierCtrl | sokoban::keyModifierAlt,
        },
    };
    const std::string serialized = profile.serialize();
    const nlohmann::json json = nlohmann::json::parse(serialized);
    const nlohmann::json& save = json["settings"]["input"]["editorSave"][0];
    CHECK_MESSAGE(save["control"] == "S" &&
            save["modifiers"] == nlohmann::json::array({ "ctrl", "alt" }),
        "a chord serializes its modifiers by name");
    CHECK_MESSAGE(!json["settings"]["input"]["undo"][0].contains("modifiers"),
        "a plain key has no modifiers property");
    CHECK_MESSAGE(sokoban::decodePlayerProfile(serialized).profile == profile,
        "chords round-trip");
    for (std::size_t index = 0; index < sokoban::inputActionCount; ++index) {
        const auto action = static_cast<sokoban::InputAction>(index);
        CHECK_MESSAGE(json["settings"]["input"].contains(
                          std::string(sokoban::inputActionName(action))),
            "every action, editor shortcuts included, is persisted");
    }

    nlohmann::json invalid = json;
    invalid["settings"]["input"]["editorSave"][0]["modifiers"] =
        nlohmann::json::array({ "hyper" });
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalid.dump());
    }, "unknown modifiers are rejected");
    invalid["settings"]["input"]["editorSave"][0]["modifiers"] =
        nlohmann::json::array({ "ctrl", "ctrl" });
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalid.dump());
    }, "repeated modifiers are rejected");
    invalid["settings"]["input"]["editorSave"][0]["modifiers"] =
        nlohmann::json::array();
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalid.dump());
    }, "an empty modifier list is rejected");

    invalid = json;
    invalid["settings"]["input"].erase("editorRecentTile9");
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalid.dump());
    }, "a missing editor action is rejected");
    invalid = json;
    invalid["settings"]["input"]["retiredAction"] =
        invalid["settings"]["input"]["undo"];
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(invalid.dump());
    }, "an unknown action is rejected");
}

sokoban::PlayerProfile longHistoryProfile()
{
    constexpr int actionCount = 2048;
    constexpr int movableCount = 64;
    constexpr int enemyCount = 32;

    sokoban::GameState state;
    state.players.push_back({ .cell = { 0, 0, 1 } });
    for (int index = 0; index < movableCount; ++index) {
        state.movables.push_back({
            .type = sokoban::TileType::Rock,
            .cell = { index, 1, 1 },
        });
    }
    for (int index = 0; index < enemyCount; ++index) {
        state.enemies.push_back({ .cell = { index, 2, 1 } });
    }

    sokoban::GameplaySession::Snapshot snapshot;
    snapshot.undoStack.reserve(actionCount);
    for (int index = 0; index < actionCount; ++index) {
        sokoban::GameplaySession::Action action;
        action.before = state;
        state.players.front().cell.x = (index + 1) % 2;
        action.after = state;
        action.playerMoveCountBefore = index;
        action.playerMoveCountAfter = index + 1;
        snapshot.undoStack.push_back(std::move(action));
    }
    snapshot.state = state;
    snapshot.playerMoveCount = actionCount;

    sokoban::PlayerProfile profile;
    profile.unlockedLevel = 24;
    profile.currentLevel = 23;
    profile.currentScreen = 9;
    profile.worldContext = sokoban::PlayerProfile::WorldContext::Puzzle;
    for (int level = 0; level < 24; ++level) {
        profile.levels.push_back({
            .level = level,
            .completed = level < 23,
            .reachedScreens = 10,
        });
        for (int screen = 0; screen < 10; ++screen) {
            profile.screens.push_back({
                .level = level,
                .screen = screen,
                .completed = level < 23 || screen < 9,
            });
        }
    }
    profile.activeScreen = sokoban::PlayerProfile::ActiveScreen {
        .level = profile.currentLevel,
        .screen = profile.currentScreen,
        .completedLevelMoveCount = 12000,
        .levelElapsedSeconds = 7200.0,
        .session = std::move(snapshot),
    };
    profile.normalize();
    return profile;
}

void benchmarkProfileSnapshots()
{
    using allocationTracking::Sample;
    const sokoban::PlayerProfile profile = longHistoryProfile();
    const sokoban::GameplaySession::Snapshot& source =
        profile.activeScreen->session;

    sokoban::GameplaySession::Snapshot snapshotCopy;
    const Sample snapshot = allocationTracking::measure([&] {
        snapshotCopy = source;
    });

    sokoban::GameplaySession::Snapshot checkpointSnapshot = source;
    const auto* checkpointUndoStorage = checkpointSnapshot.undoStack.data();
    sokoban::PlayerProfile checkpointProfile;
    sokoban::CampaignSession campaign;
    const Sample checkpointTransfer = allocationTracking::measure([&] {
        campaign.writeCheckpoint(
            checkpointProfile, std::move(checkpointSnapshot));
    });
    if (!checkpointProfile.overworldCheckpoint ||
        checkpointProfile.overworldCheckpoint->session.undoStack.data() !=
            checkpointUndoStorage) {
        throw std::runtime_error(
            "Checkpoint benchmark copied rather than transferred the snapshot");
    }

    std::string serialized;
    const Sample serialization = allocationTracking::measure([&] {
        serialized = profile.serialize(sokoban::ProfileSections::ProgressOnly);
    });

    TemporaryDirectory deferredDirectory("sokoban-profile-benchmark-deferred");
    sokoban::AsyncSaveStore deferredStore(
        deferredDirectory.path(), std::chrono::hours(1), "profile",
        sokoban::ProfileSections::ProgressOnly);
    const Sample deferred = allocationTracking::measure([&] {
        deferredStore.requestSave(profile);
    });
    if (!deferredStore.flush().allPersisted()) {
        throw std::runtime_error("Deferred benchmark save failed");
    }

    TemporaryDirectory immediateDirectory("sokoban-profile-benchmark-immediate");
    sokoban::AsyncSaveStore immediateStore(
        immediateDirectory.path(), std::chrono::hours(1), "profile",
        sokoban::ProfileSections::ProgressOnly);
    uint64_t immediateRequestMicroseconds = 0;
    const Sample immediate = allocationTracking::measure([&] {
        const auto requestStarted = std::chrono::steady_clock::now();
        immediateStore.requestSave(
            profile, sokoban::AsyncSaveStore::Urgency::Immediate);
        immediateRequestMicroseconds = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - requestStarted).count());
        if (!immediateStore.flush().allPersisted()) {
            throw std::runtime_error("Immediate benchmark save failed");
        }
    });

    std::cout
        << "profile_snapshot_benchmark history=" << source.undoStack.size()
        << " entities_per_state="
        << source.state.players.size() + source.state.movables.size() +
                source.state.enemies.size()
        << " snapshot_copy_us=" << snapshot.elapsedMicroseconds
        << " snapshot_peak_bytes=" << snapshot.peakAdditionalBytes
        << " snapshot_retained_bytes=" << snapshot.retainedAdditionalBytes
        << " checkpoint_transfer_us="
        << checkpointTransfer.elapsedMicroseconds
        << " checkpoint_transfer_peak_bytes="
        << checkpointTransfer.peakAdditionalBytes
        << " serialization_us=" << serialization.elapsedMicroseconds
        << " serialization_peak_bytes=" << serialization.peakAdditionalBytes
        << " serialized_bytes=" << serialized.size()
        << " deferred_request_us=" << deferred.elapsedMicroseconds
        << " deferred_peak_bytes=" << deferred.peakAdditionalBytes
        << " deferred_retained_bytes=" << deferred.retainedAdditionalBytes
        << " immediate_request_us=" << immediateRequestMicroseconds
        << " immediate_durable_us=" << immediate.elapsedMicroseconds
        << " immediate_peak_bytes=" << immediate.peakAdditionalBytes << '\n';
}

int main(int argc, char** argv)
{
    try {
        if (argc == 2 &&
            std::string_view(argv[1]) == "--benchmark-profile-snapshots") {
            benchmarkProfileSnapshots();
            return 0;
        }
        testRoundTripAndBests();
        testReachedScreensAndProgressReset();
        testSectionedSerialization();
        testActiveScreenCheckpointRoundTrip();
        testNormalizationAndValidation();
        testScreenProgressAndOverworldCheckpoint();
        testKeyboardChordsAndEditorBindingsRoundTrip();
        testStoreBackupsAndRecovery();
        testInterruptedWriteRecovery();
        testUsableProfilesSurviveMaintenanceFailures();
        testStorageFailuresPreserveCommittedProfile();
        testSaveSlotStems();
        testObsoleteProfilesAreSetAsideAndDoubleCorruption();
        testBackupRepairFailuresPreserveDecodedProfiles();
        testUnsupportedProfileFormatsArePreserved();
        testAsyncSaveCoalescingAndFlush();
        testAsyncSaveDestructorFlushesNewestProfile();
        testAsyncSaveFailureRetainsRetryableSnapshot();
        testAsyncStoreMultipleChannels();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected player profile test exception: "
                  << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " player profile checks failed\n";
        return 1;
    }
    std::cout << "All player profile checks passed\n";
    return 0;
}
