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

// Density thins exponentially with altitude above the base height. Below it
// the medium keeps its base density, unless depthFalloff thins it downward
// as well: the island mist hangs from its base that way.
[[nodiscard]] float atmosphereDensityAtHeight(
    float baseDensity,
    float heightFalloff,
    float baseHeight,
    float sampleHeight,
    float depthFalloff = 0.0f) noexcept;

// The box the island mist is marched in. It hangs `depth` units below
// topHeight and reaches `reach` units outside the rectangle of the board the
// camera frames, which is also the distance its density fades out over: full
// density under that rectangle, none at the box's sides.
struct AtmosphereVolumeBounds {
    Vec3 minimum {};
    Vec3 maximum {};
};
[[nodiscard]] AtmosphereVolumeBounds islandMistBounds(
    Vec2 framedMinimum,
    Vec2 framedMaximum,
    float topHeight,
    float depth,
    float reach) noexcept;

// One axis of the composite's reconstruction filter: the weights of three
// neighbouring integration texels, the first of which is
// floor(position - 0.5) for a position measured in texel centres. `fraction`
// is fract(position - 0.5).
//
// The integration pass offsets each pixel's samples along the ray by an
// ordered dither whose strongest component alternates from one texel to the
// next. These weights give the odd and the even texels half of the total
// each, wherever the output pixel falls, so that component cancels instead
// of surviving the upsample as a visible weave. Plain bilinear weights
// cannot do that: at a texel centre they take a single sample.
struct AtmosphereReconstructionWeights {
    float first = 0.0f;
    float second = 0.0f;
    float third = 0.0f;
};
[[nodiscard]] AtmosphereReconstructionWeights
atmosphereReconstructionWeights(float fraction) noexcept;

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
