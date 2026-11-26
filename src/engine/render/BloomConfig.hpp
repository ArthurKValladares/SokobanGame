#pragma once

#include "engine/Tuning.hpp"

#include <cstdint>

namespace sokoban::config {

// Bloom is a finishing effect in two scales. Only HDR values above ordinary
// white contribute, with a soft knee so highlights enter the glow without a
// hard halo boundary. A tight core keeps a highlight crisp; a wide skirt
// around it is what reads as glow from a distance. Both run at half
// resolution, which limits their cost and widens their footprint.
inline constexpr uint32_t bloomResolutionDivisor = 2;

SOKOBAN_TUNING_SECTION(bloomTuning,
    "Bloom", "src/engine/render/BloomConfig.hpp", "bloom");

SOKOBAN_TUNABLE_FLOAT(bloomTuning, bloomThreshold, 1.0f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(bloomTuning, bloomSoftKnee, 0.5f, 0.0f, 2.0f);
// Strength of the core: a Gaussian about three and a half pixels wide.
SOKOBAN_TUNABLE_FLOAT(bloomTuning, bloomIntensity, 0.18f, 0.0f, 2.0f);
// Strength of the skirt, on the same scale. The skirt carries the same light
// as the core spread over some forty times the area, so it needs the larger
// number to be seen. At zero it is not drawn.
SOKOBAN_TUNABLE_FLOAT(bloomTuning, bloomWideIntensity, 0.45f, 0.0f, 2.0f);
// How far the skirt reaches: the spacing of its taps, in texels of the bloom
// target. The skirt is about seven pixels wide for every unit. Much above
// three, the taps stand farther apart than the core they sample is wide, and
// a bright point starts to break into a row of dots.
SOKOBAN_TUNABLE_FLOAT(bloomTuning, bloomWideSpread, 3.0f, 1.0f, 4.0f);

} // namespace sokoban::config
