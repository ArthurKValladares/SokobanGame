#pragma once

#include "engine/Tuning.hpp"

#include <array>
#include <string_view>

namespace sokoban::config {

// What the world does back when something is done to it: dust where a stone
// is shoved off its spot and where a unit lands from a drop, and a lean into
// whatever a hero walks into that will not move. It is drawn and nothing
// else. No rule reads it, undo does not replay it, and a checkpoint does not
// carry it.
inline constexpr bool actionFeedbackEnabled = true;

// Drawn by tools/make_dust_puffs.py: plain soft puffs in the style of the
// tile decorations. The particle pack's smoke is photographic, and at this
// size reads as grain.
inline constexpr std::array<std::string_view, 4> actionFeedbackDustTextureNames {
    "DustPuff01", "DustPuff02", "DustPuff03", "DustPuff04",
};

SOKOBAN_TUNING_SECTION(actionFeedbackTuning,
    "Action feedback", "src/engine/ActionFeedbackConfig.hpp", "actionFeedback");

// How far a blocked hero leans toward the obstacle, in tiles, and how long
// the lean and the way back take together.
SOKOBAN_TUNABLE_FLOAT(actionFeedbackTuning, actionFeedbackBumpDistance, 0.1f, 0.0f, 0.3f);
SOKOBAN_TUNABLE_FLOAT(actionFeedbackTuning, actionFeedbackBumpSeconds, 0.22f, 0.05f, 0.6f);
// The dust is not lit, so its colour is what is seen: the ground's own pale
// earth, a little brighter than the ground in the sun.
SOKOBAN_TUNABLE_COLOR3(actionFeedbackTuning, actionFeedbackDustColor, 0.82f, 0.74f, 0.6f);
SOKOBAN_TUNABLE_FLOAT(actionFeedbackTuning, actionFeedbackDustOpacity, 0.6f, 0.0f, 1.0f);
// Scales every puff. The three effects keep their proportions to each other.
SOKOBAN_TUNABLE_FLOAT(actionFeedbackTuning, actionFeedbackDustSize, 1.0f, 0.2f, 3.0f);
// How long the longest-lived puff of a landing hangs in the air. The other
// effects are shorter by fixed shares.
SOKOBAN_TUNABLE_FLOAT(actionFeedbackTuning, actionFeedbackDustSeconds, 0.6f, 0.1f, 2.0f);

} // namespace sokoban::config
