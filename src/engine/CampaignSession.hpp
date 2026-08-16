#pragma once

#include "engine/GameplaySession.hpp"
#include "engine/LevelCatalog.hpp"
#include "engine/OverworldMap.hpp"
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
    void setOverworldTopology(
        uint64_t fingerprint,
        std::vector<OverworldScreenId> screens,
        OverworldScreenId startScreen);
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
    // Returns one deterministic owner only when every player is alive and all
    // occupy the same authored overworld screen. Used for transition admission
    // and checkpoint validation in the composed-map runtime.
    [[nodiscard]] static std::optional<OverworldScreenId> sharedPlayerScreen(
        const OverworldMap& map,
        const GameState& state);
    // Commits navigation metadata after the corresponding gameplay action has
    // committed. The next checkpoint persists this active screen.
    [[nodiscard]] bool transitionOverworldScreen(
        OverworldScreenId destination);
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
    [[nodiscard]] ScreenSelectorViewState selectorViewState(
        const PlayerProfile& profile,
        LevelLocation location) const;
    [[nodiscard]] bool screenExists(int level, int screen) const;
    [[nodiscard]] int levelCount() const;
    [[nodiscard]] int screenCount(int level) const;
    [[nodiscard]] OverworldScreenId activeOverworldScreen() const
    {
        return activeOverworldScreen_;
    }
    [[nodiscard]] OverworldScreenId overworldStartScreen() const
    {
        return overworldStartScreen_;
    }
    [[nodiscard]] uint64_t overworldFingerprint() const
    {
        return overworldFingerprint_;
    }

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
    void validateOverworldCoverage(
        const std::vector<LevelLocation>& targets) const;
    [[nodiscard]] bool overworldScreenExists(
        OverworldScreenId screen) const;
    [[nodiscard]] bool validateOverworldCheckpoint(
        PlayerProfile& profile);

    std::vector<int> levelScreenCounts_;
    std::vector<LevelLocation> overworldTargets_;
    // Screen 1/fingerprint 0 is the temporary identity of the legacy
    // levels/overworld.scr path. Application replaces this with map metadata
    // when runtime integration switches to OverworldMap.
    std::vector<OverworldScreenId> overworldScreens_ { 1 };
    uint64_t overworldFingerprint_ = 0;
    OverworldScreenId overworldStartScreen_ = 1;
    OverworldScreenId activeOverworldScreen_ = 1;
    LevelLocation current_ {};
    double puzzleElapsedSeconds_ = 0.0;
    double deferredCheckpointAgeSeconds_ = 0.0;
    bool deferredCheckpointPending_ = false;
    bool inOverworld_ = true;
    bool gameLoaded_ = false;
};

} // namespace sokoban
