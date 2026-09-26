#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <optional>

namespace sokoban {

// CPU references for the two scalar pieces of the volumetric shader. Keeping
// these explicit makes the physical units and the anisotropy convention
// testable without a Vulkan device.
[[nodiscard]] float atmosphereTransmittance(
    float density,
    float distance) noexcept;

// Four-pi-scaled Henyey-Greenstein phase. Isotropic scattering is exactly
// one, which makes authored scattering strength intuitive.
[[nodiscard]] float atmospherePhase(
    float cosine,
    float anisotropy) noexcept;

[[nodiscard]] float atmosphereDensityAtHeight(
    float baseDensity,
    float heightFalloff,
    float baseHeight,
    float sampleHeight) noexcept;

struct AtmosphereScreenRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] constexpr bool valid() const
    {
        return width > 0 && height > 0;
    }
};

// Conservative pixel bounds of a world-space fog box. Volumes crossing the
// near/camera plane deliberately fall back to the whole target: losing the
// optimization for that uncommon view is preferable to clipping visible fog.
[[nodiscard]] std::optional<AtmosphereScreenRect>
projectAtmosphereVolumeToScreen(
    const Mat4& clipFromWorld,
    Vec3 minimum,
    Vec3 maximum,
    uint32_t targetWidth,
    uint32_t targetHeight,
    uint32_t paddingPixels = 2) noexcept;

} // namespace sokoban
