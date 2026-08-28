#include "engine/render/VulkanRenderConstants.hpp"

#include "engine/render/GltfMesh.hpp"

namespace sokoban {

GpuMaterial gpuMaterialFrom(const MeshMaterial& material)
{
    return GpuMaterial {
        .baseColorFactor = material.baseColorFactor,
        .emissiveAndMetallic = {
            material.emissiveFactor.x,
            material.emissiveFactor.y,
            material.emissiveFactor.z,
            material.metallicFactor,
        },
        .materialScalars = {
            material.roughnessFactor,
            material.normalScale,
            material.occlusionStrength,
            material.alphaCutoff,
        },
        .primaryTextureHandles = {
            material.baseColorTexture,
            material.normalTexture,
            material.metallicRoughnessTexture,
            material.emissiveTexture,
        },
        .occlusionTextureAndPadding = {
            material.occlusionTexture,
            0U,
            0U,
            0U,
        },
        .textureUvSets = {
            material.baseColorUvSet,
            material.normalUvSet,
            material.metallicRoughnessUvSet,
            material.emissiveUvSet,
        },
        .materialState = {
            material.occlusionUvSet,
            static_cast<uint32_t>(material.alphaMode),
            material.flags,
            material.doubleSided ? 1U : 0U,
        },
    };
}

} // namespace sokoban
