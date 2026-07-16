#pragma once

#include "engine/Math.hpp"

#include <array>

namespace sokoban {

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
