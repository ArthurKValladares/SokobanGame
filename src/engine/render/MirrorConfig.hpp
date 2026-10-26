#pragma once

#include "engine/Math.hpp"

namespace sokoban::config {

inline constexpr float mirrorModelRotationOffsetRadians = -0.78539816339f;
inline constexpr float mirrorBeamElevation = 0.56f;
inline constexpr float mirrorBeamCoreWidth = 0.12f;
inline constexpr float mirrorBeamHaloWidth = 0.52f;
inline constexpr Vec4 mirrorBeamCoreColor { 0.72f, 0.97f, 1.0f, 0.66f };
inline constexpr Vec4 mirrorBeamHaloColor { 0.44f, 0.76f, 1.0f, 0.22f };
inline constexpr float mirrorBeamFlowSpeed = 1.15f;
inline constexpr float mirrorPreviewExitFadeProgress = 0.30f;

inline constexpr Vec4 mirrorGhostColor { 0.72f, 0.86f, 1.0f, 0.42f };
inline constexpr float mirrorGhostRimPower = 2.2f;
inline constexpr float mirrorGhostRimStrength = 1.25f;
inline constexpr float mirrorEnergyPulseSpeed = 1.6f;
inline constexpr float mirrorEnergyPulseStrength = 0.12f;
// World-space flow replaces the former screen-space scanlines.
inline constexpr float mirrorEnergyFlowFrequency = 8.5f;
inline constexpr float mirrorEnergyFlowSpeed = 1.3f;
inline constexpr float mirrorEnergyFlowStrength = 0.68f;
inline constexpr float mirrorGhostTextureInfluence = 0.24f;

} // namespace sokoban::config
