#pragma once

#include "engine/AudioConfig.hpp"
#include "engine/InputBindings.hpp"
#include "engine/UserSettingsConfig.hpp"

namespace sokoban {

struct UserSettings {
    struct Audio {
        float masterVolume = config::masterVolume;
        float musicVolume = config::musicVolume;
        float soundVolume = config::soundVolume;

        bool operator==(const Audio&) const = default;
    };

    struct Video {
        bool fullscreen = config::fullscreen;
        bool vsync = config::vsync;
        int antiAliasingSamples = config::antiAliasingSamples;
        int renderScalePercent = config::renderScalePercent;
        bool customRenderScale = config::customRenderScale;
        int customRenderScalePercent = config::customRenderScalePercent;
        bool ambientOcclusion = config::ambientOcclusion;
        float ambientOcclusionStrength =
            config::userAmbientOcclusionStrength;
        int windowWidth = config::windowWidth;
        int windowHeight = config::windowHeight;

        [[nodiscard]] int effectiveRenderScalePercent() const;
        bool operator==(const Video&) const = default;
    };

    struct Accessibility {
        bool reducedMotion = config::reducedMotion;
        bool highContrast = config::highContrast;
        bool largeText = config::largeText;
        bool subtitles = config::subtitles;
        bool screenShake = config::screenShake;

        bool operator==(const Accessibility&) const = default;
    };

    Audio audio;
    Video video;
    InputBindings input = defaultInputBindings();
    Accessibility accessibility;

    void normalize();
    bool operator==(const UserSettings&) const = default;
};

} // namespace sokoban
