#pragma once

#include "engine/Config.hpp"
#include "engine/InputBindings.hpp"

namespace sokoban {

struct UserSettings {
    struct Audio {
        float masterVolume = config::masterVolume;
        float musicVolume = config::musicVolume;
        float soundVolume = 1.0f;

        bool operator==(const Audio&) const = default;
    };

    struct Video {
        bool fullscreen = false;
        // False preserves the engine's mailbox-first presentation behavior.
        bool vsync = false;
        int antiAliasingSamples = 8;
        int renderScalePercent = 100;
        bool customRenderScale = false;
        int customRenderScalePercent = 100;
        bool ambientOcclusion = config::ambientOcclusionEnabled;
        int windowWidth = 1280;
        int windowHeight = 720;

        [[nodiscard]] int effectiveRenderScalePercent() const;
        bool operator==(const Video&) const = default;
    };

    struct Accessibility {
        bool reducedMotion = false;
        bool highContrast = false;
        bool largeText = false;
        bool subtitles = true;
        bool screenShake = true;

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
