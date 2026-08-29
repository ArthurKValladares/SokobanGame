#include "engine/render/MaterialRenderPolicy.hpp"

namespace sokoban {

ModelMaterialPolicy modelMaterialPolicy(
    std::span<const MeshMaterial> materials)
{
    if (materials.empty()) {
        return {};
    }

    ModelMaterialPolicy result {
        .hasOpaqueOrMask = false,
    };
    for (const MeshMaterial& material : materials) {
        result.hasOpaqueOrMask |=
            material.alphaMode != MaterialAlphaMode::Blend;
        result.hasBlend |= material.alphaMode == MaterialAlphaMode::Blend;
        result.hasDoubleSided |= material.doubleSided;
    }
    return result;
}

bool materialSelected(
    MaterialAlphaMode mode,
    MaterialAlphaSelection selection)
{
    switch (selection) {
    case MaterialAlphaSelection::All:
        return true;
    case MaterialAlphaSelection::OpaqueAndMask:
        return mode != MaterialAlphaMode::Blend;
    case MaterialAlphaSelection::BlendOnly:
        return mode == MaterialAlphaMode::Blend;
    }
    return false;
}

ResolvedBaseColor resolveMaterialBaseColor(
    Vec4 drawColor,
    Vec4 baseColorFactor,
    Vec4 baseColorSample,
    MaterialAlphaMode mode,
    float alphaCutoff)
{
    const float authoredAlpha =
        baseColorFactor.w * baseColorSample.w;
    return {
        .rgb = {
            drawColor.x * baseColorFactor.x * baseColorSample.x,
            drawColor.y * baseColorFactor.y * baseColorSample.y,
            drawColor.z * baseColorFactor.z * baseColorSample.z,
        },
        .alpha = mode == MaterialAlphaMode::Blend
            ? drawColor.w * authoredAlpha
            : drawColor.w,
        .discarded = mode == MaterialAlphaMode::Mask &&
            authoredAlpha < alphaCutoff,
    };
}

Vec3 resolveMirrorEnergyBaseColor(
    Vec4 baseColorFactor,
    Vec4 baseColorSample)
{
    return {
        baseColorFactor.x * baseColorSample.x,
        baseColorFactor.y * baseColorSample.y,
        baseColorFactor.z * baseColorSample.z,
    };
}

} // namespace sokoban
