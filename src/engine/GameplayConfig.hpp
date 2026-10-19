#pragma once

#include "engine/Tuning.hpp"

namespace sokoban::config {

inline constexpr float stepDurationSeconds = 0.25f;
inline constexpr float minimumStepDurationSeconds = 0.05f;
inline constexpr float maximumStepDurationSeconds = 1.0f;
inline constexpr float waterStreamMovementDurationMultiplier = 2.0f;

SOKOBAN_TUNING_SECTION(robotPatrolTuning,
    "Robots", "src/engine/GameplayConfig.hpp", "robotPatrol");

SOKOBAN_TUNABLE_FLOAT(
    robotPatrolTuning, robotPatrolSecondsPerTile, 0.5f, 0.05f, 3.0f);
SOKOBAN_TUNABLE_FLOAT(
    robotPatrolTuning, robotPatrolPauseSeconds, 0.5f, 0.0f, 3.0f);

} // namespace sokoban::config
