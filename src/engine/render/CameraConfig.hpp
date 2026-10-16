#pragma once

#include "engine/CameraAngles.hpp"

namespace sokoban::config {

// Pitch is measured away from a straight-down view. Yaw rotates around the
// vertical axis from the established +Y viewpoint toward +X.
inline constexpr float cameraPitchDegrees = CameraAngles::defaultPitchDegrees;
inline constexpr float cameraYawDegrees = CameraAngles::defaultYawDegrees;
inline constexpr float cameraVerticalFovDegrees = 35.0f;
inline constexpr float cameraPitchTransitionSeconds = 0.15f;

// Distance scales with the larger board dimension. Fit scale is the projected
// board's span in the [-1, 1] viewport: 1.92 fills 96% of the limiting dimension,
// leaving a 2% margin on each edge while keeping the whole board visible.
inline constexpr float cameraDistanceScale = 2.2f;
inline constexpr float cameraFitScale = 1.92f;
// Model meshes may extend beyond their logical tile transform. Keep the depth
// clip planes outside the authored level volume without changing its framing.
inline constexpr float cameraDepthPaddingTiles = 1.0f;

static_assert(cameraPitchDegrees > 0.0f && cameraPitchDegrees < 90.0f);
static_assert(cameraPitchTransitionSeconds >= 0.0f);
static_assert(
    cameraVerticalFovDegrees > 1.0f &&
    cameraVerticalFovDegrees < 179.0f);
static_assert(cameraDistanceScale > 0.0f);
static_assert(cameraFitScale > 0.0f && cameraFitScale <= 2.0f);
static_assert(cameraDepthPaddingTiles >= 0.0f);

} // namespace sokoban::config
