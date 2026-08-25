#pragma once

#include <array>

namespace sokoban::config {

inline constexpr bool fullscreen = false;
// FIFO is available on every Vulkan presentation surface and provides stable
// pacing out of the box. Mailbox is used only when the player opts out.
inline constexpr bool vsync = true;
inline constexpr bool allowTearing = false;
// Zero means no CPU cap: FIFO/mailbox remains responsible for pacing.
inline constexpr int frameRateLimit = 0;
inline constexpr std::array frameRateLimitOptions { 0, 30, 60, 120, 144, 240 };
inline constexpr int unfocusedFrameRateLimit = 20;
inline constexpr int minimizedFrameRateLimit = 5;
inline constexpr std::array antiAliasingSampleOptions { 1, 2, 4, 8 };
// 4x is the default because the scene pass is fragment-bound: opaque geometry
// is depth-prepassed by ordering rather than by a prepass, and every extra
// sample multiplies shading the rasterizer cannot reject. 8x stays available
// and is worth re-measuring now that opaque draws no longer blend and models
// cull back faces. Existing profiles keep whatever they already stored; only
// a fresh profile picks this up.
inline constexpr int antiAliasingSamples = 4;
inline constexpr int renderScalePercent = 100;
inline constexpr bool customRenderScale = false;
inline constexpr int customRenderScalePercent = 100;
inline constexpr bool ambientOcclusion = true;
inline constexpr float userAmbientOcclusionStrength = 0.55f;
inline constexpr float minimumUserAmbientOcclusionStrength = 0.0f;
inline constexpr float maximumUserAmbientOcclusionStrength = 1.0f;
inline constexpr int windowWidth = 1280;
inline constexpr int windowHeight = 720;
inline constexpr int minimumWindowWidth = 640;
inline constexpr int maximumWindowWidth = 7680;
inline constexpr int minimumWindowHeight = 480;
inline constexpr int maximumWindowHeight = 4320;

} // namespace sokoban::config
