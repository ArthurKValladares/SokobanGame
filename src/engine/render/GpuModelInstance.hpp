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
    // World, not clip. The CPU used to bake the camera into every instance,
    // which meant a model could not be drawn without knowing where the camera
    // was and the vertex shader had nothing left to transform. The shadow
    // copy that sat beside this is gone with it: SceneFrameUniform carries
    // the sun transform for the whole frame.
    std::array<Vec4, 4> worldFromModel {};
    Vec4 rotationRadians {};
};

inline constexpr uint32_t maxStaticModelInstancesPerFrame =
    RenderFrameData::tileCapacity * 2;

static_assert(sizeof(GpuModelInstance) == 5 * sizeof(Vec4));

} // namespace sokoban
