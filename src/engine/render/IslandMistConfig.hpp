#pragma once

#include "engine/Math.hpp"
#include "engine/Tuning.hpp"

#include <cstdint>

namespace sokoban::config {

// The Lighting panel's session toggle starts from this.
inline constexpr bool islandMistEnabled = true;

// Every value below is live-tunable in developer builds (Developer Tools >
// Tuning > Island mist), and Save writes the edited literals back into this
// file. See engine/Tuning.hpp for the declaration format.
SOKOBAN_TUNING_SECTION(islandMistTuning,
    "Island mist", "src/engine/render/IslandMistConfig.hpp", "islandMist");

// A thin participating medium hanging below and around the part of the board
// the camera frames. The atmosphere pass marches it against the sun's shadow
// map, so the island cuts a shaft of shadow through it and light pours down
// past its edges and through its gaps. It starts at the top height and
// reaches downward only: nothing a player stands on or pushes is ever inside
// it.
//
// The colour is a scattering albedo, not the colour seen. Sunlit mist takes
// the sun's warmth and shadowed mist the ambient fill, which is what makes
// the shafts read.
SOKOBAN_TUNABLE_COLOR3(islandMistTuning, islandMistColor, 1.0f, 0.9f, 0.82f);
// Extinction per world unit at the top height, looking straight down: the
// renderer thins a slanted ray so the mist is no thicker from a flat camera.
SOKOBAN_TUNABLE_FLOAT(islandMistTuning, islandMistDensity, 0.06f, 0.0f, 0.5f);
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistScatteringStrength, 1.8f, 0.0f, 4.0f);
// The camera looks down across the sun's rays, not along them, so a strongly
// forward-scattering medium would be dim from here. Keep this low.
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistAnisotropy, 0.2f, -0.85f, 0.85f);
// The share of the ambient fill the mist picks up. Shadowed mist is lit by
// little else, so lowering this darkens it and the shafts stand out more.
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistAmbientStrength, 0.5f, 0.0f, 2.0f);
SOKOBAN_TUNABLE_UINT(islandMistTuning, islandMistSampleCount, 24, 1, 32);

// World Z of the top of the layer: the underside of the lowest ground layer.
SOKOBAN_TUNABLE_FLOAT(islandMistTuning, islandMistTopHeight, 0.0f, -8.0f, 8.0f);
// How far below the top the layer is marched, and how quickly it thins on
// the way down (density falls by e over 1 / falloff units). Shafts fade out
// with the mist instead of ending on a plane.
SOKOBAN_TUNABLE_FLOAT(islandMistTuning, islandMistDepth, 10.0f, 0.5f, 48.0f);
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistDepthFalloff, 0.35f, 0.0f, 2.0f);
// Full density under the framed rectangle, fading to nothing this far
// outside it. A shadow travels about 1.3 units sideways per unit of depth
// with the default sun, so a short reach cuts the shafts off.
SOKOBAN_TUNABLE_FLOAT(islandMistTuning, islandMistReach, 10.0f, 1.0f, 64.0f);

// Slow world-space billows, the same fields the fog of war uses. Strength
// stays below one so the noise thins the mist without tearing holes in it.
SOKOBAN_TUNABLE_FLOAT(islandMistTuning, islandMistNoiseScale, 0.14f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistNoiseStrength, 0.5f, 0.0f, 0.8f);
SOKOBAN_TUNABLE_FLOAT(islandMistTuning, islandMistNoiseSpeed, 0.05f, 0.0f, 2.0f);
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistColorNoiseScale, 0.08f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistColorNoiseStrength, 0.2f, 0.0f, 0.75f);
SOKOBAN_TUNABLE_FLOAT(
    islandMistTuning, islandMistColorNoiseSpeed, 0.03f, 0.0f, 2.0f);

} // namespace sokoban::config
