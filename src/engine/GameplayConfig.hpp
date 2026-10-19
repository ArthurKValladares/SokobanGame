#pragma once

namespace sokoban::config {

inline constexpr float stepDurationSeconds = 0.25f;
inline constexpr float minimumStepDurationSeconds = 0.05f;
inline constexpr float maximumStepDurationSeconds = 1.0f;
inline constexpr float robotPatrolMovementDurationMultiplier = 2.0f;
inline constexpr float robotPatrolPauseSeconds = 0.20f;

} // namespace sokoban::config
