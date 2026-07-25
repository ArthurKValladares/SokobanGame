#pragma once

#include <array>

namespace sokoban::config {

inline constexpr bool fullscreen = false;
// False preserves the engine's mailbox-first presentation behavior.
inline constexpr bool vsync = false;
inline constexpr std::array antiAliasingSampleOptions { 1, 2, 4, 8 };
inline constexpr int antiAliasingSamples = 8;
inline constexpr int renderScalePercent = 100;
inline constexpr bool customRenderScale = false;
inline constexpr int customRenderScalePercent = 100;
inline constexpr bool ambientOcclusion = true;
inline constexpr int windowWidth = 1280;
inline constexpr int windowHeight = 720;
inline constexpr int minimumWindowWidth = 640;
inline constexpr int maximumWindowWidth = 7680;
inline constexpr int minimumWindowHeight = 480;
inline constexpr int maximumWindowHeight = 4320;

inline constexpr bool reducedMotion = false;
inline constexpr bool highContrast = false;
inline constexpr bool largeText = false;
inline constexpr bool subtitles = true;
inline constexpr bool screenShake = true;

} // namespace sokoban::config
