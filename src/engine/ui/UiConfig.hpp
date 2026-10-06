#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <array>

namespace sokoban::config {

// Draw commands one UI frame may hold in its arena. One rect, panel half, or
// glyph is one command; the busiest current page (game complete, at 4K) draws
// about 115. The budget is reserved in a single allocation at the start of
// every frame, so this is also the arena's usable capacity rather than a
// third of it. Exceeding it costs a heap allocation and a logged warning, not
// a lost frame.
inline constexpr std::size_t uiFrameCommandBudget = 8192;

inline constexpr std::string_view uiFontPath = "ui/Karla-Regular.ttf";
inline constexpr std::array<std::string_view, 4> uiFallbackFontPaths {
    "ui/NotoSans.ttf", "ui/NotoSansArabic.ttf", "ui/NotoSansHebrew.ttf", "ui/NotoSansDevanagari.ttf"
};
inline constexpr float uiFontPixelHeight = 36.0f;
inline constexpr uint32_t uiFontAtlasSize = 2048;
inline constexpr std::string_view titleBackgroundPath =
    "custom/ui/main-menu-rogue-pushing-rock-4k.png";

} // namespace sokoban::config
