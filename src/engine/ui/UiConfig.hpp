#pragma once

#include <cstdint>
#include <string_view>

namespace sokoban::config {

inline constexpr std::string_view uiFontPath = "ui/Karla-Regular.ttf";
inline constexpr float uiFontPixelHeight = 36.0f;
inline constexpr uint32_t uiFontAtlasSize = 512;
inline constexpr std::string_view titleBackgroundPath =
    "custom/ui/main-menu-rogue-pushing-rock-4k.png";

} // namespace sokoban::config
