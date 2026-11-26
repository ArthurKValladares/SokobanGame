#pragma once

#include "engine/Math.hpp"
#include "engine/Tuning.hpp"

#include <cstdint>

namespace sokoban::config {

// Motes are the dust and pollen that hang in the air over the island: small
// specks that drift on the wind, glint where the sun reaches them and all
// but vanish under a cloud. They are the first piece of ambient life, there
// to make the air look inhabited without ever looking like something to act
// on, which is why they are tiny, slow and faint.
inline constexpr bool ambientMotesEnabled = true;

SOKOBAN_TUNING_SECTION(ambientMotesTuning,
    "Ambient motes", "src/engine/AmbientMotesConfig.hpp", "ambientMotes");

// The share of the air's unit cells that hold a mote. 0.1 puts about a
// hundred in view over an ordinary screen.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesDensity, 0.1f, 0.0f, 0.5f);
// Width of a mote's sprite in world units, a tile being one. Each mote is
// somewhere between half this and all of it, and the bright disc at the
// middle of the sprite is about a third as wide: a few pixels.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesSize, 0.14f, 0.01f, 0.5f);
SOKOBAN_TUNABLE_COLOR3(ambientMotesTuning, ambientMotesColor, 1.0f, 0.93f, 0.78f);
// Opacity of a mote in full sun.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesOpacity, 0.7f, 0.0f, 1.0f);
// How bright a mote is in full sun. Above one it reaches the bloom.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesGlow, 1.5f, 0.0f, 6.0f);
// How much of a mote is left under a cloud. Low values keep the motes to the
// pools of light, which is where dust shows.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesShadeVisibility, 0.2f, 0.0f, 1.0f);
// How fast the air carries the motes, in world units per second. They ride
// the same wind as the cloud light, far more slowly.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesDriftSpeed, 0.07f, 0.0f, 1.0f);
// How fast they rise.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesRiseSpeed, 0.02f, -0.5f, 0.5f);
// How far a mote wanders from its path, and how quickly.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesWanderRadius, 0.18f, 0.0f, 1.0f);
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesWanderSpeed, 0.5f, 0.0f, 4.0f);
// How far beyond the framed board the motes reach, sideways and above the
// level's tallest layer, and how far below its floor.
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesReach, 2.5f, 0.0f, 12.0f);
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesHeight, 1.5f, 0.0f, 8.0f);
SOKOBAN_TUNABLE_FLOAT(ambientMotesTuning, ambientMotesDepth, 2.0f, 0.0f, 12.0f);

// A mote fades over this distance as it nears a face of its box, so none
// pops in or out. Never drawn past this many, whatever the box holds.
inline constexpr float ambientMotesEdgeFade = 0.75f;
inline constexpr uint32_t ambientMotesLimit = 600;

} // namespace sokoban::config
