#pragma once

namespace sokoban::config {

// Pitch is measured away from a straight-down view. Yaw rotates around the
// vertical axis from the established +Y viewpoint toward +X.
inline constexpr float cameraPitchDegrees = 30.0f;
inline constexpr float cameraYawDegrees = 0.0f;
inline constexpr float cameraVerticalFovDegrees = 35.0f;

// Distance scales with the larger board dimension. Fit scale is the final
// framing/zoom multiplier after projected scene bounds have been measured.
inline constexpr float cameraDistanceScale = 2.2f;
inline constexpr float cameraFitScale = 1.82f;

static_assert(cameraPitchDegrees > 0.0f && cameraPitchDegrees < 90.0f);
static_assert(
    cameraVerticalFovDegrees > 1.0f &&
    cameraVerticalFovDegrees < 179.0f);
static_assert(cameraDistanceScale > 0.0f);
static_assert(cameraFitScale > 0.0f);

} // namespace sokoban::config
