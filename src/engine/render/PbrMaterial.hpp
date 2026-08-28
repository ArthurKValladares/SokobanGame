#pragma once

#include "engine/Math.hpp"

namespace sokoban {

inline constexpr float minimumPbrRoughness = 0.045f;

struct MetallicRoughness {
    float metallic = 1.0f;
    float roughness = 1.0f;
};

// CPU reference for the metallic-roughness resolution performed by
// triangle.frag.glsl. A missing map is represented by the default white
// sample, so the authored factors pass through unchanged.
[[nodiscard]] MetallicRoughness resolveMetallicRoughness(
    float metallicFactor,
    float roughnessFactor,
    Vec4 linearSample = { 1.0f, 1.0f, 1.0f, 1.0f });

// CPU reference for tangent-space normal resolution in triangle.frag.glsl.
// `doubleSidedBackFace` means the primitive is authored double-sided and the
// fragment is a back face; single-sided primitives never request the flip.
[[nodiscard]] Vec3 resolveNormalMap(
    Vec3 geometricNormal,
    Vec4 tangentAndHandedness,
    Vec4 linearSample = { 0.5f, 0.5f, 1.0f, 1.0f },
    float normalScale = 1.0f,
    bool doubleSidedBackFace = false);

} // namespace sokoban
