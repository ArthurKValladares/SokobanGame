#include "engine/SettingsTypes.hpp"

#include "engine/AudioConfig.hpp"
#include "engine/UserSettingsConfig.hpp"
#include "engine/render/RenderResolution.hpp"

#include <algorithm>

namespace sokoban {

int UserSettings::Video::effectiveRenderScalePercent() const
{
    return customRenderScale
        ? normalizedRenderScalePercent(customRenderScalePercent)
        : normalizedRenderScalePresetPercent(renderScalePercent);
}

void UserSettings::normalize()
{
    audio.masterVolume = std::clamp(
        audio.masterVolume, config::minimumVolume, config::maximumVolume);
    audio.musicVolume = std::clamp(
        audio.musicVolume, config::minimumVolume, config::maximumVolume);
    audio.soundVolume = std::clamp(
        audio.soundVolume, config::minimumVolume, config::maximumVolume);
    if (std::find(
            config::antiAliasingSampleOptions.begin(),
            config::antiAliasingSampleOptions.end(),
            video.antiAliasingSamples) ==
        config::antiAliasingSampleOptions.end()) {
        video.antiAliasingSamples = config::antiAliasingSamples;
    }
    video.renderScalePercent = normalizedRenderScalePresetPercent(
        video.renderScalePercent);
    video.customRenderScalePercent = normalizedRenderScalePercent(
        video.customRenderScalePercent);
    video.ambientOcclusionStrength = std::clamp(
        video.ambientOcclusionStrength,
        config::minimumUserAmbientOcclusionStrength,
        config::maximumUserAmbientOcclusionStrength);
    video.windowWidth = std::clamp(
        video.windowWidth,
        config::minimumWindowWidth,
        config::maximumWindowWidth);
    video.windowHeight = std::clamp(
        video.windowHeight,
        config::minimumWindowHeight,
        config::maximumWindowHeight);
}

} // namespace sokoban
