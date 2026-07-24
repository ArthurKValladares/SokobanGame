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
        .fullscreen = profile_.settings.video.fullscreen,
        .width = profile_.settings.video.windowWidth,
        .height = profile_.settings.video.windowHeight,
    };
    effects.audio = profile_.settings.audio;
    effects.input = profile_.settings.input;
    effects.stepDurationSeconds = profile_.settings.accessibility.reducedMotion
        ? 0.05f
        : config::stepDurationSeconds;
    return effects;
}

const UserSettings& SettingsCoordinator::userSettings() const
{
    return profile_.settings;
}

SettingsEffects SettingsCoordinator::applyUserSettings(
    const UserSettings& settings)
{
    const UserSettings oldSettings = profile_.settings;
    profile_.settings = settings;
    profile_.normalize();
    updatePresentationSettings();

    SettingsEffects effects;
    if (oldSettings.video.antiAliasingSamples !=
        profile_.settings.video.antiAliasingSamples) {
        effects.antiAliasingSamples =
            profile_.settings.video.antiAliasingSamples;
    }
    if (oldSettings.video.effectiveRenderScalePercent() !=
        profile_.settings.video.effectiveRenderScalePercent()) {
        effects.renderScalePercent =
            profile_.settings.video.effectiveRenderScalePercent();
    }
    if (oldSettings.video.fullscreen !=
            profile_.settings.video.fullscreen ||
        (!profile_.settings.video.fullscreen &&
            (oldSettings.video.windowWidth !=
                    profile_.settings.video.windowWidth ||
                oldSettings.video.windowHeight !=
                    profile_.settings.video.windowHeight))) {
        effects.window = SettingsEffects::WindowState {
            .fullscreen = profile_.settings.video.fullscreen,
            .width = profile_.settings.video.windowWidth,
            .height = profile_.settings.video.windowHeight,
        };
    }
    if (!(oldSettings.audio == profile_.settings.audio)) {
        effects.audio = profile_.settings.audio;
    }
    if (!(oldSettings.input == profile_.settings.input)) {
        effects.input = profile_.settings.input;
    }
    effects.saveProgress = true;
    effects.saveSettings = true;
    return effects;
}

SettingsEffects SettingsCoordinator::applyAudioSettings(
    const PlayerProfile::AudioSettings& settings,
    bool persist)
{
    profile_.settings.audio = settings;
    profile_.normalize();

    SettingsEffects effects;
    effects.audio = profile_.settings.audio;
    effects.saveProgress = persist;
    effects.saveSettings = persist;
    effects.immediatePersistence = persist;
    return effects;
}

void SettingsCoordinator::updatePresentationSettings()
{
    presentationSettings_.lighting.ambientOcclusionEnabled =
        profile_.settings.video.ambientOcclusion;
    presentationSettings_.normalize();
}

} // namespace sokoban
