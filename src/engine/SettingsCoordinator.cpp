#include "engine/SettingsCoordinator.hpp"

#include "engine/Config.hpp"

namespace sokoban {

SettingsCoordinator::SettingsCoordinator(
    PlayerProfile& profile,
    PresentationSettings& presentationSettings)
    : profile_(profile)
    , presentationSettings_(presentationSettings)
{
}

SettingsEffects SettingsCoordinator::initialize()
{
    profile_.normalize();
    updatePresentationSettings();

    SettingsEffects effects;
    effects.window = SettingsEffects::WindowState {
        .fullscreen = profile_.video.fullscreen,
        .width = profile_.video.windowWidth,
        .height = profile_.video.windowHeight,
    };
    effects.audio = profile_.audio;
    effects.input = profile_.input;
    effects.stepDurationSeconds = profile_.accessibility.reducedMotion
        ? 0.05f
        : config::stepDurationSeconds;
    return effects;
}

UserSettings SettingsCoordinator::userSettings() const
{
    return {
        .antiAliasingSamples = profile_.video.antiAliasingSamples,
        .renderScalePercent = profile_.video.renderScalePercent,
        .customRenderScale = profile_.video.customRenderScale,
        .customRenderScalePercent = profile_.video.customRenderScalePercent,
        .ambientOcclusion = profile_.video.ambientOcclusion,
        .fullscreen = profile_.video.fullscreen,
        .windowWidth = profile_.video.windowWidth,
        .windowHeight = profile_.video.windowHeight,
        .masterVolume = profile_.audio.masterVolume,
        .musicVolume = profile_.audio.musicVolume,
        .input = profile_.input,
    };
}

SettingsEffects SettingsCoordinator::applyUserSettings(
    const UserSettings& settings)
{
    const PlayerProfile::VideoSettings oldVideo = profile_.video;
    const PlayerProfile::AudioSettings oldAudio = profile_.audio;
    const InputBindings oldInput = profile_.input;

    profile_.video.antiAliasingSamples = settings.antiAliasingSamples;
    profile_.video.renderScalePercent = settings.renderScalePercent;
    profile_.video.customRenderScale = settings.customRenderScale;
    profile_.video.customRenderScalePercent = settings.customRenderScalePercent;
    profile_.video.ambientOcclusion = settings.ambientOcclusion;
    profile_.video.fullscreen = settings.fullscreen;
    profile_.video.windowWidth = settings.windowWidth;
    profile_.video.windowHeight = settings.windowHeight;
    profile_.audio.masterVolume = settings.masterVolume;
    profile_.audio.musicVolume = settings.musicVolume;
    profile_.input = settings.input;
    profile_.normalize();
    updatePresentationSettings();

    SettingsEffects effects;
    if (oldVideo.antiAliasingSamples != profile_.video.antiAliasingSamples) {
        effects.antiAliasingSamples = profile_.video.antiAliasingSamples;
    }
    if (oldVideo.effectiveRenderScalePercent() !=
        profile_.video.effectiveRenderScalePercent()) {
        effects.renderScalePercent =
            profile_.video.effectiveRenderScalePercent();
    }
    if (oldVideo.fullscreen != profile_.video.fullscreen ||
        (!profile_.video.fullscreen &&
            (oldVideo.windowWidth != profile_.video.windowWidth ||
                oldVideo.windowHeight != profile_.video.windowHeight))) {
        effects.window = SettingsEffects::WindowState {
            .fullscreen = profile_.video.fullscreen,
            .width = profile_.video.windowWidth,
            .height = profile_.video.windowHeight,
        };
    }
    if (!(oldAudio == profile_.audio)) {
        effects.audio = profile_.audio;
    }
    if (!(oldInput == profile_.input)) {
        effects.input = profile_.input;
    }
    effects.saveProgress = true;
    effects.saveSettings = true;
    return effects;
}

SettingsEffects SettingsCoordinator::applyAudioSettings(
    const PlayerProfile::AudioSettings& settings,
    bool persist)
{
    profile_.audio = settings;
    profile_.normalize();

    SettingsEffects effects;
    effects.audio = profile_.audio;
    effects.saveProgress = persist;
    effects.saveSettings = persist;
    effects.immediatePersistence = persist;
    return effects;
}

void SettingsCoordinator::updatePresentationSettings()
{
    presentationSettings_.lighting.ambientOcclusionEnabled =
        profile_.video.ambientOcclusion;
    presentationSettings_.normalize();
}

} // namespace sokoban
