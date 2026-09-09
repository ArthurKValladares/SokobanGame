#include "TestHarness.hpp"
#include "ScopedTestDirectory.hpp"

#include "engine/AsyncSaveStore.hpp"
#include "engine/AtomicFile.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/SaveStore.hpp"
#include "engine/UserSettingsConfig.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

template <typename Fn>
void checkThrows(Fn&& fn, const char* label)
{
    try {
        fn();
        CHECK_MESSAGE(false, label);
    } catch (const std::exception&) {
    }
}

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
    CHECK_MESSAGE(decoded.sourceFormat == sokoban::currentPlayerProfileFormat, "current format decoded");
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

    // Format-7 files (no reachedScreens) decode with zeroed counts.
    nlohmann::json legacy = nlohmann::json::parse(profile.serialize());
    legacy["format"] = 7;
    for (auto& item : legacy["progress"]["levels"]) {
        item.erase("reachedScreens");
    }
    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(legacy.dump());
    CHECK_MESSAGE(migrated.sourceFormat == 7, "format 7 source reported");
    const sokoban::PlayerProfile::LevelProgress* migratedFirst =
        migrated.profile.progressForLevel(0);
    CHECK_MESSAGE(migratedFirst != nullptr && migratedFirst->reachedScreens == 0,
        "format 7 migration defaults reached screens to zero");

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

    // A bare format-9 document decodes as a fully default profile.
    const sokoban::PlayerProfile bare =
        sokoban::decodePlayerProfile("{\"format\": 9}").profile;
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
        .type = sokoban::TileType::Rock,
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
    after.movables.front().sliding = sokoban::MoveDirection::Right;
    after.enemies.front().cell = { 5, 0, 1 };

    sokoban::GameplaySession::Action move {
        .before = before,
        .after = after,
        .playerPushing = true,
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
    CHECK_MESSAGE(!current["progress"]["activeScreen"]["session"]["state"]
            .contains("playerClones"),
        "checkpoint state has no primary/clone compatibility fields");
    CHECK_MESSAGE(current["progress"]["activeScreen"]["session"]["undoStack"][0]
            ["presentation"]["animations"].size() == 2,
        "undo presentation timeline is persisted");

    nlohmann::json format15 = current;
    format15["format"] = 15;
    format15["progress"]["activeScreen"]["session"]["undoStack"][0]
        .erase("presentation");
    const sokoban::DecodedPlayerProfile migrated15 =
        sokoban::decodePlayerProfile(format15.dump());
    CHECK_MESSAGE(migrated15.profile.activeScreen.has_value(),
        "format 15 migration preserves the active checkpoint");
    CHECK_MESSAGE(!migrated15.profile.activeScreen->session.undoStack[0]
            .presentation.motions.empty(),
        "format 15 migration reconstructs generic motion tracks");
    nlohmann::json emptyPlayers = current;
    emptyPlayers["progress"]["activeScreen"]["session"]["state"]
        ["players"] = nlohmann::json::array();
    checkThrows([&] {
        (void)sokoban::decodePlayerProfile(emptyPlayers.dump());
    }, "checkpoint rejects an empty players array");

    nlohmann::json format13 = current;
    format13["format"] = 13;
    const sokoban::DecodedPlayerProfile migrated13 =
        sokoban::decodePlayerProfile(format13.dump());
    CHECK_MESSAGE(migrated13.sourceFormat == 13,
        "format 13 checkpoint source is reported");
    CHECK_MESSAGE(!migrated13.profile.activeScreen,
        "format 13 active checkpoint is intentionally discarded");

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

    constexpr std::string_view format1 = R"json({
  "format": 1,
  "unlockedLevel": 3,
  "currentLevel": 2,
  "completedLevels": [0, 1],
  "masterVolume": 0.7,
  "musicVolume": 0.4
})json";
    const sokoban::DecodedPlayerProfile migrated = sokoban::decodePlayerProfile(format1);
    CHECK_MESSAGE(migrated.sourceFormat == 1, "format 1 source reported");
    CHECK_MESSAGE(migrated.profile.currentLevel == 2, "format 1 current level migrated");
    CHECK_MESSAGE(migrated.profile.progressForLevel(0) != nullptr, "format 1 completion migrated");
    CHECK_MESSAGE(migrated.profile.settings.audio.soundVolume == 1.0f, "new setting receives migration default");
    CHECK_MESSAGE(sokoban::decodePlayerProfile(migrated.profile.serialize()).sourceFormat ==
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
    format2Root["settings"]["video"].erase("antiAliasingSamples");
    format2Root["settings"]["video"].erase("renderScalePercent");
    format2Root["settings"]["video"].erase("customRenderScale");
    format2Root["settings"]["video"].erase("customRenderScalePercent");
    format2Root["settings"]["video"].erase("ambientOcclusion");
    format2Root["settings"]["video"].erase("windowWidth");
    format2Root["settings"]["video"].erase("windowHeight");
    const sokoban::DecodedPlayerProfile migratedFormat2 =
        sokoban::decodePlayerProfile(format2Root.dump());
    CHECK_MESSAGE(migratedFormat2.sourceFormat == 2, "format 2 source reported");
    CHECK_MESSAGE(migratedFormat2.profile.currentScreen == 0,
        "format 2 receives default screen");
    CHECK_MESSAGE(!migratedFormat2.profile.activeScreen,
        "format 2 receives no gameplay checkpoint");
    const sokoban::KeyboardBinding* migratedKeyboard = keyboardBinding(
        migratedFormat2.profile.settings.input, sokoban::InputAction::MoveUp);
    CHECK_MESSAGE(migratedKeyboard && migratedKeyboard->scancode == "Up",
        "format 2 keyboard binding migrates");
    CHECK_MESSAGE(migratedFormat2.profile.settings.input.forAction(
            sokoban::InputAction::MoveUp).size() == 3,
        "format 2 migration adds controller defaults");

    nlohmann::json format3Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format3Root["format"] = 3;
    format3Root["settings"]["input"] = legacyInput;
    format3Root["settings"]["video"].erase("antiAliasingSamples");
    format3Root["settings"]["video"].erase("renderScalePercent");
    format3Root["settings"]["video"].erase("customRenderScale");
    format3Root["settings"]["video"].erase("customRenderScalePercent");
    format3Root["settings"]["video"].erase("ambientOcclusion");
    format3Root["settings"]["video"].erase("windowWidth");
    format3Root["settings"]["video"].erase("windowHeight");
    const sokoban::DecodedPlayerProfile migratedFormat3 =
        sokoban::decodePlayerProfile(format3Root.dump());
    CHECK_MESSAGE(migratedFormat3.sourceFormat == 3, "format 3 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat3.profile.settings.input, sokoban::InputAction::Undo);
    CHECK_MESSAGE(migratedKeyboard && migratedKeyboard->scancode == "Backspace",
        "format 3 keyboard binding migrates");

    nlohmann::json format4Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format4Root["format"] = 4;
    format4Root["settings"]["input"].erase("menuConfirm");
    format4Root["settings"]["video"].erase("antiAliasingSamples");
    format4Root["settings"]["video"].erase("renderScalePercent");
    format4Root["settings"]["video"].erase("customRenderScale");
    format4Root["settings"]["video"].erase("customRenderScalePercent");
    format4Root["settings"]["video"].erase("ambientOcclusion");
    format4Root["settings"]["video"].erase("windowWidth");
    format4Root["settings"]["video"].erase("windowHeight");
    const sokoban::DecodedPlayerProfile migratedFormat4 =
        sokoban::decodePlayerProfile(format4Root.dump());
    CHECK_MESSAGE(migratedFormat4.sourceFormat == 4, "format 4 source reported");
    CHECK_MESSAGE(!migratedFormat4.profile.settings.input.forAction(
            sokoban::InputAction::MenuConfirm).empty(),
        "format 4 receives menu-confirm defaults");
    CHECK_MESSAGE(migratedFormat4.profile.settings.video.antiAliasingSamples ==
            sokoban::config::antiAliasingSamples,
        "format 4 receives MSAA default");
    CHECK_MESSAGE(migratedFormat4.profile.settings.video.windowWidth == 1280 &&
            migratedFormat4.profile.settings.video.windowHeight == 720,
        "format 4 receives window-size defaults");

    nlohmann::json format24 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format24["format"] = 24;
    format24["settings"]["video"].erase("allowTearing");
    format24["settings"]["video"].erase("frameRateLimit");
    const sokoban::DecodedPlayerProfile migratedFormat24 =
        sokoban::decodePlayerProfile(format24.dump());
    CHECK_MESSAGE(migratedFormat24.sourceFormat == 24,
        "format 24 source reported");
    CHECK_MESSAGE(!migratedFormat24.profile.settings.video.allowTearing,
        "format 24 receives safe tearing default");
    CHECK_MESSAGE(migratedFormat24.profile.settings.video.frameRateLimit == 0,
        "format 24 receives unlimited foreground cap default");

    nlohmann::json format25 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format25["format"] = 25;
    format25["settings"]["accessibility"] = {
        { "reducedMotion", true },
        { "highContrast", true },
        { "largeText", true },
        { "subtitles", false },
        { "screenShake", false },
    };
    const sokoban::DecodedPlayerProfile migratedFormat25 =
        sokoban::decodePlayerProfile(format25.dump());
    CHECK_MESSAGE(migratedFormat25.sourceFormat == 25,
        "format 25 source reported");
    CHECK_MESSAGE(migratedFormat25.profile.serialize().find("\"accessibility\"") ==
            std::string::npos,
        "format 25 accessibility settings are removed during migration");

    nlohmann::json format26 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format26["format"] = 26;
    format26["settings"]["video"].erase("exposureEv");
    const sokoban::DecodedPlayerProfile migratedFormat26 =
        sokoban::decodePlayerProfile(format26.dump());
    CHECK_MESSAGE(migratedFormat26.sourceFormat == 26,
        "format 26 source reported");
    CHECK_MESSAGE(migratedFormat26.profile.settings.video.exposureEv == 0.0f,
        "format 26 receives neutral exposure");

    nlohmann::json format5Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format5Root["format"] = 5;
    format5Root["settings"]["video"].erase("renderScalePercent");
    format5Root["settings"]["video"].erase("customRenderScale");
    format5Root["settings"]["video"].erase("customRenderScalePercent");
    const sokoban::DecodedPlayerProfile migratedFormat5 =
        sokoban::decodePlayerProfile(format5Root.dump());
    CHECK_MESSAGE(migratedFormat5.sourceFormat == 5, "format 5 source reported");
    CHECK_MESSAGE(migratedFormat5.profile.settings.video.renderScalePercent == 100,
        "format 5 receives native render scale");

    nlohmann::json format6Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format6Root["format"] = 6;
    format6Root["settings"]["video"].erase("customRenderScale");
    format6Root["settings"]["video"].erase("customRenderScalePercent");
    const sokoban::DecodedPlayerProfile migratedFormat6 =
        sokoban::decodePlayerProfile(format6Root.dump());
    CHECK_MESSAGE(migratedFormat6.sourceFormat == 6, "format 6 source reported");
    CHECK_MESSAGE(!migratedFormat6.profile.settings.video.customRenderScale,
        "format 6 defaults to preset render scale");
    CHECK_MESSAGE(migratedFormat6.profile.settings.video.customRenderScalePercent == 100,
        "format 6 receives a native custom value");

    nlohmann::json format9Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format9Root["format"] = 9;
    format9Root["settings"]["input"].erase("mirror");
    format9Root["settings"]["input"].erase("showTopDownView");
    format9Root["settings"]["input"]["undo"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "Z" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "west" } },
    });
    const sokoban::DecodedPlayerProfile migratedFormat9 =
        sokoban::decodePlayerProfile(format9Root.dump());
    CHECK_MESSAGE(migratedFormat9.sourceFormat == 9, "format 9 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat9.profile.settings.input, sokoban::InputAction::Undo);
    CHECK_MESSAGE(migratedKeyboard && migratedKeyboard->scancode == "Z",
        "format 9 keeps the original undo default");

    nlohmann::json format10Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format10Root["format"] = 10;
    format10Root["settings"]["input"].erase("showTopDownView");
    format10Root["settings"]["input"]["mirror"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "Z" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    format10Root["settings"]["input"]["undo"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "X" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "west" } },
    });
    const sokoban::DecodedPlayerProfile migratedFormat10 =
        sokoban::decodePlayerProfile(format10Root.dump());
    CHECK_MESSAGE(migratedFormat10.sourceFormat == 10, "format 10 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat10.profile.settings.input, sokoban::InputAction::Undo);
    CHECK_MESSAGE(migratedKeyboard && migratedKeyboard->scancode == "Z",
        "format 10 default undo returns to Z");

    format10Root["settings"]["input"]["mirror"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "G" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    const sokoban::DecodedPlayerProfile migratedCustomFormat10 =
        sokoban::decodePlayerProfile(format10Root.dump());
    const nlohmann::json migratedCustomFormat10Json = nlohmann::json::parse(
        migratedCustomFormat10.profile.serialize());
    CHECK_MESSAGE(!migratedCustomFormat10Json["settings"]["input"].contains("mirror"),
        "retired custom mirror binding is removed");

    nlohmann::json format11Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format11Root["format"] = 11;
    format11Root["settings"]["input"].erase("showTopDownView");
    format11Root["settings"]["input"]["undo"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "T" } },
    });
    const sokoban::DecodedPlayerProfile migratedFormat11 =
        sokoban::decodePlayerProfile(format11Root.dump());
    CHECK_MESSAGE(migratedFormat11.sourceFormat == 11, "format 11 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat11.profile.settings.input,
        sokoban::InputAction::ShowTopDownView);
    CHECK_MESSAGE(migratedKeyboard && migratedKeyboard->scancode == "T",
        "format 11 receives current-screen top-down default");
    migratedKeyboard = keyboardBinding(
        migratedFormat11.profile.settings.input, sokoban::InputAction::Undo);
    CHECK_MESSAGE(migratedKeyboard && migratedKeyboard->scancode == "Z",
        "format 11 binding displaced by T recovers its default");

    nlohmann::json format12Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format12Root["format"] = 12;
    format12Root["settings"]["video"].erase("ambientOcclusionStrength");
    const sokoban::DecodedPlayerProfile migratedFormat12 =
        sokoban::decodePlayerProfile(format12Root.dump());
    CHECK_MESSAGE(migratedFormat12.sourceFormat == 12,
        "format 12 source reported");
    CHECK_MESSAGE(migratedFormat12.profile.settings.video.ambientOcclusionStrength ==
            sokoban::UserSettings {}.video.ambientOcclusionStrength,
        "format 12 receives AO strength default");

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

