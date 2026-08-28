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
    check(progress != nullptr && progress->completed, "completion status recorded");
    check(progress != nullptr && progress->bestMoves == 30, "worse move count ignored");
    check(progress != nullptr && progress->bestTimeSeconds == 40.0, "better time recorded independently");

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    check(decoded.sourceFormat == sokoban::currentPlayerProfileFormat, "current format decoded");
    check(decoded.profile == profile, "current profile round-trips");
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
    check(first != nullptr && first->reachedScreens == 3, "reached screens track the max");
    check(first != nullptr && !first->completed, "reaching screens does not complete");
    const sokoban::PlayerProfile::LevelProgress* second = profile.progressForLevel(1);
    check(second != nullptr && second->reachedScreens == 1, "second level entry created");
    check(profile.progressForLevel(-1) == nullptr, "negative level ignored");

    const sokoban::DecodedPlayerProfile decoded =
        sokoban::decodePlayerProfile(profile.serialize());
    check(decoded.profile == profile, "reached screens round-trip");

    // Format-7 files (no reachedScreens) decode with zeroed counts.
    nlohmann::json legacy = nlohmann::json::parse(profile.serialize());
    legacy["format"] = 7;
    for (auto& item : legacy["progress"]["levels"]) {
        item.erase("reachedScreens");
    }
    const sokoban::DecodedPlayerProfile migrated =
        sokoban::decodePlayerProfile(legacy.dump());
    check(migrated.sourceFormat == 7, "format 7 source reported");
    const sokoban::PlayerProfile::LevelProgress* migratedFirst =
        migrated.profile.progressForLevel(0);
    check(migratedFirst != nullptr && migratedFirst->reachedScreens == 0,
        "format 7 migration defaults reached screens to zero");

    // Completing without recordBests keeps completion but no records.
    profile.recordLevelCompletion(1, 12, 5.0, true, false);
    const sokoban::PlayerProfile::LevelProgress* partial = profile.progressForLevel(1);
    check(partial != nullptr && partial->completed, "partial run still completes");
    check(partial != nullptr && !partial->bestMoves && !partial->bestTimeSeconds,
        "partial run records no bests");

    sokoban::PlayerProfile populated = profile;
    populated.settings.audio.musicVolume = 0.25f;
    check(!populated.progressEmpty(), "populated profile has progress");
    populated.resetProgress();
    check(populated.unlockedLevel == 0 && populated.currentLevel == 0 &&
            populated.currentScreen == 0,
        "reset clears position");
    check(populated.levels.empty() && !populated.activeScreen, "reset clears records");
    check(populated.settings.audio.musicVolume == 0.25f, "reset keeps audio settings");
    check(populated.progressEmpty(), "reset profile reads as empty");

    // Settings split: settingsOnly strips progress, adoptSettingsFrom keeps it.
    const sokoban::PlayerProfile settings = populated.settingsOnly();
    check(settings.progressEmpty(), "settingsOnly has no progress");
    check(settings.settings.audio.musicVolume == 0.25f, "settingsOnly keeps audio");

    sokoban::PlayerProfile target = profile;
    const int levelsBefore = static_cast<int>(target.levels.size());
    target.adoptSettingsFrom(settings);
    check(target.settings.audio.musicVolume == 0.25f, "adopt applies audio settings");
    check(static_cast<int>(target.levels.size()) == levelsBefore,
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
    check(progressOnly.find("\"settings\"") == std::string::npos,
        "progress-only file has no settings section");
    const sokoban::PlayerProfile progressDecoded =
        sokoban::decodePlayerProfile(progressOnly).profile;
    check(progressDecoded.progressForLevel(0) != nullptr &&
            progressDecoded.progressForLevel(0)->bestMoves == 12,
        "progress-only round-trips progress");
    check(progressDecoded.settings.audio.musicVolume ==
            sokoban::PlayerProfile {}.settings.audio.musicVolume,
        "progress-only decodes default settings");

    // Settings-only files carry no progress section.
    const std::string settingsOnly =
        profile.serialize(sokoban::ProfileSections::SettingsOnly);
    check(settingsOnly.find("\"progress\"") == std::string::npos,
        "settings-only file has no progress section");
    const sokoban::PlayerProfile settingsDecoded =
        sokoban::decodePlayerProfile(settingsOnly).profile;
    check(settingsDecoded.progressEmpty(), "settings-only decodes empty progress");
    check(settingsDecoded.settings.audio.musicVolume == 0.25f,
        "settings-only round-trips settings");

    // A bare format-9 document decodes as a fully default profile.
    const sokoban::PlayerProfile bare =
        sokoban::decodePlayerProfile("{\"format\": 9}").profile;
    check(bare == sokoban::PlayerProfile {}, "sections are optional on read");
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
    check(decoded.profile == profile, "active screen checkpoint round-trips exactly");
    check(decoded.profile.currentScreen == 3, "current screen round-trips");
    check(decoded.profile.activeScreen->session.undoStack.size() == 1,
        "undo stack round-trips");
    check(decoded.profile.activeScreen->session.state == after,
        "exact committed game state round-trips");

    const nlohmann::json current = nlohmann::json::parse(serialized);
    check(current["progress"]["activeScreen"]["session"]["state"]
            .contains("players"),
        "checkpoint state uses the players array");
    check(current["progress"]["activeScreen"]["session"]["state"]
            .contains("enemies"),
        "checkpoint state persists enemies");
    check(!current["progress"]["activeScreen"]["session"]["state"]
            .contains("playerClones"),
        "checkpoint state has no primary/clone compatibility fields");
    check(current["progress"]["activeScreen"]["session"]["undoStack"][0]
            ["presentation"]["animations"].size() == 2,
        "undo presentation timeline is persisted");

    nlohmann::json format15 = current;
    format15["format"] = 15;
    format15["progress"]["activeScreen"]["session"]["undoStack"][0]
        .erase("presentation");
    const sokoban::DecodedPlayerProfile migrated15 =
        sokoban::decodePlayerProfile(format15.dump());
    check(migrated15.profile.activeScreen.has_value(),
        "format 15 migration preserves the active checkpoint");
    check(!migrated15.profile.activeScreen->session.undoStack[0]
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
    check(migrated13.sourceFormat == 13,
        "format 13 checkpoint source is reported");
    check(!migrated13.profile.activeScreen,
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
    check(profile.currentLevel == 9,
        "current level is independent of legacy unlock progression");
    check(profile.settings.audio.masterVolume == 0.0f, "master volume clamps low");
    check(profile.settings.audio.musicVolume == 1.0f, "music volume clamps high");
    check(profile.settings.video.antiAliasingSamples ==
            sokoban::config::antiAliasingSamples,
        "invalid MSAA receives default");
    check(profile.settings.video.renderScalePercent == 100, "invalid render scale receives default");
    check(profile.settings.video.customRenderScalePercent == 25,
        "custom render scale clamps to its minimum");
    check(profile.settings.video.effectiveRenderScalePercent() == 25,
        "enabled custom render scale is effective");
    check(profile.settings.video.exposureEv ==
            sokoban::minimumExposureEv,
        "exposure clamps to its safe minimum");
    check(profile.settings.video.windowWidth == 640, "window width clamps low");
    check(profile.settings.video.windowHeight == 480, "window height clamps low");

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
    check(migrated.profile.settings.audio.soundVolume == 1.0f, "new setting receives migration default");
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
    format2Root["settings"]["video"].erase("antiAliasingSamples");
    format2Root["settings"]["video"].erase("renderScalePercent");
    format2Root["settings"]["video"].erase("customRenderScale");
    format2Root["settings"]["video"].erase("customRenderScalePercent");
    format2Root["settings"]["video"].erase("ambientOcclusion");
    format2Root["settings"]["video"].erase("windowWidth");
    format2Root["settings"]["video"].erase("windowHeight");
    const sokoban::DecodedPlayerProfile migratedFormat2 =
        sokoban::decodePlayerProfile(format2Root.dump());
    check(migratedFormat2.sourceFormat == 2, "format 2 source reported");
    check(migratedFormat2.profile.currentScreen == 0,
        "format 2 receives default screen");
    check(!migratedFormat2.profile.activeScreen,
        "format 2 receives no gameplay checkpoint");
    const sokoban::KeyboardBinding* migratedKeyboard = keyboardBinding(
        migratedFormat2.profile.settings.input, sokoban::InputAction::MoveUp);
    check(migratedKeyboard && migratedKeyboard->scancode == "Up",
        "format 2 keyboard binding migrates");
    check(migratedFormat2.profile.settings.input.forAction(
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
    check(migratedFormat3.sourceFormat == 3, "format 3 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat3.profile.settings.input, sokoban::InputAction::Undo);
    check(migratedKeyboard && migratedKeyboard->scancode == "Backspace",
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
    check(migratedFormat4.sourceFormat == 4, "format 4 source reported");
    check(!migratedFormat4.profile.settings.input.forAction(
            sokoban::InputAction::MenuConfirm).empty(),
        "format 4 receives menu-confirm defaults");
    check(migratedFormat4.profile.settings.video.antiAliasingSamples ==
            sokoban::config::antiAliasingSamples,
        "format 4 receives MSAA default");
    check(migratedFormat4.profile.settings.video.windowWidth == 1280 &&
            migratedFormat4.profile.settings.video.windowHeight == 720,
        "format 4 receives window-size defaults");

    nlohmann::json format24 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format24["format"] = 24;
    format24["settings"]["video"].erase("allowTearing");
    format24["settings"]["video"].erase("frameRateLimit");
    const sokoban::DecodedPlayerProfile migratedFormat24 =
        sokoban::decodePlayerProfile(format24.dump());
    check(migratedFormat24.sourceFormat == 24,
        "format 24 source reported");
    check(!migratedFormat24.profile.settings.video.allowTearing,
        "format 24 receives safe tearing default");
    check(migratedFormat24.profile.settings.video.frameRateLimit == 0,
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
    check(migratedFormat25.sourceFormat == 25,
        "format 25 source reported");
    check(migratedFormat25.profile.serialize().find("\"accessibility\"") ==
            std::string::npos,
        "format 25 accessibility settings are removed during migration");

    nlohmann::json format26 = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format26["format"] = 26;
    format26["settings"]["video"].erase("exposureEv");
    const sokoban::DecodedPlayerProfile migratedFormat26 =
        sokoban::decodePlayerProfile(format26.dump());
    check(migratedFormat26.sourceFormat == 26,
        "format 26 source reported");
    check(migratedFormat26.profile.settings.video.exposureEv == 0.0f,
        "format 26 receives neutral exposure");

    nlohmann::json format5Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format5Root["format"] = 5;
    format5Root["settings"]["video"].erase("renderScalePercent");
    format5Root["settings"]["video"].erase("customRenderScale");
    format5Root["settings"]["video"].erase("customRenderScalePercent");
    const sokoban::DecodedPlayerProfile migratedFormat5 =
        sokoban::decodePlayerProfile(format5Root.dump());
    check(migratedFormat5.sourceFormat == 5, "format 5 source reported");
    check(migratedFormat5.profile.settings.video.renderScalePercent == 100,
        "format 5 receives native render scale");

    nlohmann::json format6Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format6Root["format"] = 6;
    format6Root["settings"]["video"].erase("customRenderScale");
    format6Root["settings"]["video"].erase("customRenderScalePercent");
    const sokoban::DecodedPlayerProfile migratedFormat6 =
        sokoban::decodePlayerProfile(format6Root.dump());
    check(migratedFormat6.sourceFormat == 6, "format 6 source reported");
    check(!migratedFormat6.profile.settings.video.customRenderScale,
        "format 6 defaults to preset render scale");
    check(migratedFormat6.profile.settings.video.customRenderScalePercent == 100,
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
    check(migratedFormat9.sourceFormat == 9, "format 9 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat9.profile.settings.input, sokoban::InputAction::Undo);
    check(migratedKeyboard && migratedKeyboard->scancode == "Z",
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
    check(migratedFormat10.sourceFormat == 10, "format 10 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat10.profile.settings.input, sokoban::InputAction::Undo);
    check(migratedKeyboard && migratedKeyboard->scancode == "Z",
        "format 10 default undo returns to Z");

    format10Root["settings"]["input"]["mirror"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "G" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    const sokoban::DecodedPlayerProfile migratedCustomFormat10 =
        sokoban::decodePlayerProfile(format10Root.dump());
    const nlohmann::json migratedCustomFormat10Json = nlohmann::json::parse(
        migratedCustomFormat10.profile.serialize());
    check(!migratedCustomFormat10Json["settings"]["input"].contains("mirror"),
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
    check(migratedFormat11.sourceFormat == 11, "format 11 source reported");
    migratedKeyboard = keyboardBinding(
        migratedFormat11.profile.settings.input,
        sokoban::InputAction::ShowTopDownView);
    check(migratedKeyboard && migratedKeyboard->scancode == "T",
        "format 11 receives current-screen top-down default");
    migratedKeyboard = keyboardBinding(
        migratedFormat11.profile.settings.input, sokoban::InputAction::Undo);
    check(migratedKeyboard && migratedKeyboard->scancode == "Z",
        "format 11 binding displaced by T recovers its default");

    nlohmann::json format12Root = nlohmann::json::parse(
        sokoban::PlayerProfile {}.serialize());
    format12Root["format"] = 12;
    format12Root["settings"]["video"].erase("ambientOcclusionStrength");
    const sokoban::DecodedPlayerProfile migratedFormat12 =
        sokoban::decodePlayerProfile(format12Root.dump());
    check(migratedFormat12.sourceFormat == 12,
        "format 12 source reported");
    check(migratedFormat12.profile.settings.video.ambientOcclusionStrength ==
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
    check(progression.selectorStatus({ .level = 0, .screen = 0 }) ==
            sokoban::ScreenSelectorStatus::Playable,
        "screen zero is immediately playable");
    check(progression.selectorStatus({ .level = 0, .screen = 1 }) ==
            sokoban::ScreenSelectorStatus::Unavailable,
        "later screen waits for its predecessor");
    check(progression.selectorStatus({ .level = 7, .screen = 0 }) ==
            sokoban::ScreenSelectorStatus::Playable,
        "a different level's first screen is independently playable");
    progression.recordScreenCompletion({ .level = 0, .screen = 0 }, 3, 2.0);
    check(progression.selectorStatus({ .level = 0, .screen = 0 }) ==
            sokoban::ScreenSelectorStatus::Solved,
        "completed screen is solved");
    check(progression.selectorStatus({ .level = 0, .screen = 1 }) ==
            sokoban::ScreenSelectorStatus::Playable,
        "solving a screen unlocks only its successor");
    check(progression.selectorStatus({ .level = 0, .screen = 2 }) ==
            sokoban::ScreenSelectorStatus::Unavailable,
        "unlocking does not skip a screen");

    sokoban::PlayerProfile profile;
    profile.recordScreenCompletion({ .level = 2, .screen = 3 }, 18, 12.5);
    profile.recordScreenCompletion({ .level = 2, .screen = 3 }, 14, 13.0);
    profile.recordScreenCompletion({ .level = 1, .screen = 0 }, 7, 4.0);
    check(profile.screenCompleted({ .level = 2, .screen = 3 }),
        "screen completion is queryable");
    check(profile.progressForScreen({ .level = 2, .screen = 3 })->bestMoves == 14,
        "screen best moves improve independently");
    check(profile.progressForScreen({ .level = 2, .screen = 3 })->bestTimeSeconds == 12.5,
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
    check(decoded.profile == profile,
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
    check(migrated.sourceFormat == 17, "format 17 source is reported");
    check(migrated.profile.screenCompleted({ .level = 4, .screen = 0 }) &&
            migrated.profile.screenCompleted({ .level = 4, .screen = 1 }),
        "format 17 reached screens migrate as completed");
    check(!migrated.profile.progressForScreen({ .level = 4, .screen = 0 })
                ->bestMoves,
        "legacy aggregate best is not copied to an individual screen");
    check(migrated.profile.worldContext ==
            sokoban::PlayerProfile::WorldContext::Overworld,
        "format 17 without an active checkpoint resumes in overworld");

    nlohmann::json format19 = nlohmann::json::parse(profile.serialize());
    format19["format"] = 19;
    format19["progress"]["overworldSession"] =
        format19["progress"]["overworldCheckpoint"]["session"];
    format19["progress"].erase("overworldCheckpoint");
    const sokoban::DecodedPlayerProfile migrated19 =
        sokoban::decodePlayerProfile(format19.dump());
    check(migrated19.sourceFormat == 19,
        "format 19 source is reported");
    check(!migrated19.profile.overworldCheckpoint,
        "format 19 single-overworld checkpoint is safely discarded");
    check(migrated19.profile.screenCompleted({ .level = 2, .screen = 3 }),
        "format 19 puzzle progress survives checkpoint migration");
}

void testStoreBackupsAndRecovery()
{
    TemporaryDirectory temporary;
    sokoban::SaveStore store(temporary.path());
    sokoban::SaveStore::LoadResult created = store.load();
    check(created.disposition == sokoban::SaveStore::LoadDisposition::CreatedDefault,
        "missing profile returns defaults");
    check(!std::filesystem::is_regular_file(store.primaryPath()),
        "fresh start writes no file");

    sokoban::PlayerProfile first = created.profile;
    first.unlockedLevel = 1;
    first.setCurrentLevel(1);
    first.settings.audio.musicVolume = 0.25f;
    check(store.save(first), "first profile saves");

    sokoban::PlayerProfile second = first;
    second.settings.audio.musicVolume = 0.75f;
    check(store.save(second), "second profile saves");
    check(std::filesystem::is_regular_file(store.backupPath()), "backup written");
    check(sokoban::decodePlayerProfile(
        [&] {
            std::ifstream stream(store.backupPath(), std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }()).profile.settings.audio.musicVolume == 0.25f,
        "backup contains prior valid profile");

    writeFile(store.primaryPath(), "{ definitely not json");
    const sokoban::SaveStore::LoadResult recovered = store.load();
    check(recovered.disposition == sokoban::SaveStore::LoadDisposition::RecoveredBackup,
        "corrupt primary recovers backup");
    check(recovered.profile.settings.audio.musicVolume == 0.25f, "recovered backup data returned");
    check(!std::filesystem::exists(store.primaryPath().string() + ".tmp"),
        "recovery leaves no temporary primary");

    bool foundCorruptArchive = false;
    for (const auto& entry : std::filesystem::directory_iterator(temporary.path())) {
        foundCorruptArchive = foundCorruptArchive ||
            entry.path().filename().string().starts_with("profile.json.corrupt-");
    }
    check(foundCorruptArchive, "corrupt primary archived for diagnostics");
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
    check(temporaryRecovered.disposition ==
            sokoban::SaveStore::LoadDisposition::RecoveredInterruptedWrite,
        "valid temporary profile is promoted at startup");
    check(temporaryRecovered.profile == interrupted,
        "temporary recovery returns its saved profile");
    check(!std::filesystem::exists(store.primaryPath().string() + ".tmp"),
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
    check(displacedRecovered.disposition ==
            sokoban::SaveStore::LoadDisposition::RecoveredInterruptedWrite,
        "displaced profile is restored when the live file is absent");
    check(displacedRecovered.profile == displaced,
        "displaced recovery returns its saved profile");
    check(!std::filesystem::exists(
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
    check(fallbackRecovered.disposition ==
            sokoban::SaveStore::LoadDisposition::RecoveredInterruptedWrite,
        "valid displaced profile is used when the temporary file is corrupt");
    check(fallbackRecovered.profile == fallback,
        "displaced fallback returns its saved profile");
    check(!std::filesystem::exists(fallbackStore.primaryPath().string() + ".tmp") &&
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
    check(backupRecovered.disposition == sokoban::SaveStore::LoadDisposition::RecoveredBackup,
        "interrupted backup write remains available for normal backup recovery");
    check(backupRecovered.profile == backup,
        "recovered backup temporary returns its saved profile");
    check(!std::filesystem::exists(backupStore.backupPath().string() + ".tmp") &&
            std::filesystem::is_regular_file(backupStore.primaryPath()),
        "backup recovery removes its artifact and repairs the primary");

    TemporaryDirectory liveDirectory;
    sokoban::SaveStore liveStore(liveDirectory.path());
    sokoban::PlayerProfile live;
    live.unlockedLevel = 4;
    live.setCurrentLevel(4);
    check(liveStore.save(live), "live profile saves before stale-artifact recovery");
    sokoban::PlayerProfile stale = live;
    stale.unlockedLevel = 5;
    stale.setCurrentLevel(5);
    writeFile(liveStore.primaryPath().string() + ".tmp", stale.serialize());
    writeFile(
        liveStore.primaryPath().string() + ".replace-old",
        stale.serialize());

    const sokoban::SaveStore::LoadResult liveLoaded = liveStore.load();
    check(liveLoaded.disposition == sokoban::SaveStore::LoadDisposition::Loaded,
        "valid live profile remains authoritative");
    check(liveLoaded.profile == live,
        "stale artifacts never overwrite a valid live profile");
    check(!std::filesystem::exists(liveStore.primaryPath().string() + ".tmp") &&
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
    check(store.save(committed), "committed profile saves before fault injection");

    sokoban::PlayerProfile replacement = committed;
    replacement.unlockedLevel = 4;
    replacement.setCurrentLevel(4);
    replacement.settings.audio.musicVolume = 0.8f;
    replacement.normalize();

    sokoban::atomicFile::failWriteAfterForTesting(
        0, std::errc::permission_denied);
    check(!store.save(replacement), "permission-denied save reports failure");
    check(store.status().starts_with("Player profile save failed:"),
        "permission-denied save records a diagnostic");
    check(store.load().profile == committed,
        "permission-denied save preserves committed profile");
    check(!std::filesystem::exists(store.primaryPath().string() + ".tmp") &&
            !std::filesystem::exists(store.backupPath().string() + ".tmp"),
        "permission-denied save cleans temporary artifacts");

    // A replacement save writes the prior primary to the backup first. Let
    // that write finish, then simulate ENOSPC while writing the live file.
    sokoban::atomicFile::failWriteAfterForTesting(
        1, std::errc::no_space_on_device);
    check(!store.save(replacement), "disk-full save reports failure");
    check(store.load().profile == committed,
        "disk-full save preserves committed profile");
    check(std::filesystem::is_regular_file(store.backupPath()),
        "disk-full save retains the valid backup");
    check(!std::filesystem::exists(store.primaryPath().string() + ".tmp") &&
            !std::filesystem::exists(store.backupPath().string() + ".tmp"),
        "disk-full save cleans temporary artifacts");
}

void testSaveSlotStems()
{
    TemporaryDirectory directory;
    sokoban::SaveStore first(directory.path()); // historical "profile" stem
    sokoban::SaveStore second(directory.path(), "profile-slot2");
    check(first.primaryPath() != second.primaryPath(),
        "slot stems use separate primaries");
    check(first.backupPath() != second.backupPath(),
        "slot stems use separate backups");

    sokoban::PlayerProfile firstProfile;
    firstProfile.unlockedLevel = 1;
    firstProfile.normalize();
    check(first.save(firstProfile), "slot 1 saves");

    sokoban::PlayerProfile secondProfile;
    secondProfile.settings.audio.musicVolume = 0.25f;
    secondProfile.normalize();
    check(second.save(secondProfile), "slot 2 saves");

    check(first.load().profile == firstProfile, "slot 1 reloads its own data");
    check(second.load().profile == secondProfile, "slot 2 reloads its own data");

    // A corrupt neighbour slot does not disturb this slot's load.
    writeFile(second.primaryPath(), "not json");
    writeFile(second.backupPath(), "also not json");
    check(first.load().profile == firstProfile,
        "slot 1 unaffected by corrupt slot 2");
    const sokoban::SaveStore::LoadResult recovered = second.load();
    check(recovered.disposition == sokoban::SaveStore::LoadDisposition::ResetCorrupt,
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
    first.settings.audio.musicVolume = 0.25f;
    sokoban::PlayerProfile latest = first;
    latest.settings.audio.musicVolume = 0.75f;

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
    check(sokoban::decodePlayerProfile(contents).profile.settings.audio.musicVolume == 0.75f,
        "coalesced save writes newest profile");

    latest.settings.audio.musicVolume = 0.5f;
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
        profile.settings.audio.soundVolume = 0.35f;
        store.requestSave(profile);
    }

    std::ifstream stream(temporary.path() / "profile.json", std::ios::binary);
    const std::string contents {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char> {}
    };
    check(sokoban::decodePlayerProfile(contents).profile.settings.audio.soundVolume == 0.35f,
        "async store destructor flushes newest profile");
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
        check(settings == 1, "added channel gets the next id");

        sokoban::PlayerProfile progress;
        progress.unlockedLevel = 2;
        progress.setCurrentScreen(2, 0);
        progress.normalize();
        sokoban::PlayerProfile config;
        config.settings.audio.musicVolume = 0.2f;
        config.normalize();

        store.requestSave(progress);
        store.requestSave(settings, config);
        store.flush();

        // One worker wrote both channels to their own files.
        check(store.diagnostics(0).completedWrites >= 1, "channel 0 wrote");
        check(store.diagnostics(settings).completedWrites >= 1, "channel 1 wrote");
        check(std::filesystem::is_regular_file(temporary.path() / "profile.json"),
            "progress channel wrote its file");
        check(std::filesystem::is_regular_file(temporary.path() / "settings.json"),
            "settings channel wrote its own file");

        // Each channel round-trips only its own sections.
        check(store.load(0).profile.unlockedLevel == 2, "channel 0 has progress");
        check(store.load(settings).profile.settings.audio.musicVolume == 0.2f,
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
        store.flush();
        check(std::filesystem::is_regular_file(temporary.path() / "profile-slot2.json"),
            "third channel wrote a distinct file");
        store.replaceChannel(
            repointed, temporary.path(), "profile-slot3",
            sokoban::ProfileSections::ProgressOnly);
        check(store.load(repointed).profile.progressEmpty(),
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
    check(migrated.sourceFormat == 18, "format 18 source is reported");
    check(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::EditorReplaceTile) == "R",
        "format 18 receives the editor replace default");
    check(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::EditorDeleteTile) == "D",
        "format 18 receives the editor delete default");
    check(sokoban::actionBindingsDisplay(
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
    check(migrated.sourceFormat == 20, "format 20 source is reported");
    check(sokoban::actionBindingsDisplay(
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
    check(migrated.sourceFormat == 21, "format 21 source is reported");
    check(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::MenuConfirm) ==
            "Space / Pad south",
        "format 21 receives the consolidated interact default");
    const nlohmann::json current = nlohmann::json::parse(
        migrated.profile.serialize());
    check(!current["settings"]["input"].contains("mirror"),
        "format 21 mirror binding is retired");

    format21["settings"]["input"]["mirror"] = nlohmann::json::array({
        nlohmann::json { { "type", "keyboard" }, { "control", "G" } },
        nlohmann::json { { "type", "gamepadButton" }, { "control", "east" } },
    });
    const sokoban::DecodedPlayerProfile migratedMirrorCustom =
        sokoban::decodePlayerProfile(format21.dump());
    check(sokoban::actionBindingsDisplay(
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
    check(sokoban::actionBindingsDisplay(
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
    check(migrated.sourceFormat == 22, "format 22 source is reported");
    check(sokoban::actionBindingsDisplay(
              migrated.profile.settings.input,
              sokoban::InputAction::ShowTopDownView) ==
            "T",
        "format 22 receives the current-screen top-down default");
    check(sokoban::actionBindingsDisplay(
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
    check(sokoban::actionBindingsDisplay(
              custom.profile.settings.input,
              sokoban::InputAction::ShowTopDownView) == "Q",
        "format 22 preserves a customized overview binding");
    check(sokoban::actionBindingsDisplay(
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
    check(sokoban::actionBindingsDisplay(
              split.profile.settings.input,
              sokoban::InputAction::ShowTopDownView) == "T",
        "format 23 combined binding migrates back to T for current screen");
    check(sokoban::actionBindingsDisplay(
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
        testAsyncSaveCoalescingAndFlush();
        testAsyncSaveDestructorFlushesNewestProfile();
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
