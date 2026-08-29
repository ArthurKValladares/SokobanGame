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

// CPU reference for glTF emissive resolution in triangle.frag.glsl. The
// texture sample has already been decoded from sRGB by the image format.
// Alpha is ignored and the result deliberately remains unclamped so authored
// HDR emissive factors survive.
[[nodiscard]] Vec3 resolveEmissive(
    Vec3 emissiveFactor,
    Vec4 linearSample = { 1.0f, 1.0f, 1.0f, 1.0f });

// CPU reference for glTF material occlusion in triangle.frag.glsl. The map is
// linear data, only R is defined, and strength interpolates between an
// unoccluded value of one and the sample. A missing map is represented by the
// default white sample.
[[nodiscard]] float resolveMaterialOcclusion(
    float strength,
    Vec4 linearSample = { 1.0f, 1.0f, 1.0f, 1.0f });

// CPU reference for the ambient-share value written into the scene target's
// alpha. totalColor includes direct, ambient, specular and emissive light;
// ambientContribution is the only term SSAO is allowed to darken.
[[nodiscard]] float ambientLightRatio(
    Vec3 ambientContribution,
    Vec3 totalColor);

} // namespace sokoban
