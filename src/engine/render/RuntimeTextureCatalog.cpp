#include "engine/render/RuntimeTextureCatalog.hpp"

#include "engine/AssetManifest.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace sokoban {
namespace {

std::string documentKey(const std::filesystem::path& path)
{
    std::string key = path.lexically_normal().generic_string();
#ifdef _WIN32
    std::ranges::transform(key, key.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return key;
}

TextureSourceIdentity manifestIdentity(const AssetManifest::Texture& texture)
{
    return {
        .source = ExternalTextureSource {
            std::filesystem::path(texture.path).lexically_normal(),
        },
        .interpretation = {
            .colorSpace = texture.colorSpace,
            .wrapU = texture.tiling
                ? TextureAddressMode::Repeat
                : TextureAddressMode::ClampToEdge,
            .wrapV = texture.tiling
                ? TextureAddressMode::Repeat
                : TextureAddressMode::ClampToEdge,
            .magFilter = texture.filter == TextureFilter::Linear
                ? TextureMagnificationFilter::Linear
                : TextureMagnificationFilter::Nearest,
            .minFilter = texture.filter == TextureFilter::Linear
                ? TextureMinificationFilter::LinearMipmapLinear
                : TextureMinificationFilter::Nearest,
        },
    };
}

void requireOnce(std::vector<uint32_t>& required, uint32_t texture)
{
    if (std::ranges::find(required, texture) == required.end()) {
        required.push_back(texture);
    }
}

} // namespace

RuntimeTextureDefinition runtimeTextureDefinitionFor(
    const AssetManifest::Texture& texture)
{
    return {
        .identity = manifestIdentity(texture),
        .label = "texture '" + texture.name + "'",
        .manifestOwned = true,
    };
}

uint32_t RuntimeTextureCatalog::descriptorIndex(
    uint32_t logicalIndex,
    uint32_t descriptorCapacity) const
{
    if (logicalIndex >= textures_.size() ||
        textures_.size() > descriptorCapacity) {
        throw std::out_of_range(
            "Runtime texture index or descriptor capacity is invalid");
    }
    if (logicalIndex < manifestTextureCount_) {
        return logicalIndex;
    }
    const uint32_t discoveredBase =
        descriptorCapacity - discoveredTextureCount();
    if (manifestTextureCount_ > discoveredBase) {
        throw std::out_of_range(
            "Runtime texture ranges overlap in the descriptor heap");
    }
    return discoveredBase + logicalIndex - manifestTextureCount_;
}

RuntimeTextureCatalog buildRuntimeTextureCatalog(
    const AssetManifest& manifest,
    std::span<const ResolvedMaterialTexture> materialTextures)
{
    if (manifest.textures().size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Manifest texture catalog is too large");
    }

    RuntimeTextureCatalog catalog;
    catalog.manifestTextureCount_ =
        static_cast<uint32_t>(manifest.textures().size());
    catalog.textures_.reserve(
        manifest.textures().size() + materialTextures.size());

    std::unordered_map<std::string, uint32_t> logicalIndexByIdentity;
    for (uint32_t index = 0; index < manifest.textures().size(); ++index) {
        const AssetManifest::Texture& texture = manifest.textures()[index];
        RuntimeTextureDefinition definition =
            runtimeTextureDefinitionFor(texture);
        logicalIndexByIdentity.try_emplace(
            textureSourceIdentityKey(definition.identity), index);
        catalog.textures_.push_back(std::move(definition));
    }

    catalog.models_.resize(manifest.models().size());
    std::unordered_map<std::string, std::vector<uint32_t>> modelsByDocument;
    for (uint32_t index = 0; index < manifest.models().size(); ++index) {
        const AssetManifest::Model& definition = manifest.models()[index];
        modelsByDocument[documentKey(definition.path)].push_back(index);
        RuntimeModelTextures& model = catalog.models_[index];
        model.primitiveMaterials.resize(definition.primitiveMaterials.size());
        for (PrimitiveMaterialBinding& binding : model.primitiveMaterials) {
            binding.bindBaseColorTexture = false;
        }
        if (definition.materialMode == ModelMaterialMode::SingleTexture) {
            requireOnce(model.requiredTextures, definition.textureIndex);
        } else if (
            definition.materialMode == ModelMaterialMode::PrimitiveMaterials) {
            for (uint32_t materialIndex = 0;
                 materialIndex < definition.primitiveMaterials.size();
                 ++materialIndex) {
                const AssetManifest::Model::PrimitiveMaterial& source =
                    definition.primitiveMaterials[materialIndex];
                PrimitiveMaterialBinding& binding =
                    model.primitiveMaterials[materialIndex];
                binding.textureIndex = source.textureIndex;
                binding.flags = source.scrollV
                    ? PrimitiveMaterialScrollV
                    : PrimitiveMaterialNone;
                binding.bindBaseColorTexture = true;
                requireOnce(model.requiredTextures, source.textureIndex);
            }
        }
    }

    for (const ResolvedMaterialTexture& texture : materialTextures) {
        const auto modelsIt = modelsByDocument.find(documentKey(texture.document));
        if (modelsIt == modelsByDocument.end() ||
            texture.semantic == MaterialTextureSemantic::BaseColor) {
            continue;
        }

        const std::string identityKey =
            textureSourceIdentityKey(texture.identity);
        uint32_t logicalIndex = 0;
        const auto existing = logicalIndexByIdentity.find(identityKey);
        if (existing != logicalIndexByIdentity.end()) {
            logicalIndex = existing->second;
        } else {
            if (catalog.textures_.size() >=
                std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("Runtime texture catalog is too large");
            }
            logicalIndex = static_cast<uint32_t>(catalog.textures_.size());
            logicalIndexByIdentity.emplace(identityKey, logicalIndex);
            catalog.textures_.push_back({
                .identity = texture.identity,
                .label = texture.assetLabel + ", material " +
                    std::to_string(texture.materialIndex) + " '" +
                    texture.materialName + "', texture '" +
                    texture.textureName + "'",
            });
        }

        for (uint32_t modelIndex : modelsIt->second) {
            RuntimeModelTextures& model = catalog.models_[modelIndex];
            if (model.primitiveMaterials.size() <= texture.materialIndex) {
                const std::size_t previousSize =
                    model.primitiveMaterials.size();
                model.primitiveMaterials.resize(texture.materialIndex + 1);
                for (std::size_t index = previousSize;
                     index < model.primitiveMaterials.size();
                     ++index) {
                    model.primitiveMaterials[index].bindBaseColorTexture =
                        false;
                }
            }
            PrimitiveMaterialBinding& binding =
                model.primitiveMaterials[texture.materialIndex];
            switch (texture.semantic) {
            case MaterialTextureSemantic::Normal:
                binding.normalTextureIndex = logicalIndex;
                break;
            case MaterialTextureSemantic::MetallicRoughness:
                binding.metallicRoughnessTextureIndex = logicalIndex;
                break;
            case MaterialTextureSemantic::Emissive:
                binding.emissiveTextureIndex = logicalIndex;
                break;
            case MaterialTextureSemantic::Occlusion:
                binding.occlusionTextureIndex = logicalIndex;
                break;
            case MaterialTextureSemantic::BaseColor:
                break;
            }
            requireOnce(model.requiredTextures, logicalIndex);
        }
    }

    return catalog;
}

RuntimeTextureCatalog collectRuntimeTextureCatalog(
    const std::filesystem::path& assetRoot,
    const AssetManifest& manifest)
{
    std::vector<ResolvedMaterialTexture> textures;
    std::unordered_set<std::string> inspectedDocuments;
    for (const AssetManifest::Model& model : manifest.models()) {
        const std::string key = documentKey(model.path);
        if (!inspectedDocuments.insert(key).second) {
            continue;
        }
        std::vector<ResolvedMaterialTexture> resolved =
            resolveGltfMaterialTextures(
                assetRoot,
                model.path,
                "model '" + model.name + "'");
        textures.insert(
            textures.end(),
            std::make_move_iterator(resolved.begin()),
            std::make_move_iterator(resolved.end()));
    }
    return buildRuntimeTextureCatalog(manifest, textures);
}

} // namespace sokoban
