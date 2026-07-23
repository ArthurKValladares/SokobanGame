#pragma once

#include "engine/InputBindings.hpp"

namespace sokoban {

// User-facing settings shared by menus and the runtime settings coordinator.
// Keeping labels/widgets out of this type lets another UI edit the same data.
struct UserSettings {
    int antiAliasingSamples = 8;
    int renderScalePercent = 100;
    bool customRenderScale = false;
    int customRenderScalePercent = 100;
    bool ambientOcclusion = true;
    bool fullscreen = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    float masterVolume = 1.0f;
    float musicVolume = 0.5f;
    InputBindings input = defaultInputBindings();

    bool operator==(const UserSettings&) const = default;
};

} // namespace sokoban
