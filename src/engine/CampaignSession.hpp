#pragma once

#include "engine\GameplaySession.hpp"
#include "engine\LevelCatalog.hpp"
#include "engine\PlayerProfile.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace sokoban {

// Headless campaign-level state. Owns location, cross-screen counters,
// checkpoint cadence, and completion policy while Application performs file,
// renderer, audio, UI, and persistence effects around its decisions.
class CampaignSession {
public:
    struct ScreenRestore {
        std::optional<GameplaySession::Snapshot> snapshot;
        bool checkpointMatched = false;
    };

    struct ScreenAdvanced {};

    struct LevelCompleted {
        int level = 0;
        int moves = 0;
        double timeSeconds = 0.0;
        std::optional<int> previousBestMoves;
        std::optional<double> previousBestTimeSeconds;
        bool newBestMoves = false;
        bool newBestTime = false;
        bool hasNextLevel = false;
    };

    struct GameCompleted {
        LevelCompleted finalLevel;
    };

    using AdvanceResult =
        std::variant<ScreenAdvanced, LevelCompleted, GameCompleted>;

    void setLevelScreenCounts(std::vector<int> screenCounts);
    [[nodiscard]] bool restoreProfileLocation(PlayerProfile& profile);
    void resetForProfile(PlayerProfile& profile);

    void startNewGame(PlayerProfile& profile);
    [[nodiscard]] bool startLevel(
        PlayerProfile& profile,
        int level,
        int screen);
    [[nodiscard]] ScreenRestore prepareScreenLoad(
        const PlayerProfile& profile);
    void finishScreenLoad(PlayerProfile& profile);
    void markWorldLoaded() { gameLoaded_ = true; }

    [[nodiscard]] AdvanceResult advanceScreen(
        PlayerProfile& profile,
        int screenMoveCount);
    void resolveLevelComplete(PlayerProfile& profile);

    void addElapsedTime(float dt);
    void writeCheckpoint(
        PlayerProfile& profile,
        const GameplaySession::Snapshot& snapshot);
    [[nodiscard]] bool deferCheckpoint();
    [[nodiscard]] bool updateDeferredCheckpoint(
        float dt,
        bool gameplayMoving,
        bool playingDraft);

    [[nodiscard]] bool allLevelsCompleted(
        const PlayerProfile& profile) const;
    [[nodiscard]] bool screenExists(int level, int screen) const;
    [[nodiscard]] int levelCount() const;
    [[nodiscard]] int screenCount(int level) const;

    [[nodiscard]] int currentLevel() const { return current_.level; }
    [[nodiscard]] int currentScreen() const { return current_.screen; }
    [[nodiscard]] LevelLocation location() const { return current_; }
    [[nodiscard]] bool gameLoaded() const { return gameLoaded_; }
    [[nodiscard]] int completedLevelMoveCount() const
    {
        return completedLevelMoveCount_;
    }
    [[nodiscard]] double levelElapsedSeconds() const
    {
        return levelElapsedSeconds_;
    }

private:
    static constexpr double autosaveIntervalSeconds_ = 2.0;

    void clearRunState();

    std::vector<int> levelScreenCounts_;
    LevelLocation current_ {};
    std::optional<int> pendingNextLevel_;
    int completedLevelMoveCount_ = 0;
    double levelElapsedSeconds_ = 0.0;
    double deferredCheckpointAgeSeconds_ = 0.0;
    bool deferredCheckpointPending_ = false;
    bool levelRunFromStart_ = true;
    bool gameLoaded_ = false;
};

} // namespace sokoban
