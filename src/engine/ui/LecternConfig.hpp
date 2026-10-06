#pragma once

#include "engine/Tuning.hpp"

namespace sokoban::config {

SOKOBAN_TUNING_SECTION(lecternTuning,
    "Lecterns", "src/engine/ui/LecternConfig.hpp", "lectern");

// Global reading-text bounds in screen pixels. Zero disables either bound,
// leaving the font size proportional to the reading box. If the minimum
// exceeds the maximum, the minimum takes precedence.
SOKOBAN_TUNABLE_FLOAT(
    lecternTuning, lecternMinimumFontSize, 0.0f, 0.0f, 128.0f);
SOKOBAN_TUNABLE_FLOAT(
    lecternTuning, lecternMaximumFontSize, 0.0f, 0.0f, 128.0f);

} // namespace sokoban::config
