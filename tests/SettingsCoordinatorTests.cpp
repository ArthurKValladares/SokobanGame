#include "engine/SettingsCoordinator.hpp"

#include <iostream>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

void testInitializationProducesAllStartupEffects()
{
    sokoban::PlayerProfile profile;
    profile.settings.video.fullscreen = true;
    profile.settings.audio.masterVolume = 0.7f;
    profile.settings.video.ambientOcclusion = false;
    profile.settings.video.ambientOcclusionStrength = 0.35f;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);

    const sokoban::SettingsEffects effects = coordinator.initialize();

    CHECK(effects.window.has_value());
    CHECK(effects.window->fullscreen);
    CHECK(effects.audio.has_value());
    CHECK(effects.audio->masterVolume == 0.7f);
    CHECK(effects.input.has_value());
    CHECK(!effects.antiAliasingSamples.has_value());
    CHECK(!effects.renderScalePercent.has_value());
    CHECK(effects.presentation.has_value());
    CHECK(effects.presentation->vsync == profile.settings.video.vsync);
    CHECK(effects.frameRateLimit == profile.settings.video.frameRateLimit);
    CHECK(!effects.saveProgress);
    CHECK(!effects.saveSettings);
    CHECK(!presentation.lighting.ambientOcclusionEnabled);
    CHECK(presentation.lighting.ambientOcclusionStrength == 0.35f);
}

void testMenuProjectionAndChangePlan()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);
    (void)coordinator.initialize();

    sokoban::UserSettings settings = coordinator.userSettings();
    settings.video.antiAliasingSamples = 2;
    settings.video.customRenderScale = true;
    settings.video.customRenderScalePercent = 50;
    settings.video.ambientOcclusion =
        !settings.video.ambientOcclusion;
    settings.video.ambientOcclusionStrength = 0.8f;
    settings.video.windowWidth = 1600;
    settings.video.windowHeight = 900;
    settings.audio.masterVolume = 0.4f;
    settings.input.forAction(sokoban::InputAction::Undo) = {
        sokoban::KeyboardBinding { "Backspace" },
    };

    const sokoban::SettingsEffects effects =
        coordinator.applyUserSettings(settings);

    CHECK(effects.antiAliasingSamples == 2);
    CHECK(effects.renderScalePercent == 50);
    CHECK(effects.window.has_value());
    CHECK(!effects.window->fullscreen);
    CHECK(effects.window->width == 1600);
    CHECK(effects.window->height == 900);
    CHECK(effects.audio.has_value());
    CHECK(effects.input.has_value());
    CHECK(effects.saveProgress);
    CHECK(effects.saveSettings);
    CHECK(!effects.immediatePersistence);
    CHECK(
        profile.settings.video.effectiveRenderScalePercent() == 50);
    CHECK(presentation.lighting.ambientOcclusionEnabled ==
        settings.video.ambientOcclusion);
    CHECK(presentation.lighting.ambientOcclusionStrength == 0.8f);
    CHECK(coordinator.userSettings() == settings);
}

void testUnchangedDomainsDoNotProduceRuntimeEffects()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);
    (void)coordinator.initialize();

    sokoban::UserSettings settings = coordinator.userSettings();
    settings.video.ambientOcclusion =
        !settings.video.ambientOcclusion;
    settings.video.ambientOcclusionStrength = 0.25f;
    const sokoban::SettingsEffects effects =
        coordinator.applyUserSettings(settings);

    CHECK(!effects.window.has_value());
    CHECK(!effects.antiAliasingSamples.has_value());
    CHECK(!effects.renderScalePercent.has_value());
    CHECK(!effects.presentation.has_value());
    CHECK(!effects.frameRateLimit.has_value());
    CHECK(!effects.audio.has_value());
    CHECK(!effects.input.has_value());
    CHECK(effects.saveProgress);
    CHECK(effects.saveSettings);
    CHECK(presentation.lighting.ambientOcclusionEnabled ==
        settings.video.ambientOcclusion);
    CHECK(presentation.lighting.ambientOcclusionStrength == 0.25f);
}

void testPresentationAndPacingProduceTargetedEffects()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);
    (void)coordinator.initialize();

    sokoban::UserSettings settings = coordinator.userSettings();
    settings.video.vsync = false;
    settings.video.allowTearing = true;
    settings.video.frameRateLimit = 120;
    const sokoban::SettingsEffects effects =
        coordinator.applyUserSettings(settings);

    CHECK(effects.presentation.has_value());
    CHECK(!effects.presentation->vsync);
    CHECK(effects.presentation->allowTearing);
    CHECK(effects.frameRateLimit == 120);
    CHECK(!effects.window.has_value());
    CHECK(!effects.antiAliasingSamples.has_value());
}

void testAudioPersistencePolicy()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);

    sokoban::PlayerProfile::AudioSettings audio =
        profile.settings.audio;
    audio.soundVolume = 0.25f;
    sokoban::SettingsEffects effects =
        coordinator.applyAudioSettings(audio, false);
    CHECK(effects.audio.has_value());
    CHECK(!effects.saveProgress);
    CHECK(!effects.saveSettings);

    effects = coordinator.applyAudioSettings(audio, true);
    CHECK(effects.saveProgress);
    CHECK(effects.saveSettings);
    CHECK(effects.immediatePersistence);
}

} // namespace

int main()
{
    testInitializationProducesAllStartupEffects();
    testMenuProjectionAndChangePlan();
    testUnchangedDomainsDoNotProduceRuntimeEffects();
    testPresentationAndPacingProduceTargetedEffects();
    testAudioPersistencePolicy();

    if (failures == 0) {
        std::cout << "SettingsCoordinatorTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "SettingsCoordinatorTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
