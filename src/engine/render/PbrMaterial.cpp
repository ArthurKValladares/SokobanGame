#include "engine/render/PbrMaterial.hpp"

#include <algorithm>

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

} // namespace sokoban
