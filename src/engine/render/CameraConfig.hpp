#pragma once

namespace sokoban::config {

// Pitch is measured away from a straight-down view. Yaw rotates around the
// vertical axis from the established +Y viewpoint toward +X.
inline constexpr float cameraPitchDegrees = 30.0f;
inline constexpr float cameraYawDegrees = 0.0f;
inline constexpr float cameraVerticalFovDegrees = 35.0f;
inline constexpr float cameraPitchTransitionSeconds = 0.15f;

// Distance scales with the larger board dimension. Fit scale is the final
// framing/zoom multiplier after projected scene bounds have been measured.
inline constexpr float cameraDistanceScale = 2.2f;
inline constexpr float cameraFitScale = 1.82f;
// Model meshes may extend beyond their logical tile transform. Keep the depth
// clip planes outside the authored level volume without changing its framing.
inline constexpr float cameraDepthPaddingTiles = 1.0f;

static_assert(cameraPitchDegrees > 0.0f && cameraPitchDegrees < 90.0f);
static_assert(cameraPitchTransitionSeconds >= 0.0f);
static_assert(
    cameraVerticalFovDegrees > 1.0f &&
    cameraVerticalFovDegrees < 179.0f);
static_assert(cameraDistanceScale > 0.0f);
static_assert(cameraFitScale > 0.0f);
static_assert(cameraDepthPaddingTiles >= 0.0f);

} // namespace sokoban::config
