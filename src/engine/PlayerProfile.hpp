#pragma once

#include "engine/Config.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/SettingsTypes.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

inline constexpr int currentPlayerProfileFormat = 9;

// Which top-level sections serialize() writes. Save-slot files carry only
// progress and the shared settings file only settings; both sections are
// optional on read (missing sections decode as defaults).
enum class ProfileSections {
    All,
    ProgressOnly,
    SettingsOnly,
};

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

    using AudioSettings = UserSettings::Audio;
    using VideoSettings = UserSettings::Video;
    using AccessibilitySettings = UserSettings::Accessibility;

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
    UserSettings settings;

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
    [[nodiscard]] std::string serialize(
        ProfileSections sections = ProfileSections::All) const;

    bool operator==(const PlayerProfile&) const = default;
};

struct DecodedPlayerProfile {
    PlayerProfile profile;
    int sourceFormat = currentPlayerProfileFormat;
};

// Throws std::runtime_error for malformed, unsupported, or semantically
// invalid profile data. Formats 1 through 8 migrate through forward JSON
// patches followed by one strict current-format parse.
[[nodiscard]] DecodedPlayerProfile decodePlayerProfile(std::string_view text);

} // namespace sokoban
