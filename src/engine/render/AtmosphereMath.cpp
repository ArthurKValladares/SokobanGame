#include "engine/render/AtmosphereMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sokoban {

float atmosphereTransmittance(float density, float distance) noexcept
{
    return std::exp(-std::max(density, 0.0f) * std::max(distance, 0.0f));
}

float atmospherePhase(float cosine, float anisotropy) noexcept
{
    const float g = std::clamp(anisotropy, -0.85f, 0.85f);
    const float mu = std::clamp(cosine, -1.0f, 1.0f);
    const float denominator = std::max(
        1.0f + g * g - 2.0f * g * mu,
        0.0001f);
    return (1.0f - g * g) /
        (denominator * std::sqrt(denominator));
}

float atmosphereDensityAtHeight(
    float baseDensity,
    float heightFalloff,
    float baseHeight,
    float sampleHeight) noexcept
{
    const float altitude = std::max(sampleHeight - baseHeight, 0.0f);
    return std::max(baseDensity, 0.0f) *
        std::exp(-std::max(heightFalloff, 0.0f) * altitude);
}

std::optional<AtmosphereScreenRect> projectAtmosphereVolumeToScreen(
    const Mat4& clipFromWorld,
    Vec3 minimum,
    Vec3 maximum,
    uint32_t targetWidth,
    uint32_t targetHeight,
    uint32_t paddingPixels) noexcept
{
    if (targetWidth == 0 || targetHeight == 0) {
        return std::nullopt;
    }

    const std::array<Vec4, 8> corners {
        Vec4 { minimum.x, minimum.y, minimum.z, 1.0f },
        Vec4 { maximum.x, minimum.y, minimum.z, 1.0f },
        Vec4 { minimum.x, maximum.y, minimum.z, 1.0f },
        Vec4 { maximum.x, maximum.y, minimum.z, 1.0f },
        Vec4 { minimum.x, minimum.y, maximum.z, 1.0f },
        Vec4 { maximum.x, minimum.y, maximum.z, 1.0f },
        Vec4 { minimum.x, maximum.y, maximum.z, 1.0f },
        Vec4 { maximum.x, maximum.y, maximum.z, 1.0f },
    };
    float minimumNdcX = 1.0f;
    float minimumNdcY = 1.0f;
    float maximumNdcX = -1.0f;
    float maximumNdcY = -1.0f;
    for (const Vec4 corner : corners) {
        const Vec4 clip = transform(clipFromWorld, corner);
        if (clip.w <= 0.0001f || clip.z < 0.0f) {
            return AtmosphereScreenRect {
                .width = targetWidth,
                .height = targetHeight,
            };
        }
        const float reciprocalW = 1.0f / clip.w;
        minimumNdcX = std::min(minimumNdcX, clip.x * reciprocalW);
        minimumNdcY = std::min(minimumNdcY, clip.y * reciprocalW);
        maximumNdcX = std::max(maximumNdcX, clip.x * reciprocalW);
        maximumNdcY = std::max(maximumNdcY, clip.y * reciprocalW);
    }

    const float width = static_cast<float>(targetWidth);
    const float height = static_cast<float>(targetHeight);
    const float padding = static_cast<float>(paddingPixels);
    const float unclampedLeft =
        (minimumNdcX * 0.5f + 0.5f) * width - padding;
    const float unclampedRight =
        (maximumNdcX * 0.5f + 0.5f) * width + padding;
    // The renderer uses a negative viewport height, so +Y NDC maps to the
    // top of the image.
    const float unclampedTop =
        (0.5f - maximumNdcY * 0.5f) * height - padding;
    const float unclampedBottom =
        (0.5f - minimumNdcY * 0.5f) * height + padding;
    const int32_t left = std::clamp(
        static_cast<int32_t>(std::floor(unclampedLeft)),
        0,
        static_cast<int32_t>(targetWidth));
    const int32_t right = std::clamp(
        static_cast<int32_t>(std::ceil(unclampedRight)),
        0,
        static_cast<int32_t>(targetWidth));
    const int32_t top = std::clamp(
        static_cast<int32_t>(std::floor(unclampedTop)),
        0,
        static_cast<int32_t>(targetHeight));
    const int32_t bottom = std::clamp(
        static_cast<int32_t>(std::ceil(unclampedBottom)),
        0,
        static_cast<int32_t>(targetHeight));
    if (right <= left || bottom <= top) {
        return std::nullopt;
    }
    return AtmosphereScreenRect {
        .x = static_cast<uint32_t>(left),
        .y = static_cast<uint32_t>(top),
        .width = static_cast<uint32_t>(right - left),
        .height = static_cast<uint32_t>(bottom - top),
    };
}

} // namespace sokoban
