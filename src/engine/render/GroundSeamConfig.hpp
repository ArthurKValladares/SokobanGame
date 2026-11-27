#pragma once

#include "engine/Tuning.hpp"

namespace sokoban::config {

// On painted ground the board's grid is drawn as the joints between
// flagstones, one stone to a cell, instead of as lines: a narrow crevice that
// wanders, chips at the corners and thins out in places, with a pale rim on
// the edge that looks toward the sun and a soft shade on the one that looks
// away. It is the ground's own colour, darkened, so it is brown on rock and
// green under grass, and it still says where every cell is. Faces that are
// not painted ground, and a frame that sets its own grid as the Maker does,
// keep the plain lines of SceneConfig.hpp.
inline constexpr bool groundSeamsEnabled = true;

SOKOBAN_TUNING_SECTION(groundSeamTuning,
    "Ground seams", "src/engine/render/GroundSeamConfig.hpp", "groundSeam");

// How wide the crevice between two stones is on average, in tiles. The
// chips at the corners grow with it.
SOKOBAN_TUNABLE_FLOAT(groundSeamTuning, groundSeamWidth, 0.022f, 0.0f, 0.12f);
// How far a joint strays to either side of the straight line, in tiles.
SOKOBAN_TUNABLE_FLOAT(groundSeamTuning, groundSeamWobble, 0.12f, 0.0f, 0.2f);
// How far into a stone the shade of an edge that looks away from the sun
// reaches, in tiles. The pale rim of an edge that looks toward it is narrower
// by a fixed share.
SOKOBAN_TUNABLE_FLOAT(groundSeamTuning, groundSeamSoftness, 0.12f, 0.0f, 0.4f);
// What the ground's colour is multiplied by at the bottom of the crevice.
SOKOBAN_TUNABLE_COLOR3(groundSeamTuning, groundSeamTint, 0.56f, 0.53f, 0.48f);
// How much paler than the stone the worn rim of a sunward edge is.
SOKOBAN_TUNABLE_FLOAT(groundSeamTuning, groundSeamWornEdge, 0.15f, 0.0f, 0.5f);

} // namespace sokoban::config
