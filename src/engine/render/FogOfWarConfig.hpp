#pragma once

#include "engine/Math.hpp"
#include "engine/Tuning.hpp"

#include <cstdint>

namespace sokoban::config {

// Every value here is live-tunable in Debug builds (Developer Tools >
// Tuning > Fog of war), and Save writes the edited literals back into this
// file. See engine/Tuning.hpp for the declaration format.
SOKOBAN_TUNING_SECTION(fogOfWarTuning,
    "Fog of war", "src/engine/render/FogOfWarConfig.hpp", "fogOfWar");

// Undiscovered overworld screens are filled with a dense participating
// medium. It is rendered by the atmosphere pass, so these values describe
// physical extinction and scattering rather than the alpha of an overlay.
SOKOBAN_TUNABLE_COLOR3(fogOfWarTuning, fogOfWarColor, 0.34f, 0.42f, 0.52f);
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarDensity, 4.25f, 0.0f, 16.0f);
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarScatteringStrength, 0.72f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarAnisotropy, 0.18f, -0.95f, 0.95f);
SOKOBAN_TUNABLE_UINT(fogOfWarTuning, fogOfWarSampleCount, 20, 1, 64);

// World-space fractal noise breaks the volume into broad billows with a
// smaller detail octave. Strength stays below one so noise can shape the
// cover without punching transparent holes through an undiscovered screen.
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarNoiseScale, 0.28f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarNoiseStrength, 0.58f, 0.0f, 1.0f);
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarNoiseSpeed, 0.12f, 0.0f, 2.0f);

// A second, independent field varies only the value of the scattered fog
// color. It uses a broader scale and slower motion so light and dark grey
// masses do not simply trace the density billows.
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarColorNoiseScale, 0.18f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarColorNoiseStrength, 0.48f, 0.0f, 1.0f);
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarColorNoiseSpeed, 0.07f, 0.0f, 2.0f);

// Keep the cover to one consistent three-tile column above the board. The
// horizontal feather begins outside the authored screen rectangle, so every
// tile in an undiscovered screen still receives full-density fog.
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarMinimumHeight, 0.0f, -4.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarHeight, 3.0f, 0.0f, 12.0f);
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarEdgeFadeDistance, 1.25f, 0.0f, 6.0f);

// A first visit clears outwards from the centre of the entry tile. The soft
// edge keeps the transition volumetric instead of reading as a hard wipe.
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarRevealDurationSeconds, 1.6f, 0.05f, 10.0f);
SOKOBAN_TUNABLE_FLOAT(
    fogOfWarTuning, fogOfWarInitialRevealRadius, 0.72f, 0.0f, 8.0f);
SOKOBAN_TUNABLE_FLOAT(fogOfWarTuning, fogOfWarRevealFeather, 0.9f, 0.0f, 8.0f);

} // namespace sokoban::config
