#pragma once

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

} // namespace sokoban
