#pragma once

#include "engine/GameplaySession.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/PlayerProfile.hpp"

#include <optional>
#include <vector>

namespace sokoban {

// Headless navigation state for the playable overworld and independently
// selected puzzle screens. Application owns file/render/audio/UI effects;
// this class owns context, timing, completion policy, and checkpoints.
class CampaignSession {
public:
    struct WorldRestore {
        std::optional<GameplaySession::Snapshot> snapshot;
        bool checkpointMatched = false;
    };

    struct PuzzleCompleted {
        LevelLocation location {};
        int moves = 0;
        double timeSeconds = 0.0;
        std::optional<int> previousBestMoves;
        std::optional<double> previousBestTimeSeconds;
        bool newBestMoves = false;
        bool newBestTime = false;
        bool gameCompleted = false;
    };

    void setLevelScreenCounts(std::vector<int> screenCounts);
    void setOverworldTargets(std::vector<LevelLocation> targets);
    [[nodiscard]] bool restoreProfileLocation(PlayerProfile& profile);
    void resetForProfile(PlayerProfile& profile);

    void startNewGame(PlayerProfile& profile);
    [[nodiscard]] bool startPuzzle(
        PlayerProfile& profile,
        LevelLocation location);
    [[nodiscard]] bool enterSelector(
        PlayerProfile& profile,
        LevelLocation target,
        const GameplaySession::Snapshot& overworldSnapshot);
    // Activation policy shared by runtime and headless tests: every player
    // must be alive and occupy the same selector cell.
    [[nodiscard]] static const Level::ScreenSelector* selectorForInteraction(
        const Level& level,
        const GameState& state);
    [[nodiscard]] WorldRestore prepareWorldLoad(
        const PlayerProfile& profile);
    void finishWorldLoad(PlayerProfile& profile);
    void markWorldLoaded() { gameLoaded_ = true; }

    [[nodiscard]] PuzzleCompleted completePuzzle(
        PlayerProfile& profile,
        int moveCount,
        bool recordBests = true);

    void addElapsedTime(float dt);
    void writeCheckpoint(
        PlayerProfile& profile,
        const GameplaySession::Snapshot& snapshot);
    [[nodiscard]] bool deferCheckpoint();
    [[nodiscard]] bool updateDeferredCheckpoint(
        float dt,
        bool gameplayMoving,
        bool playingDraft);

    [[nodiscard]] bool allTargetsCompleted(
        const PlayerProfile& profile) const;
    [[nodiscard]] bool screenExists(int level, int screen) const;
    [[nodiscard]] int levelCount() const;
    [[nodiscard]] int screenCount(int level) const;

    [[nodiscard]] bool inOverworld() const { return inOverworld_; }
    [[nodiscard]] int currentLevel() const { return current_.level; }
    [[nodiscard]] int currentScreen() const { return current_.screen; }
    [[nodiscard]] LevelLocation location() const { return current_; }
    [[nodiscard]] bool gameLoaded() const { return gameLoaded_; }
    [[nodiscard]] double puzzleElapsedSeconds() const
    {
        return puzzleElapsedSeconds_;
    }
    [[nodiscard]] const std::vector<LevelLocation>& overworldTargets() const
    {
        return overworldTargets_;
    }

private:
    static constexpr double autosaveIntervalSeconds_ = 2.0;

    void clearRunState();

    std::vector<int> levelScreenCounts_;
    std::vector<LevelLocation> overworldTargets_;
    LevelLocation current_ {};
    double puzzleElapsedSeconds_ = 0.0;
    double deferredCheckpointAgeSeconds_ = 0.0;
    bool deferredCheckpointPending_ = false;
    bool inOverworld_ = true;
    bool gameLoaded_ = false;
};

} // namespace sokoban
