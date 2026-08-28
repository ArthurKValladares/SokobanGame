#pragma once

#include "engine/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sokoban {

enum class TonemapCurve : uint32_t {
    Clamp = 0,
    PbrNeutral = 1,
};

inline constexpr float defaultExposureEv = 0.0f;
inline constexpr float minimumExposureEv = -4.0f;
inline constexpr float maximumExposureEv = 4.0f;

[[nodiscard]] inline float normalizedExposureEv(float exposureEv)
{
    return std::isfinite(exposureEv)
        ? std::clamp(exposureEv, minimumExposureEv, maximumExposureEv)
        : defaultExposureEv;
}

[[nodiscard]] inline float exposureMultiplier(float exposureEv)
{
    return std::exp2(normalizedExposureEv(exposureEv));
}

// CPU reference used by tests and offline comparisons. Keep the GLSL
// implementation in tonemap.frag.glsl mathematically identical.
[[nodiscard]] Vec3 pbrNeutralToneMap(Vec3 color);
[[nodiscard]] Vec3 applyOutputTransform(
    Vec3 linearColor,
    float exposureEv,
    TonemapCurve curve);

} // namespace sokoban
