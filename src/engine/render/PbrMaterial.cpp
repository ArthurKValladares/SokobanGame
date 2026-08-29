#include "engine/render/PbrMaterial.hpp"

#include <algorithm>
#include <cmath>

namespace sokoban {

MetallicRoughness resolveMetallicRoughness(
    float metallicFactor,
    float roughnessFactor,
    Vec4 linearSample)
{
    // glTF stores perceptual roughness in G and metallic in B. R and A are
    // deliberately ignored; they may carry unrelated packed data.
    return {
        .metallic = std::clamp(
            metallicFactor * linearSample.z, 0.0f, 1.0f),
        .roughness = std::clamp(
            roughnessFactor * linearSample.y,
            minimumPbrRoughness,
            1.0f),
    };
}

Vec3 resolveNormalMap(
    Vec3 geometricNormal,
    Vec4 tangentAndHandedness,
    Vec4 linearSample,
    float normalScale,
    bool doubleSidedBackFace)
{
    geometricNormal = normalizeOr(
        geometricNormal, Vec3 { 0.0f, 0.0f, 1.0f });
    Vec3 tangent {
        tangentAndHandedness.x,
        tangentAndHandedness.y,
        tangentAndHandedness.z,
    };
    tangent -= geometricNormal * dot(geometricNormal, tangent);
    if (lengthSquared(tangent) < 0.000001f) {
        const Vec3 axis = std::abs(geometricNormal.z) < 0.9f
            ? Vec3 { 0.0f, 0.0f, 1.0f }
            : Vec3 { 1.0f, 0.0f, 0.0f };
        tangent = cross(axis, geometricNormal);
    }
    tangent = normalize(tangent);
    const float handedness = tangentAndHandedness.w < 0.0f ? -1.0f : 1.0f;
    const Vec3 bitangent = cross(geometricNormal, tangent) * handedness;
    const Vec3 tangentNormal {
        (linearSample.x * 2.0f - 1.0f) * normalScale,
        (linearSample.y * 2.0f - 1.0f) * normalScale,
        linearSample.z * 2.0f - 1.0f,
    };
    Vec3 result = normalizeOr(
        tangent * tangentNormal.x +
            bitangent * tangentNormal.y +
            geometricNormal * tangentNormal.z,
        geometricNormal);
    if (doubleSidedBackFace) {
        result = -result;
    }
    return result;
}

Vec3 resolveEmissive(Vec3 emissiveFactor, Vec4 linearSample)
{
    return {
        emissiveFactor.x * linearSample.x,
        emissiveFactor.y * linearSample.y,
        emissiveFactor.z * linearSample.z,
    };
}

float resolveMaterialOcclusion(float strength, Vec4 linearSample)
{
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    return 1.0f + (linearSample.x - 1.0f) * clampedStrength;
}

float ambientLightRatio(Vec3 ambientContribution, Vec3 totalColor)
{
    constexpr Vec3 luminanceWeights { 0.2126f, 0.7152f, 0.0722f };
    return std::clamp(
        dot(ambientContribution, luminanceWeights) /
            std::max(dot(totalColor, luminanceWeights), 0.0001f),
        0.0f,
        1.0f);
}

} // namespace sokoban
