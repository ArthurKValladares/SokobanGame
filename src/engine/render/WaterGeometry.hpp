#pragma once

// Water geometry shared by frame building, presentation and gameplay. Kept
// apart from WaterConfig.hpp (the look) so tuning the look recompiles only the
// water renderer and its settings UI.
namespace sokoban::config {

inline constexpr float waterDepthBelowGround = 0.18f;
inline constexpr float waterExteriorMarginScale = 2.0f;
inline constexpr float waterExteriorMinimumMarginTiles = 4.0f;
inline constexpr float drownedPlayerDepthBelowGround = 1.0f;

} // namespace sokoban::config
