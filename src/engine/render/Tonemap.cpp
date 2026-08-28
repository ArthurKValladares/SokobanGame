#include "engine/render/Tonemap.hpp"

#include <algorithm>

namespace sokoban {

Vec3 pbrNeutralToneMap(Vec3 color)
{
    constexpr float startCompression = 0.8f - 0.04f;
    constexpr float desaturation = 0.15f;
    const float x = std::min({ color.x, color.y, color.z });
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    color.x -= offset;
    color.y -= offset;
    color.z -= offset;

    const float peak = std::max({ color.x, color.y, color.z });
    if (peak < startCompression) {
        return color;
    }
    constexpr float d = 1.0f - startCompression;
    const float newPeak =
        1.0f - d * d / (peak + d - startCompression);
    const float scale = newPeak / peak;
    color.x *= scale;
    color.y *= scale;
    color.z *= scale;
    const float blend = 1.0f - 1.0f /
        (desaturation * (peak - newPeak) + 1.0f);
    color.x += (newPeak - color.x) * blend;
    color.y += (newPeak - color.y) * blend;
    color.z += (newPeak - color.z) * blend;
    return color;
}

Vec3 applyOutputTransform(
    Vec3 linearColor,
    float exposureEv,
    TonemapCurve curve)
{
    const float exposure = exposureMultiplier(exposureEv);
    Vec3 color {
        std::max(linearColor.x, 0.0f) * exposure,
        std::max(linearColor.y, 0.0f) * exposure,
        std::max(linearColor.z, 0.0f) * exposure,
    };
    if (curve == TonemapCurve::PbrNeutral) {
        color = pbrNeutralToneMap(color);
    }
    return {
        std::clamp(color.x, 0.0f, 1.0f),
        std::clamp(color.y, 0.0f, 1.0f),
        std::clamp(color.z, 0.0f, 1.0f),
    };
}

} // namespace sokoban
