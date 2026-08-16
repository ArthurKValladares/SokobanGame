#include "engine/PlayerProfile.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sokoban {

void PlayerProfile::normalize()
{
    unlockedLevel = std::max(unlockedLevel, 0);
    // Levels unlock independently through the overworld. The legacy
    // unlockedLevel field may be lower than the selected puzzle's level.
    currentLevel = std::max(currentLevel, 0);
    currentScreen = std::max(currentScreen, 0);
    settings.normalize();
    for (LevelProgress& level : levels) {
        level.reachedScreens = std::max(level.reachedScreens, 0);
    }
    std::ranges::sort(levels, {}, &LevelProgress::level);
    for (ScreenProgress& screen : screens) {
        screen.level = std::max(screen.level, 0);
        screen.screen = std::max(screen.screen, 0);
    }
    std::ranges::sort(screens, [](const ScreenProgress& left,
                                  const ScreenProgress& right) {
        return std::pair { left.level, left.screen } <
            std::pair { right.level, right.screen };
    });
    if (activeScreen &&
        (activeScreen->level != currentLevel || activeScreen->screen != currentScreen)) {
        activeScreen.reset();
    }
}

void PlayerProfile::setCurrentLevel(int level)
{
    currentLevel = std::clamp(level, 0, std::max(unlockedLevel, 0));
    currentScreen = 0;
    activeScreen.reset();
}

void PlayerProfile::setCurrentScreen(int level, int screen)
{
    const int normalizedLevel = std::max(level, 0);
    const int normalizedScreen = std::max(screen, 0);
    if (currentLevel != normalizedLevel || currentScreen != normalizedScreen) {
        activeScreen.reset();
    }
    currentLevel = normalizedLevel;
    currentScreen = normalizedScreen;
}

void PlayerProfile::recordLevelCompletion(
    int level,
    int moves,
    std::optional<double> completionTimeSeconds,
    bool unlockNextLevel,
    bool recordBests)
{
    if (level < 0 || moves < 0 ||
        (completionTimeSeconds &&
            (!std::isfinite(*completionTimeSeconds) || *completionTimeSeconds < 0.0))) {
        throw std::invalid_argument("invalid level completion metrics");
    }

    auto found = std::ranges::find(levels, level, &LevelProgress::level);
    if (found == levels.end()) {
        levels.push_back({ .level = level });
        found = std::prev(levels.end());
    }
    found->completed = true;
    if (recordBests && (!found->bestMoves || moves < *found->bestMoves)) {
        found->bestMoves = moves;
    }
    if (recordBests && completionTimeSeconds &&
        (!found->bestTimeSeconds || *completionTimeSeconds < *found->bestTimeSeconds)) {
        found->bestTimeSeconds = *completionTimeSeconds;
    }
    unlockedLevel = std::max(unlockedLevel, level + (unlockNextLevel ? 1 : 0));
    normalize();
}

void PlayerProfile::recordReachedScreen(int level, int screen)
{
    if (level < 0 || screen < 0) {
        return;
    }
    auto found = std::ranges::find(levels, level, &LevelProgress::level);
    if (found == levels.end()) {
        levels.push_back({ .level = level });
        found = std::prev(levels.end());
    }
    found->reachedScreens = std::max(found->reachedScreens, screen + 1);
    normalize();
}

void PlayerProfile::resetProgress()
{
    unlockedLevel = 0;
    currentLevel = 0;
    currentScreen = 0;
    levels.clear();
    screens.clear();
    activeScreen.reset();
    overworldCheckpoint.reset();
    worldContext = WorldContext::Overworld;
    normalize();
}

bool PlayerProfile::progressEmpty() const
{
    return levels.empty() &&
        screens.empty() &&
        !activeScreen &&
        !overworldCheckpoint &&
        unlockedLevel == 0 &&
        currentLevel == 0 &&
        currentScreen == 0;
}

PlayerProfile PlayerProfile::settingsOnly() const
{
    PlayerProfile result;
    result.adoptSettingsFrom(*this);
    return result;
}

void PlayerProfile::adoptSettingsFrom(const PlayerProfile& other)
{
    settings = other.settings;
    normalize();
}

const PlayerProfile::LevelProgress* PlayerProfile::progressForLevel(int level) const
{
    const auto found = std::ranges::find(levels, level, &LevelProgress::level);
    return found == levels.end() ? nullptr : &*found;
}

void PlayerProfile::recordScreenCompletion(
    LevelLocation location,
    int moves,
    std::optional<double> completionTimeSeconds,
    bool recordBests)
{
    if (location.level < 0 || location.screen < 0 || moves < 0 ||
        (completionTimeSeconds &&
            (!std::isfinite(*completionTimeSeconds) ||
                *completionTimeSeconds < 0.0))) {
        throw std::invalid_argument("invalid screen completion metrics");
    }
    auto found = std::ranges::find_if(
        screens,
        [&](const ScreenProgress& progress) {
            return progress.level == location.level &&
                progress.screen == location.screen;
        });
    if (found == screens.end()) {
        screens.push_back({
            .level = location.level,
            .screen = location.screen,
        });
        found = std::prev(screens.end());
    }
    found->completed = true;
    if (recordBests && (!found->bestMoves || moves < *found->bestMoves)) {
        found->bestMoves = moves;
    }
    if (recordBests && completionTimeSeconds &&
        (!found->bestTimeSeconds ||
            *completionTimeSeconds < *found->bestTimeSeconds)) {
        found->bestTimeSeconds = *completionTimeSeconds;
    }
    normalize();
}

const PlayerProfile::ScreenProgress* PlayerProfile::progressForScreen(
    LevelLocation location) const
{
    const auto found = std::ranges::find_if(
        screens,
        [&](const ScreenProgress& progress) {
            return progress.level == location.level &&
                progress.screen == location.screen;
        });
    return found == screens.end() ? nullptr : &*found;
}

bool PlayerProfile::screenCompleted(LevelLocation location) const
{
    if (const ScreenProgress* progress = progressForScreen(location)) {
        return progress->completed;
    }
    // A migrated sequential completion means every screen in that level was
    // necessarily traversed. This preserves old formats whose reached-screen
    // count predates that field and therefore cannot enumerate the screens.
    const LevelProgress* legacy = progressForLevel(location.level);
    return legacy != nullptr && legacy->completed;
}

ScreenSelectorStatus PlayerProfile::selectorStatus(
    LevelLocation location) const
{
    if (location.level < 0 || location.screen < 0) {
        return ScreenSelectorStatus::Unavailable;
    }
    if (screenCompleted(location)) {
        return ScreenSelectorStatus::Solved;
    }
    if (location.screen == 0 || screenCompleted({
            .level = location.level,
            .screen = location.screen - 1,
        })) {
        return ScreenSelectorStatus::Playable;
    }
    return ScreenSelectorStatus::Unavailable;
}

} // namespace sokoban
