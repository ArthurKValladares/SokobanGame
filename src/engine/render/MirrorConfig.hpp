#pragma once

#include "engine/Math.hpp"

namespace sokoban::config {

inline constexpr float mirrorModelRotationOffsetRadians = -0.78539816339f;
inline constexpr float mirrorBeamElevation = 0.56f;
inline constexpr float mirrorBeamThickness = 0.07f;
inline constexpr float mirrorBeamCoreWidth = 0.10f;
inline constexpr float mirrorBeamHaloWidth = 0.28f;
inline constexpr Vec4 mirrorBeamCoreColor { 0.72f, 0.97f, 1.0f, 0.78f };
inline constexpr Vec4 mirrorBeamHaloColor { 0.30f, 0.82f, 1.0f, 0.20f };
inline constexpr float mirrorPreviewExitFadeProgress = 0.30f;

inline constexpr Vec4 mirrorGhostColor { 0.64f, 0.94f, 1.0f, 0.46f };
inline constexpr float mirrorGhostRimPower = 2.2f;
inline constexpr float mirrorGhostRimStrength = 1.45f;
inline constexpr float mirrorEnergyPulseSpeed = 3.4f;
inline constexpr float mirrorEnergyPulseStrength = 0.16f;
inline constexpr float mirrorEnergyScanlineFrequency = 0.12f;
inline constexpr float mirrorEnergyScanlineSpeed = 5.0f;
inline constexpr float mirrorEnergyScanlineStrength = 0.10f;
inline constexpr float mirrorGhostTextureInfluence = 0.38f;

} // namespace sokoban::config
