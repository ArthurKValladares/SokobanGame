#pragma once

// Water geometry shared by frame building, presentation and gameplay. Kept
// apart from WaterConfig.hpp (the look) so tuning the look recompiles only the
// water renderer and its settings UI.
namespace sokoban::config {

inline constexpr float waterDepthBelowGround = 0.18f;
// Sunken supports sit in the water volume, leaving only their upper cap above
// the surface. Keep motion, settled geometry and mirror previews aligned.
inline constexpr float sunkenMovableDepthBelowGround = 1.0f;
// The authored leaf is source scale, with a thin, walkable upper surface.
inline constexpr float lilyPadRideHeight = 0.070f;
inline constexpr float lilyPadPickHeight = 0.090f;
inline constexpr float waterExteriorMarginScale = 2.0f;
inline constexpr float waterExteriorMinimumMarginTiles = 4.0f;
inline constexpr float drownedPlayerDepthBelowGround = 1.0f;

} // namespace sokoban::config
