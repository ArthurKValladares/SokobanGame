#pragma once

#include "engine/Math.hpp"

namespace sokoban::config {

// Thin silhouettes in display pixels, independent of camera distance and
// internal render scale. Color is linear RGB, before the output transform.
inline constexpr float interactionOutlineWidthPixels = 1.5f;
inline constexpr Vec4 interactionOutlineColor { 0.018f, 0.030f, 0.075f, 1.0f };

} // namespace sokoban::config
