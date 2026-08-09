#include "engine/CampaignSession.hpp"

#include <algorithm>
#include <utility>

namespace sokoban {

void CampaignSession::setLevelScreenCounts(std::vector<int> screenCounts)
{
    levelScreenCounts_ = std::move(screenCounts);
}

void CampaignSession::setOverworldTargets(std::vector<LevelLocation> targets)
{
    std::ranges::sort(targets, [](LevelLocation left, LevelLocation right) {
        return std::pair { left.level, left.screen } <
            std::pair { right.level, right.screen };
    });
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    overworldTargets_ = std::move(targets);
}

bool CampaignSession::restoreProfileLocation(PlayerProfile& profile)
{
    clearRunState();
    if (profile.worldContext == PlayerProfile::WorldContext::Overworld) {
        inOverworld_ = true;
        return true;
    }

    const LevelLocation saved {
        .level = profile.currentLevel,
        .screen = profile.currentScreen,
    };
    if (!screenExists(saved.level, saved.screen) || !profile.activeScreen) {
        profile.worldContext = PlayerProfile::WorldContext::Overworld;
        profile.activeScreen.reset();
        inOverworld_ = true;
        return false;
    }
    current_ = saved;
    inOverworld_ = false;
    puzzleElapsedSeconds_ = profile.activeScreen->levelElapsedSeconds;
    return true;
}

void CampaignSession::resetForProfile(PlayerProfile& profile)
{
    (void)restoreProfileLocation(profile);
}

void CampaignSession::startNewGame(PlayerProfile& profile)
{
    profile.resetProgress();
    clearRunState();
    inOverworld_ = true;
    profile.worldContext = PlayerProfile::WorldContext::Overworld;
}

bool CampaignSession::startPuzzle(
    PlayerProfile& profile,
    LevelLocation location)
{
    if (!screenExists(location.level, location.screen)) {
        return false;
    }
    current_ = location;
    inOverworld_ = false;
    puzzleElapsedSeconds_ = 0.0;
    profile.worldContext = PlayerProfile::WorldContext::Puzzle;
    profile.activeScreen.reset();
    profile.setCurrentScreen(location.level, location.screen);
    return true;
}

bool CampaignSession::enterSelector(
    PlayerProfile& profile,
    LevelLocation target,
    const GameplaySession::Snapshot& overworldSnapshot)
{
    if (!inOverworld_ || !screenExists(target.level, target.screen)) {
        return false;
    }
    profile.overworldSession = overworldSnapshot;
    return startPuzzle(profile, target);
}

const Level::ScreenSelector* CampaignSession::selectorForInteraction(
    const Level& level,
    const GameState& state)
{
    if (state.players.empty()) {
        return nullptr;
    }

    const Level::ScreenSelector* sharedSelector = nullptr;
    for (const GameState::Player& player : state.players) {
        if (player.dead) {
            return nullptr;
        }
        const Level::ScreenSelector* selector = level.selectorAt(player.cell);
        if (!selector || (sharedSelector && selector->id != sharedSelector->id)) {
            return nullptr;
        }
        sharedSelector = selector;
    }
    return sharedSelector;
}

CampaignSession::WorldRestore CampaignSession::prepareWorldLoad(
    const PlayerProfile& profile)
{
    if (inOverworld_) {
        return {
            .snapshot = profile.overworldSession,
            .checkpointMatched = profile.overworldSession.has_value(),
        };
    }
    if (!profile.activeScreen ||
        profile.activeScreen->level != current_.level ||
        profile.activeScreen->screen != current_.screen) {
        return {};
    }
    puzzleElapsedSeconds_ = profile.activeScreen->levelElapsedSeconds;
    return {
        .snapshot = profile.activeScreen->session,
        .checkpointMatched = true,
    };
}

void CampaignSession::finishWorldLoad(PlayerProfile& profile)
{
    if (inOverworld_) {
        profile.worldContext = PlayerProfile::WorldContext::Overworld;
        profile.activeScreen.reset();
    } else {
        profile.worldContext = PlayerProfile::WorldContext::Puzzle;
        profile.setCurrentScreen(current_.level, current_.screen);
    }
    gameLoaded_ = true;
}

CampaignSession::PuzzleCompleted CampaignSession::completePuzzle(
    PlayerProfile& profile,
    int moveCount,
    bool recordBests)
{
    const PlayerProfile::ScreenProgress* progress =
        profile.progressForScreen(current_);
    const std::optional<int> previousBestMoves =
        progress ? progress->bestMoves : std::nullopt;
    const std::optional<double> previousBestTime =
        progress ? progress->bestTimeSeconds : std::nullopt;
    const int moves = std::max(moveCount, 0);
    profile.recordScreenCompletion(
        current_, moves, puzzleElapsedSeconds_, recordBests);
    const LevelLocation completedLocation = current_;
    profile.activeScreen.reset();
    profile.worldContext = PlayerProfile::WorldContext::Overworld;
    inOverworld_ = true;
    deferredCheckpointPending_ = false;
    deferredCheckpointAgeSeconds_ = 0.0;
    const PuzzleCompleted completed {
        .location = completedLocation,
        .moves = moves,
        .timeSeconds = puzzleElapsedSeconds_,
        .previousBestMoves = previousBestMoves,
        .previousBestTimeSeconds = previousBestTime,
        .newBestMoves = recordBests &&
            (!previousBestMoves || moves < *previousBestMoves),
        .newBestTime = recordBests &&
            (!previousBestTime || puzzleElapsedSeconds_ < *previousBestTime),
        .gameCompleted = allTargetsCompleted(profile),
    };
    puzzleElapsedSeconds_ = 0.0;
    return completed;
}

void CampaignSession::addElapsedTime(float dt)
{
    if (!inOverworld_) {
        puzzleElapsedSeconds_ +=
            static_cast<double>(std::max(dt, 0.0f));
    }
}

void CampaignSession::writeCheckpoint(
    PlayerProfile& profile,
    const GameplaySession::Snapshot& snapshot)
{
    if (inOverworld_) {
        profile.worldContext = PlayerProfile::WorldContext::Overworld;
        profile.activeScreen.reset();
        profile.overworldSession = snapshot;
    } else {
        profile.worldContext = PlayerProfile::WorldContext::Puzzle;
        profile.setCurrentScreen(current_.level, current_.screen);
        profile.activeScreen = PlayerProfile::ActiveScreen {
            .level = current_.level,
            .screen = current_.screen,
            .completedLevelMoveCount = 0,
            .levelElapsedSeconds = puzzleElapsedSeconds_,
            .session = snapshot,
        };
    }
    deferredCheckpointPending_ = false;
    deferredCheckpointAgeSeconds_ = 0.0;
}

bool CampaignSession::deferCheckpoint()
{
    if (!deferredCheckpointPending_) {
        deferredCheckpointPending_ = true;
        deferredCheckpointAgeSeconds_ = 0.0;
    }
    return deferredCheckpointAgeSeconds_ >= autosaveIntervalSeconds_;
}

bool CampaignSession::updateDeferredCheckpoint(
    float dt,
    bool gameplayMoving,
    bool playingDraft)
{
    if (!deferredCheckpointPending_ || playingDraft) {
        return false;
    }
    deferredCheckpointAgeSeconds_ +=
        static_cast<double>(std::max(dt, 0.0f));
    return deferredCheckpointAgeSeconds_ >= autosaveIntervalSeconds_ &&
        !gameplayMoving;
}

bool CampaignSession::allTargetsCompleted(const PlayerProfile& profile) const
{
    return !overworldTargets_.empty() && std::ranges::all_of(
        overworldTargets_,
        [&](LevelLocation target) {
            return profile.screenCompleted(target);
        });
}

bool CampaignSession::screenExists(int level, int screen) const
{
    return levelLocationExists(
        levelScreenCounts_,
        { .level = level, .screen = screen });
}

int CampaignSession::levelCount() const
{
    return static_cast<int>(levelScreenCounts_.size());
}

int CampaignSession::screenCount(int level) const
{
    if (level < 0 || level >= levelCount()) {
        return 0;
    }
    return levelScreenCounts_[static_cast<std::size_t>(level)];
}

void CampaignSession::clearRunState()
{
    current_ = {};
    puzzleElapsedSeconds_ = 0.0;
    deferredCheckpointAgeSeconds_ = 0.0;
    deferredCheckpointPending_ = false;
    inOverworld_ = true;
    gameLoaded_ = false;
}

} // namespace sokoban
