#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/ContentPipeline.hpp"
#include "engine/render/GltfMesh.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sokoban {

struct RuntimeTextureDefinition {
    TextureSourceIdentity identity;
    std::string label;
    bool manifestOwned = false;
};

[[nodiscard]] RuntimeTextureDefinition runtimeTextureDefinitionFor(
    const AssetManifest::Texture& texture);

struct RuntimeModelTextures {
    // Logical catalog indices. VulkanModelResources maps discovered entries
    // to stable high descriptor slots after the device capacity is selected.
    std::vector<uint32_t> requiredTextures;
    std::vector<PrimitiveMaterialBinding> primitiveMaterials;
};

class RuntimeTextureCatalog {
public:
    [[nodiscard]] const std::vector<RuntimeTextureDefinition>& textures() const
    {
        return textures_;
    }
    [[nodiscard]] const RuntimeModelTextures& model(uint32_t index) const
    {
        return models_.at(index);
    }
    [[nodiscard]] uint32_t manifestTextureCount() const
    {
        return manifestTextureCount_;
    }
    [[nodiscard]] uint32_t discoveredTextureCount() const
    {
        return static_cast<uint32_t>(textures_.size()) - manifestTextureCount_;
    }
    // Manifest-owned slots remain at the bottom of the heap so editor-created
    // RenderTexture ids stay stable. Discovered glTF maps occupy the top of
    // the selected fixed-capacity heap, leaving the middle available for
    // later manifest appends.
    [[nodiscard]] uint32_t descriptorIndex(
        uint32_t logicalIndex,
        uint32_t descriptorCapacity) const;

private:
    friend RuntimeTextureCatalog buildRuntimeTextureCatalog(
        const AssetManifest&,
        std::span<const ResolvedMaterialTexture>);

    uint32_t manifestTextureCount_ = 0;
    std::vector<RuntimeTextureDefinition> textures_;
    std::vector<RuntimeModelTextures> models_;
};

// Pure catalog assembly used by tests and by runtime collection.
[[nodiscard]] RuntimeTextureCatalog buildRuntimeTextureCatalog(
    const AssetManifest& manifest,
    std::span<const ResolvedMaterialTexture> materialTextures);

// Inspects each distinct manifest model document without loading image bytes
// or creating GPU resources, then assembles stable logical texture slots.
[[nodiscard]] RuntimeTextureCatalog collectRuntimeTextureCatalog(
    const std::filesystem::path& assetRoot,
    const AssetManifest& manifest);

} // namespace sokoban
