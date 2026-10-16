#pragma once

#include "engine/AssetManifest.hpp"

#include <cstddef>
#include <string_view>

namespace sokoban {

namespace config {
inline constexpr std::string_view robotDriveAnimationName = "RobotDrive";
// The authored clip uses distance as its clock: one second is one tile of
// tread travel, with a seamless two-tile circulation period.
inline constexpr float robotDriveClipSecondsPerTile = 1.0f;
inline constexpr float robotDriveLoopSeconds = 2.0f;
} // namespace config

[[nodiscard]] inline RenderAnimation robotDriveAnimation(const AssetManifest& manifest)
{
    const auto& animations = manifest.animations();
    for (std::size_t index = 0; index < animations.size(); ++index) {
        if (animations[index].name == config::robotDriveAnimationName) {
            return RenderAnimation { static_cast<uint32_t>(index + 1) };
        }
    }
    // Small headless fixtures may still use a static robot model.
    return noAnimation;
}

} // namespace sokoban
