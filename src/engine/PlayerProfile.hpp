#pragma once

#include "engine/Config.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/InputBindings.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

inline constexpr int currentPlayerProfileFormat = 8;

struct PlayerProfile {
    struct LevelProgress {
        int level = 0;
        bool completed = false;
        // Number of screens the player has entered in this level (max screen
        // index + 1). Completed levels are treated as fully reachable even if
        // this predates format 8 and reads back as zero.
        int reachedScreens = 0;
        std::optional<int> bestMoves;
        std::optional<double> bestTimeSeconds;

        bool operator==(const LevelProgress&) const = default;
    };

    struct AudioSettings {
        float masterVolume = config::masterVolume;
        float musicVolume = config::musicVolume;
        float soundVolume = 1.0f;

        bool operator==(const AudioSettings&) const = default;
    };

    struct VideoSettings {
        bool fullscreen = false;
        // False preserves the engine's mailbox-first presentation behavior.
        bool vsync = false;
        int antiAliasingSamples = 8;
        int renderScalePercent = 100;
        bool customRenderScale = false;
        int customRenderScalePercent = 100;
        bool ambientOcclusion = config::ambientOcclusionEnabled;
        int windowWidth = 1280;
        int windowHeight = 720;

        [[nodiscard]] int effectiveRenderScalePercent() const;
        bool operator==(const VideoSettings&) const = default;
    };

    struct AccessibilitySettings {
        bool reducedMotion = false;
        bool highContrast = false;
        bool largeText = false;
        bool subtitles = true;
        bool screenShake = true;

        bool operator==(const AccessibilitySettings&) const = default;
    };

    struct ActiveScreen {
        int level = 0;
        int screen = 0;
        int completedLevelMoveCount = 0;
        double levelElapsedSeconds = 0.0;
        GameplaySession::Snapshot session;

        bool operator==(const ActiveScreen&) const = default;
    };

    int unlockedLevel = 0;
    int currentLevel = 0;
    int currentScreen = 0;
    std::vector<LevelProgress> levels;
    std::optional<ActiveScreen> activeScreen;
    AudioSettings audio;
    VideoSettings video;
    InputBindings input = defaultInputBindings();
    AccessibilitySettings accessibility;

    void normalize();
    void setCurrentLevel(int level);
    void setCurrentScreen(int level, int screen);
    // Records that the player entered a screen, for level-select unlocking.
    void recordReachedScreen(int level, int screen);
    // Clears all progress (levels, unlocks, checkpoint) while keeping audio,
    // video, input, and accessibility settings.
    void resetProgress();
    // True when the profile carries no progress at all (fresh or reset).
    [[nodiscard]] bool progressEmpty() const;
    // A copy with default progress and this profile's settings; the shared
    // settings file stores exactly this shape.
    [[nodiscard]] PlayerProfile settingsOnly() const;
    // Adopts the settings sections of `other`, keeping this progress.
    void adoptSettingsFrom(const PlayerProfile& other);
    // recordBests is false for runs that did not start at the level's first
    // screen (level-select jumps); completion still counts, records do not.
    void recordLevelCompletion(
        int level,
        int moves,
        std::optional<double> completionTimeSeconds,
        bool unlockNextLevel,
        bool recordBests = true);

    [[nodiscard]] const LevelProgress* progressForLevel(int level) const;
    [[nodiscard]] std::string serialize() const;

    bool operator==(const PlayerProfile&) const = default;
};

struct DecodedPlayerProfile {
    PlayerProfile profile;
    int sourceFormat = currentPlayerProfileFormat;
};

// Throws std::runtime_error for malformed, unsupported, or semantically
// invalid profile data. Formats 1 through 7 migrate into the current model.
[[nodiscard]] DecodedPlayerProfile decodePlayerProfile(std::string_view text);

} // namespace sokoban
