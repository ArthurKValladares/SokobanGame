#include "engine/render/AtmosphereMath.hpp"

#include <algorithm>
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

} // namespace sokoban
