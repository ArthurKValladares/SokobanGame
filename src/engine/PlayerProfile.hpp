#pragma once

#include "engine/Config.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/InputBindings.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

inline constexpr int currentPlayerProfileFormat = 4;

struct PlayerProfile {
    struct LevelProgress {
        int level = 0;
        bool completed = false;
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
    void recordLevelCompletion(
        int level,
        int moves,
        std::optional<double> completionTimeSeconds,
        bool unlockNextLevel);

    [[nodiscard]] const LevelProgress* progressForLevel(int level) const;
    [[nodiscard]] std::string serialize() const;

    bool operator==(const PlayerProfile&) const = default;
};

struct DecodedPlayerProfile {
    PlayerProfile profile;
    int sourceFormat = currentPlayerProfileFormat;
};

// Throws std::runtime_error for malformed, unsupported, or semantically
// invalid profile data. Formats 1 through 3 migrate into the current model.
[[nodiscard]] DecodedPlayerProfile decodePlayerProfile(std::string_view text);

} // namespace sokoban