void testScreenProgressOverworldCheckpointAndFormat17Migration()
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
    profile.worldContext = sokoban::PlayerProfile::WorldContext::Overworld;

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    CHECK_MESSAGE(decoded.profile == profile,
        "screen progress and overworld checkpoint round-trip");

    nlohmann::json format17 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format17["format"] = 17;
    format17["progress"].erase("screens");
    format17["progress"].erase("overworldCheckpoint");
    format17["progress"].erase("worldContext");
    format17["progress"]["levels"] = nlohmann::json::array({ {
        { "level", 4 },
        { "completed", true },
        { "reachedScreens", 2 },
        { "bestMoves", 30 },
        { "bestTimeSeconds", 20.0 },
    } });
    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(format17.dump());
    CHECK_MESSAGE(migrated.sourceFormat == 17, "format 17 source is reported");
    CHECK_MESSAGE(migrated.profile.screenCompleted({ .level = 4, .screen = 0 }) &&
            migrated.profile.screenCompleted({ .level = 4, .screen = 1 }),
        "format 17 reached screens migrate as completed");
    CHECK_MESSAGE(!migrated.profile.progressForScreen({ .level = 4, .screen = 0 })
                ->bestMoves,
        "legacy aggregate best is not copied to an individual screen");
    CHECK_MESSAGE(migrated.profile.worldContext ==
            sokoban::PlayerProfile::WorldContext::Overworld,
        "format 17 without an active checkpoint resumes in overworld");

    nlohmann::json format19 = nlohmann::json::parse(profile.serialize());
    format19["format"] = 19;
    format19["progress"]["overworldSession"] =
        format19["progress"]["overworldCheckpoint"]["session"];
    format19["progress"].erase("overworldCheckpoint");
    const sokoban::DecodedPlayerProfile migrated19 =
        sokoban::decodePlayerProfile(format19.dump());
    CHECK_MESSAGE(migrated19.sourceFormat == 19,
        "format 19 source is reported");
    CHECK_MESSAGE(!migrated19.profile.overworldCheckpoint,
        "format 19 single-overworld checkpoint is safely discarded");
    CHECK_MESSAGE(migrated19.profile.screenCompleted({ .level = 2, .screen = 3 }),
        "format 19 puzzle progress survives checkpoint migration");
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
    CHECK_MESSAGE(migrated.disposition == sokoban::SaveStore::LoadDisposition::Migrated,
        "store migrates old primary");
    CHECK_MESSAGE(sokoban::decodePlayerProfile(
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
    CHECK_MESSAGE(reset.disposition == sokoban::SaveStore::LoadDisposition::ResetCorrupt,
        "double corruption resets defaults");
    CHECK_MESSAGE(reset.profile == sokoban::PlayerProfile {}, "double corruption returns defaults");
    CHECK_MESSAGE(sokoban::decodePlayerProfile(
        [&] {
            std::ifstream stream(corruptStore.primaryPath(), std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }()).sourceFormat == sokoban::currentPlayerProfileFormat,
        "double corruption writes valid replacement");
}

void testMigrationPersistenceFailuresPreserveDecodedProfiles()
{
    constexpr std::string_view legacy = R"json({
  "format": 1,
  "unlockedLevel": 3,
  "currentLevel": 2,
  "completedLevels": [0],
  "masterVolume": 0.5,
  "musicVolume": 0.25,
  "soundVolume": 0.75
})json";
    const sokoban::PlayerProfile expected =
        sokoban::decodePlayerProfile(legacy).profile;

    const auto checkMigrationFailure = [&](std::uint32_t writesBeforeFailure) {
        TemporaryDirectory directory;
        sokoban::SaveStore store(directory.path());
        writeFile(store.primaryPath(), legacy);

        sokoban::atomicFile::failWriteAfterForTesting(
            writesBeforeFailure, std::errc::no_space_on_device);
        const sokoban::SaveStore::LoadResult loaded = store.load();

        CHECK_MESSAGE(loaded.disposition ==
                sokoban::SaveStore::LoadDisposition::LoadedWithPersistenceError,
            "migration write failure has a distinct load disposition");
        CHECK_MESSAGE(loaded.profile == expected,
            "migration write failure returns the decoded legacy profile");
        CHECK_MESSAGE(readFile(store.primaryPath()) == legacy,
            "migration write failure preserves the valid legacy primary");
        CHECK_MESSAGE(!hasCorruptArchive(directory.path(), "profile.json.corrupt-"),
            "migration write failure does not archive the valid primary");
        CHECK_MESSAGE(loaded.message.starts_with(
                  "Loaded legacy player profile, but migration could not be saved:"),
            "migration write failure reports an accurate storage diagnostic");
    };

    // Migration first protects the old primary in the backup, then installs
    // the current-format primary. Exercise failure at each write boundary.
    checkMigrationFailure(0);
    checkMigrationFailure(1);

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

void testFormat18AddsEditorBindings()
{
    nlohmann::json format18 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format18["format"] = 18;
    format18["settings"]["input"].erase("editorReplaceTile");
    format18["settings"]["input"].erase("editorDeleteTile");
    format18["settings"]["input"].erase("editorMoveTile");

    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(format18.dump());
    CHECK_MESSAGE(migrated.sourceFormat == 18, "format 18 source is reported");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::EditorReplaceTile) == "R",
        "format 18 receives the editor replace default");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::EditorDeleteTile) == "D",
        "format 18 receives the editor delete default");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::EditorMoveTile) == "M",
        "format 18 receives the editor move default");
}

