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
    profile.video.fullscreen = true;
    profile.audio.masterVolume = 0.7f;
    profile.accessibility.reducedMotion = true;
    profile.video.ambientOcclusion = false;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);

    const sokoban::SettingsEffects effects = coordinator.initialize();

    CHECK(effects.window.has_value());
    CHECK(effects.window->fullscreen);
    CHECK(effects.audio.has_value());
    CHECK(effects.audio->masterVolume == 0.7f);
    CHECK(effects.input.has_value());
    CHECK(effects.stepDurationSeconds == 0.05f);
    CHECK(!effects.antiAliasingSamples.has_value());
    CHECK(!effects.renderScalePercent.has_value());
    CHECK(!effects.saveProgress);
    CHECK(!effects.saveSettings);
    CHECK(!presentation.lighting.ambientOcclusionEnabled);
}

void testMenuProjectionAndChangePlan()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);
    (void)coordinator.initialize();

    sokoban::UserSettings settings = coordinator.userSettings();
    settings.antiAliasingSamples = 4;
    settings.customRenderScale = true;
    settings.customRenderScalePercent = 50;
    settings.ambientOcclusion = !settings.ambientOcclusion;
    settings.windowWidth = 1600;
    settings.windowHeight = 900;
    settings.masterVolume = 0.4f;
    settings.input.forAction(sokoban::InputAction::Undo) = {
        sokoban::KeyboardBinding { "Backspace" },
    };

    const sokoban::SettingsEffects effects =
        coordinator.applyUserSettings(settings);

    CHECK(effects.antiAliasingSamples == 4);
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
    CHECK(profile.video.effectiveRenderScalePercent() == 50);
    CHECK(presentation.lighting.ambientOcclusionEnabled ==
        settings.ambientOcclusion);
    CHECK(coordinator.userSettings() == settings);
}

void testUnchangedDomainsDoNotProduceRuntimeEffects()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);
    (void)coordinator.initialize();

    sokoban::UserSettings settings = coordinator.userSettings();
    settings.ambientOcclusion = !settings.ambientOcclusion;
    const sokoban::SettingsEffects effects =
        coordinator.applyUserSettings(settings);

    CHECK(!effects.window.has_value());
    CHECK(!effects.antiAliasingSamples.has_value());
    CHECK(!effects.renderScalePercent.has_value());
    CHECK(!effects.audio.has_value());
    CHECK(!effects.input.has_value());
    CHECK(effects.saveProgress);
    CHECK(effects.saveSettings);
    CHECK(presentation.lighting.ambientOcclusionEnabled ==
        settings.ambientOcclusion);
}

void testAudioPersistencePolicy()
{
    sokoban::PlayerProfile profile;
    sokoban::PresentationSettings presentation;
    sokoban::SettingsCoordinator coordinator(profile, presentation);

    sokoban::PlayerProfile::AudioSettings audio = profile.audio;
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
