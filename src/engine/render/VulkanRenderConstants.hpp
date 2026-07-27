#pragma once

#include "engine/Math.hpp"

#include <array>
#include <cstdint>

namespace sokoban {

// Scene descriptor set bindings that hold a single image each: the shadow map,
// the resolved scene color, and the post-processing inputs. The model texture
// array is the remaining binding. Kept here so the descriptor pool sizing and
// the device sampled-image limit check agree on the count.
inline constexpr uint32_t sceneSingleImageBindings = 6;

struct TilePushConstants {
    std::array<Vec4, 4> vertices;
    std::array<Vec4, 4> shadowVertices;
    Vec4 color;
    Vec4 normalAndAmbientRed;
    Vec4 sunDirectionAndAmbientGreen;
    Vec4 sunRadianceAndAmbientBlue;
    Vec4 shadowOptions;
    Vec4 materialOptions;
    Vec4 gridColor;
    Vec4 textureOptions;
};

static_assert(sizeof(TilePushConstants) == 256);

} // namespace sokoban