void testFormat20AddsScreenPreviewBinding()
{
    nlohmann::json format20 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format20["format"] = 20;
    format20["settings"]["input"].erase("previewScreen");

    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(format20.dump());
    CHECK_MESSAGE(migrated.sourceFormat == 20, "format 20 source is reported");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::PreviewScreen) ==
            "V / Pad rightshoulder",
        "format 20 receives the screen preview defaults");
}

void testFormat21ConsolidatesInteractBinding()
{
    nlohmann::json format21 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format21["format"] = 21;
    format21["settings"]["input"]["mirror"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "F" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    format21["settings"]["input"]["menuConfirm"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "Return" } },
        nlohmann::json { { "type", "keyboard" }, { "control", "Space" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "south" } },
    });

    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(format21.dump());
    CHECK_MESSAGE(migrated.sourceFormat == 21, "format 21 source is reported");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::MenuConfirm) ==
            "Space / Pad south",
        "format 21 receives the consolidated interact default");
    const nlohmann::json current = nlohmann::json::parse(
        migrated.profile.serialize());
    CHECK_MESSAGE(!current["settings"]["input"].contains("mirror"),
        "format 21 mirror binding is retired");

    format21["settings"]["input"]["mirror"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "G" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    const sokoban::DecodedPlayerProfile migratedMirrorCustom =
        sokoban::decodePlayerProfile(format21.dump());
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migratedMirrorCustom.profile.settings.input,
              sokoban::InputAction::MenuConfirm) ==
            "G / Pad south",
        "format 21 carries a customized mirror key into interact");

    format21["settings"]["input"]["menuConfirm"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "G" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "south" } },
    });
    const sokoban::DecodedPlayerProfile migratedCustom =
        sokoban::decodePlayerProfile(format21.dump());
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migratedCustom.profile.settings.input,
              sokoban::InputAction::MenuConfirm) ==
            "G / Pad south",
        "format 21 preserves a customized interact binding");
}

