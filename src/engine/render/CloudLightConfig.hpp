#pragma once

#include "engine/Math.hpp"
#include "engine/Tuning.hpp"

namespace sokoban::config {

// The Lighting panel's session toggle starts from this.
inline constexpr bool cloudLightEnabled = true;

// Every value below is live-tunable in developer builds (Developer Tools >
// Tuning > Cloud light), and Save writes the edited literals back into this
// file. See engine/Tuning.hpp for the declaration format.
SOKOBAN_TUNING_SECTION(cloudLightTuning,
    "Cloud light", "src/engine/render/CloudLightConfig.hpp", "cloudLight");

// Sunlight broken up as if by clouds passing overhead: a slow, drifting
// pattern of gaps that dims the sun wherever a cloud stands in its way. It is
// the same pattern for every surface and every medium, and it is laid out
// along the sun's rays, so the bright pools on the ground sit exactly where
// the shafts in the air come down. Only the sun is dimmed. Ambient light,
// point lights and cast shadows are left as they are.
//
// How much darker a surface gets under a cloud. Keep this low: the board is a
// puzzle first, and pools of light that are strong enough to notice at a
// glance are strong enough to be mistaken for something that matters.
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightSurfaceStrength, 0.3f, 0.0f, 1.0f);
// The same for the air: the island mist and the atmosphere. This is what
// turns the mist's even glow into separate shafts, so it can go much further
// than the surfaces do.
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightMediumStrength, 0.85f, 0.0f, 1.0f);
// How much brighter the air glows inside a gap. Breaking the light up takes
// more than half of it away, and this gives part of that back where it still
// falls, so the mist keeps most of the brightness it was tuned to and the
// shafts gain contrast. The surfaces get no such lift: a board is never
// brighter than its sun.
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightMediumGain, 1.35f, 1.0f, 3.0f);

// Size of the pattern, in clouds per world unit: 0.09 puts a gap every
// eleven tiles or so, one or two to a board.
SOKOBAN_TUNABLE_FLOAT(cloudLightTuning, cloudLightScale, 0.09f, 0.01f, 1.0f);
// The share of the sky the clouds take up. At 0.5 gaps and clouds divide it
// evenly; lower values let more light through.
SOKOBAN_TUNABLE_FLOAT(cloudLightTuning, cloudLightCoverage, 0.46f, 0.0f, 1.0f);
// How far a cloud's edge spreads to either side, as a share of the sky too.
// Small values give hard-edged pools; at 0.2 about two fifths of the sky is
// edge, and the pools fade in and out rather than arrive.
SOKOBAN_TUNABLE_FLOAT(cloudLightTuning, cloudLightSoftness, 0.2f, 0.01f, 0.5f);

// Finer breaks inside the pattern, seen by the air only. They split a pool's
// one broad beam into several narrow ones, which is what reads as rays. The
// surfaces never get them, so the board keeps its broad, calm pools.
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightDetailScale, 0.6f, 0.01f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightDetailStrength, 0.9f, 0.0f, 1.0f);
// The breaks are drawn out along the direction the camera looks across the
// sun's rays, by one over this: at 1 they are round, and the air between the
// camera and the board averages most of them away; at 0.2 each is five
// times as long as it is wide and survives as one clean ray.
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightDetailStretch, 0.2f, 0.05f, 1.0f);

// How fast the clouds travel, in world units per second, and which way, in
// degrees from +X toward +Y.
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightDriftSpeed, 0.35f, 0.0f, 4.0f);
SOKOBAN_TUNABLE_FLOAT(
    cloudLightTuning, cloudLightDriftDirectionDegrees, 25.0f, -180.0f, 180.0f);

} // namespace sokoban::config
