#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace sokoban::config {

inline constexpr std::array<std::string_view, 10>
    mirrorSwapSmokeTextureNames {
        "Smoke01", "Smoke02", "Smoke03", "Smoke04", "Smoke05",
        "Smoke06", "Smoke07", "Smoke08", "Smoke09", "Smoke10",
    };

inline constexpr uint32_t mirrorSwapSmokeParticleCount = 7;
inline constexpr Vec2 mirrorSwapSmokeLifetimeSeconds { 0.42f, 0.72f };
inline constexpr float mirrorSwapSmokeScale = 1.6f;
inline constexpr Vec2 mirrorSwapSmokeInitialSize { 0.34f, 0.52f };
inline constexpr Vec2 mirrorSwapSmokeFinalSize { 0.72f, 1.02f };
inline constexpr float mirrorSwapSmokeSpawnRadius = 0.20f;
inline constexpr Vec3 mirrorSwapSmokeMinimumVelocity {
    -0.18f, -0.18f, 0.28f };
inline constexpr Vec3 mirrorSwapSmokeMaximumVelocity {
    0.18f, 0.18f, 0.62f };
inline constexpr float mirrorSwapSmokeMinimumAngularVelocity = -1.8f;
inline constexpr float mirrorSwapSmokeMaximumAngularVelocity = 1.8f;
inline constexpr float mirrorSwapSmokeElevation = 0.72f;
inline constexpr float mirrorSwapSmokeOpacity = 0.72f;
inline constexpr bool mirrorSwapSmokeDrawOnTop = true;

} // namespace sokoban::config