void testFormat22UpdatesOverworldViewBinding()
{
    nlohmann::json format22 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format22["format"] = 22;
    format22["settings"]["input"]["showTopDownView"] =
        nlohmann::json::array({
            nlohmann::json {
                { "type", "keyboard" }, { "control", "T" } },
        });

    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(format22.dump());
    CHECK_MESSAGE(migrated.sourceFormat == 22, "format 22 source is reported");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::ShowTopDownView) ==
            "T",
        "format 22 receives the current-screen top-down default");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::ShowOverworldMap) ==
            "Tab / Pad lefttrigger+",
        "format 22 receives TAB and left-trigger overworld defaults");

    format22["settings"]["input"]["showTopDownView"] =
        nlohmann::json::array({
            nlohmann::json {
                { "type", "keyboard" }, { "control", "Q" } },
        });
    const sokoban::DecodedPlayerProfile custom =
        sokoban::decodePlayerProfile(format22.dump());
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              custom.profile.settings.input,
              sokoban::InputAction::ShowTopDownView) == "Q",
        "format 22 preserves a customized overview binding");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              custom.profile.settings.input,
              sokoban::InputAction::ShowOverworldMap) ==
            "Tab / Pad lefttrigger+",
        "format 22 custom top-down binding still receives overworld map defaults");

    nlohmann::json format23 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format23["format"] = 23;
    format23["settings"]["input"].erase("showOverworldMap");
    format23["settings"]["input"]["showTopDownView"] =
        nlohmann::json::array({
            nlohmann::json {
                { "type", "keyboard" }, { "control", "Tab" } },
            nlohmann::json {
                { "type", "gamepadAxis" },
                { "control", "lefttrigger" },
                { "direction", "positive" },
                { "threshold", 0.5f },
            },
        });
    const sokoban::DecodedPlayerProfile split =
        sokoban::decodePlayerProfile(format23.dump());
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              split.profile.settings.input,
              sokoban::InputAction::ShowTopDownView) == "T",
        "format 23 combined binding migrates back to T for current screen");
    CHECK_MESSAGE(sokoban::actionBindingsDisplay(
              split.profile.settings.input,
              sokoban::InputAction::ShowOverworldMap) ==
            "Tab / Pad lefttrigger+",
        "format 23 combined binding migrates to the whole-map action");
}

int main()
{
    try {
        testRoundTripAndBests();
        testReachedScreensAndProgressReset();
        testSectionedSerialization();
        testActiveScreenCheckpointRoundTrip();
        testNormalizationAndMigration();
        testScreenProgressOverworldCheckpointAndFormat17Migration();
        testFormat18AddsEditorBindings();
        testFormat20AddsScreenPreviewBinding();
        testFormat21ConsolidatesInteractBinding();
        testFormat22UpdatesOverworldViewBinding();
        testStoreBackupsAndRecovery();
        testInterruptedWriteRecovery();
        testStorageFailuresPreserveCommittedProfile();
        testSaveSlotStems();
        testMigrationAndDoubleCorruption();
        testMigrationPersistenceFailuresPreserveDecodedProfiles();
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
