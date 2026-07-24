#include "engine/SettingsTypes.hpp"

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
}

} // namespace sokoban
