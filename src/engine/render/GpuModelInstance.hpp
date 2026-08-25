#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstdint>

namespace sokoban {

// Per-instance vertex data for static models. Fragment parameters remain push
// constants, so an instanced draw only groups models with identical material
// state; transforms and normal rotation may still differ.
struct alignas(16) GpuModelInstance {
    std::array<Vec4, 4> clipFromModel {};
    std::array<Vec4, 4> shadowFromModel {};
    Vec4 rotationRadians {};
};

inline constexpr uint32_t maxStaticModelInstancesPerFrame =
    RenderFrameData::tileCapacity * 2;

static_assert(sizeof(GpuModelInstance) == 9 * sizeof(Vec4));

} // namespace sokoban
