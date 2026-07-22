#include "engine/PlayerProfile.hpp"

#include "engine/render/RenderResolution.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sokoban {

int PlayerProfile::VideoSettings::effectiveRenderScalePercent() const
{
    return customRenderScale
        ? normalizedRenderScalePercent(customRenderScalePercent)
        : normalizedRenderScalePresetPercent(renderScalePercent);
}

void PlayerProfile::normalize()
{
    unlockedLevel = std::max(unlockedLevel, 0);
    currentLevel = std::clamp(currentLevel, 0, unlockedLevel);
    currentScreen = std::max(currentScreen, 0);
    audio.masterVolume = std::clamp(audio.masterVolume, 0.0f, 1.0f);
    audio.musicVolume = std::clamp(audio.musicVolume, 0.0f, 1.0f);
    audio.soundVolume = std::clamp(audio.soundVolume, 0.0f, 1.0f);
    if (video.antiAliasingSamples != 1 &&
        video.antiAliasingSamples != 2 &&
        video.antiAliasingSamples != 4 &&
        video.antiAliasingSamples != 8) {
        video.antiAliasingSamples = 8;
    }
    video.renderScalePercent = normalizedRenderScalePresetPercent(
        video.renderScalePercent);
    video.customRenderScalePercent = normalizedRenderScalePercent(
        video.customRenderScalePercent);
    video.windowWidth = std::clamp(video.windowWidth, 640, 7680);
    video.windowHeight = std::clamp(video.windowHeight, 480, 4320);
    for (LevelProgress& level : levels) {
        level.reachedScreens = std::max(level.reachedScreens, 0);
    }
    std::ranges::sort(levels, {}, &LevelProgress::level);
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
    const int normalizedLevel = std::clamp(level, 0, std::max(unlockedLevel, 0));
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
    activeScreen.reset();
    normalize();
}

bool PlayerProfile::progressEmpty() const
{
    return levels.empty() &&
        !activeScreen &&
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
    audio = other.audio;
    video = other.video;
    input = other.input;
    accessibility = other.accessibility;
    normalize();
}

const PlayerProfile::LevelProgress* PlayerProfile::progressForLevel(int level) const
{
    const auto found = std::ranges::find(levels, level, &LevelProgress::level);
    return found == levels.end() ? nullptr : &*found;
}

} // namespace sokoban
