#include "engine\CampaignSession.hpp"

#include <algorithm>
#include <utility>

namespace sokoban {

void CampaignSession::setLevelScreenCounts(std::vector<int> screenCounts)
{
    levelScreenCounts_ = std::move(screenCounts);
}

bool CampaignSession::restoreProfileLocation(PlayerProfile& profile)
{
    const LevelLocation saved {
        .level = profile.currentLevel,
        .screen = profile.currentScreen,
    };
    current_ = resolveSavedLevelLocation(levelScreenCounts_, saved);
    if (current_ == saved) {
        return true;
    }
    profile.setCurrentScreen(current_.level, current_.screen);
    return false;
}

void CampaignSession::resetForProfile(PlayerProfile& profile)
{
    clearRunState();
    (void)restoreProfileLocation(profile);
}

void CampaignSession::startNewGame(PlayerProfile& profile)
{
    profile.resetProgress();
    clearRunState();
    current_ = {};
    profile.setCurrentScreen(0, 0);
}

bool CampaignSession::startLevel(
    PlayerProfile& profile,
    int level,
    int screen)
{
    if (!screenExists(level, screen)) {
        return false;
    }
    if (current_ != LevelLocation { level, screen }) {
        profile.activeScreen.reset();
        completedLevelMoveCount_ = 0;
        levelElapsedSeconds_ = 0.0;
    }
    pendingNextLevel_.reset();
    levelRunFromStart_ = screen == 0;
    current_ = { level, screen };
    profile.setCurrentScreen(level, screen);
    return true;
}

CampaignSession::ScreenRestore CampaignSession::prepareScreenLoad(
    const PlayerProfile& profile)
{
    if (!profile.activeScreen ||
        profile.activeScreen->level != current_.level ||
        profile.activeScreen->screen != current_.screen) {
        return {};
    }

    completedLevelMoveCount_ =
        profile.activeScreen->completedLevelMoveCount;
    levelElapsedSeconds_ = profile.activeScreen->levelElapsedSeconds;
    return {
        .snapshot = profile.activeScreen->session,
        .checkpointMatched = true,
    };
}

void CampaignSession::finishScreenLoad(PlayerProfile& profile)
{
    profile.setCurrentScreen(current_.level, current_.screen);
    profile.recordReachedScreen(current_.level, current_.screen);
    gameLoaded_ = true;
}

CampaignSession::AdvanceResult CampaignSession::advanceScreen(
    PlayerProfile& profile,
    int screenMoveCount)
{
    const int completedMoves = completedLevelMoveCount_ + screenMoveCount;
    if (screenExists(current_.level, current_.screen + 1)) {
        completedLevelMoveCount_ = completedMoves;
        ++current_.screen;
        profile.setCurrentScreen(current_.level, current_.screen);
        return ScreenAdvanced {};
    }

    const bool hasNextLevel = screenExists(current_.level + 1, 0);
    const PlayerProfile::LevelProgress* progress =
        profile.progressForLevel(current_.level);
    const std::optional<int> previousBestMoves =
        progress ? progress->bestMoves : std::nullopt;
    const std::optional<double> previousBestTime =
        progress ? progress->bestTimeSeconds : std::nullopt;
    const LevelCompleted completed {
        .level = current_.level,
        .moves = completedMoves,
        .timeSeconds = levelElapsedSeconds_,
        .previousBestMoves = previousBestMoves,
        .previousBestTimeSeconds = previousBestTime,
        .newBestMoves = levelRunFromStart_ &&
            (!previousBestMoves || completedMoves < *previousBestMoves),
        .newBestTime = levelRunFromStart_ &&
            (!previousBestTime || levelElapsedSeconds_ < *previousBestTime),
        .hasNextLevel = hasNextLevel,
    };
    profile.recordLevelCompletion(
        current_.level,
        completedMoves,
        levelElapsedSeconds_,
        hasNextLevel,
        levelRunFromStart_);
    pendingNextLevel_ = hasNextLevel ? current_.level + 1 : 0;
    if (hasNextLevel) {
        return completed;
    }
    return GameCompleted { .finalLevel = completed };
}

void CampaignSession::resolveLevelComplete(PlayerProfile& profile)
{
    current_ = {
        .level = pendingNextLevel_.value_or(0),
        .screen = 0,
    };
    pendingNextLevel_.reset();
    completedLevelMoveCount_ = 0;
    levelElapsedSeconds_ = 0.0;
    levelRunFromStart_ = true;
    profile.setCurrentScreen(current_.level, current_.screen);
}

void CampaignSession::addElapsedTime(float dt)
{
    levelElapsedSeconds_ += static_cast<double>(std::max(dt, 0.0f));
}

void CampaignSession::writeCheckpoint(
    PlayerProfile& profile,
    const GameplaySession::Snapshot& snapshot)
{
    profile.setCurrentScreen(current_.level, current_.screen);
    profile.activeScreen = PlayerProfile::ActiveScreen {
        .level = current_.level,
        .screen = current_.screen,
        .completedLevelMoveCount = completedLevelMoveCount_,
        .levelElapsedSeconds = levelElapsedSeconds_,
        .session = snapshot,
    };
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

bool CampaignSession::allLevelsCompleted(const PlayerProfile& profile) const
{
    for (int level = 0; level < levelCount(); ++level) {
        const PlayerProfile::LevelProgress* progress =
            profile.progressForLevel(level);
        if (progress == nullptr || !progress->completed) {
            return false;
        }
    }
    return levelCount() > 0;
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
    pendingNextLevel_.reset();
    completedLevelMoveCount_ = 0;
    levelElapsedSeconds_ = 0.0;
    deferredCheckpointAgeSeconds_ = 0.0;
    deferredCheckpointPending_ = false;
    levelRunFromStart_ = true;
    gameLoaded_ = false;
}

} // namespace sokoban
