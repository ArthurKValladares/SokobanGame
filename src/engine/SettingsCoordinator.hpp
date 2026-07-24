#pragma once

#include "engine/PlayerProfile.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/SettingsTypes.hpp"

#include <optional>

namespace sokoban {

// A data-only plan of runtime effects. Application executes these against the
// platform and renderer, keeping SettingsCoordinator headless and testable.
struct SettingsEffects {
    struct WindowState {
        bool fullscreen = false;
        int width = 1280;
        int height = 720;
    };

    std::optional<WindowState> window;
    std::optional<int> antiAliasingSamples;
    std::optional<int> renderScalePercent;
    std::optional<PlayerProfile::AudioSettings> audio;
    std::optional<InputBindings> input;
    std::optional<float> stepDurationSeconds;
    bool saveProgress = false;
    bool saveSettings = false;
    bool immediatePersistence = false;
};

// Owns projection, normalization, change detection, and persistence policy for
// user settings. It deliberately has no Window, Vulkan, audio, or SaveStore
// dependencies; those effects are described by SettingsEffects.
class SettingsCoordinator {
public:
    SettingsCoordinator(
        PlayerProfile& profile,
        PresentationSettings& presentationSettings);

    [[nodiscard]] SettingsEffects initialize();
    [[nodiscard]] const UserSettings& userSettings() const;
    [[nodiscard]] SettingsEffects applyUserSettings(
        const UserSettings& settings);
    [[nodiscard]] SettingsEffects applyAudioSettings(
        const PlayerProfile::AudioSettings& settings,
        bool persist);

private:
    void updatePresentationSettings();

    PlayerProfile& profile_;
    PresentationSettings& presentationSettings_;
};

} // namespace sokoban
