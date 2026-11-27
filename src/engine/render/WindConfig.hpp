#pragma once

#include "engine/Tuning.hpp"

namespace sokoban::config {

// The wind sways whatever a model marks for it with a _WIND weight: the
// grass and moss of the tile decorations today. It blows the way the cloud
// light drifts, so the pools of light, the motes and the grass all answer to
// one weather. It is slow and small on purpose: a board that moves too much
// is hard to read.
inline constexpr bool windEnabled = true;

SOKOBAN_TUNING_SECTION(windTuning,
    "Wind", "src/engine/render/WindConfig.hpp", "wind");

// How far the tip of a blade moves in a full gust, in tiles.
SOKOBAN_TUNABLE_FLOAT(windTuning, windSwayAmplitude, 0.06f, 0.0f, 0.3f);
// Distance between one gust and the next, in tiles.
SOKOBAN_TUNABLE_FLOAT(windTuning, windGustLength, 6.0f, 0.5f, 40.0f);
// How fast a gust crosses the board, in tiles per second.
SOKOBAN_TUNABLE_FLOAT(windTuning, windGustSpeed, 1.8f, 0.0f, 12.0f);
// The share of the sway that is a quick sideways flutter, which keeps
// neighbouring blades from moving as one.
SOKOBAN_TUNABLE_FLOAT(windTuning, windFlutter, 0.25f, 0.0f, 1.0f);

} // namespace sokoban::config
