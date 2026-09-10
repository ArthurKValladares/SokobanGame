#include "engine/render/RuntimeTextureCatalog.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/render/TextureDescriptorSpace.hpp"

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

void requireOnce(std::vector<uint32_t>& required, uint32_t texture)
{
    if (std::ranges::find(required, texture) == required.end()) {
        required.push_back(texture);
    }
}

uint64_t saturatedAdd(uint64_t left, uint64_t right)
{
    if (left > std::numeric_limits<uint64_t>::max() - right) {
        return std::numeric_limits<uint64_t>::max();
    }
    return left + right;
}

} // namespace

RuntimeTextureDefinition runtimeTextureDefinitionFor(
    const AssetManifest::Texture& texture)
{
    return {
        .identity = manifestTextureSourceIdentity(texture, texture.path),
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
    // Kept ahead of the overlap test, exactly as before: a manifest index is
    // answerable even from a heap whose ranges collide, and callers rely on
    // that. Only the discovered half needs a well-formed partition.
    if (logicalIndex < manifestTextureCount_) {
        return logicalIndex;
    }
    // The partition arithmetic is shared with the renderer, which lays out
    // the same heap from the other side; only this error is ours.
    const uint32_t discoveredBase = TextureDescriptorSpace::discoveredBaseFor(
        descriptorCapacity, discoveredTextureCount());
    if (TextureDescriptorSpace::rangesOverlap(
            manifestTextureCount_, discoveredBase)) {
        throw std::out_of_range(
            "Runtime texture ranges overlap in the descriptor heap");
    }
    return TextureDescriptorSpace::descriptorIndexFor(
        logicalIndex, manifestTextureCount_, discoveredBase);
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
    catalog.animationPreparedBytes_.resize(manifest.animations().size());
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
    std::unordered_map<std::string, GltfAssetDependencies> inspections;
    const auto inspect = [&](const std::filesystem::path& relative)
        -> const GltfAssetDependencies& {
        const std::string key = documentKey(relative);
        const auto existing = inspections.find(key);
        if (existing != inspections.end()) {
            return existing->second;
        }
        auto [inserted, unused] = inspections.emplace(
            key, inspectGltfAssetDependencies(assetRoot / relative));
        (void)unused;
        return inserted->second;
    };

    std::unordered_set<std::string> materialDocuments;
    for (const AssetManifest::Model& model : manifest.models()) {
        const std::string key = documentKey(model.path);
        const GltfAssetDependencies& dependencies = inspect(model.path);
        if (materialDocuments.insert(key).second) {
            std::vector<ResolvedMaterialTexture> resolved =
                resolveGltfMaterialTextures(
                    assetRoot,
                    model.path,
                    "model '" + model.name + "'",
                    dependencies);
            textures.insert(
                textures.end(),
                std::make_move_iterator(resolved.begin()),
                std::make_move_iterator(resolved.end()));
        }
        for (const AssetManifest::Model::Attachment& attachment :
             model.attachments) {
            (void)inspect(attachment.path);
        }
    }
    for (const AssetManifest::Animation& animation : manifest.animations()) {
        (void)inspect(animation.path);
    }

    RuntimeTextureCatalog catalog =
        buildRuntimeTextureCatalog(manifest, textures);
    for (uint32_t modelIndex = 0;
         modelIndex < manifest.models().size();
         ++modelIndex) {
        const AssetManifest::Model& model = manifest.models()[modelIndex];
        const GltfPreparedSizeMetadata& prepared =
            inspect(model.path).preparedSizes;
        uint64_t bytes = model.geometry == ModelGeometry::Skinned
            ? prepared.skinnedMeshBytes
            : prepared.staticMeshBytes;
        for (const AssetManifest::Model::Attachment& attachment :
             model.attachments) {
            const GltfPreparedSizeMetadata& attachmentPrepared =
                inspect(attachment.path).preparedSizes;
            bytes = saturatedAdd(bytes, attachmentPrepared.staticMeshBytes);
            bytes = saturatedAdd(bytes, attachmentPrepared.materialBytes);
            bytes = saturatedAdd(bytes, sizeof(SkinnedAttachment));
        }
        catalog.models_[modelIndex].preparedBytes = bytes;
    }
    for (uint32_t animationIndex = 0;
         animationIndex < manifest.animations().size();
         ++animationIndex) {
        const AssetManifest::Animation& animation =
            manifest.animations()[animationIndex];
        const std::vector<uint64_t>& estimates =
            inspect(animation.path).preparedSizes.animationBytes;
        const uint32_t clipIndex =
            animationIndexFromManifestClip(animation.clip);
        if (clipIndex >= estimates.size()) {
            throw std::runtime_error(
                "Animation '" + animation.name +
                "' references a clip outside its glTF document");
        }
        catalog.animationPreparedBytes_[animationIndex] =
            estimates[clipIndex];
    }
    return catalog;
}

std::vector<uint32_t> reconcileRuntimeTextureCatalog(
    const RuntimeTextureCatalog& catalog,
    TextureDescriptorSpace& descriptorSpace,
    std::vector<std::optional<RuntimeTextureDefinition>>& definitions)
{
    if (definitions.size() != descriptorSpace.capacity() ||
        catalog.manifestTextureCount() != descriptorSpace.manifestCount()) {
        throw std::runtime_error(
            "Runtime texture catalog does not match the live descriptor heap");
    }

    std::unordered_map<std::string, uint32_t> descriptorByIdentity;
    for (uint32_t logicalIndex = 0;
         logicalIndex < catalog.manifestTextureCount();
         ++logicalIndex) {
        if (!definitions[logicalIndex] ||
            textureSourceIdentityKey(definitions[logicalIndex]->identity) !=
                textureSourceIdentityKey(catalog.textures()[logicalIndex].identity)) {
            throw std::runtime_error(
                "Manifest texture definitions changed while synchronizing models");
        }
        descriptorByIdentity.try_emplace(
            textureSourceIdentityKey(definitions[logicalIndex]->identity),
            logicalIndex);
    }
    for (uint32_t descriptor : descriptorSpace.active()) {
        if (descriptor < descriptorSpace.manifestCount()) {
            continue;
        }
        if (descriptor >= definitions.size() || !definitions[descriptor]) {
            throw std::runtime_error(
                "Active texture descriptor has no runtime definition");
        }
        descriptorByIdentity.try_emplace(
            textureSourceIdentityKey(definitions[descriptor]->identity),
            descriptor);
    }

    std::unordered_set<std::string> missingIdentities;
    for (uint32_t logicalIndex = catalog.manifestTextureCount();
         logicalIndex < catalog.textures().size();
         ++logicalIndex) {
        const std::string key = textureSourceIdentityKey(
            catalog.textures()[logicalIndex].identity);
        if (!descriptorByIdentity.contains(key)) {
            missingIdentities.insert(key);
        }
    }
    if (missingIdentities.size() > descriptorSpace.manifestHeadroom()) {
        throw std::runtime_error(
            "Discovered model textures exceed the remaining descriptor capacity");
    }

    std::vector<uint32_t> logicalToDescriptor(catalog.textures().size());
    for (uint32_t logicalIndex = 0;
         logicalIndex < catalog.manifestTextureCount();
         ++logicalIndex) {
        logicalToDescriptor[logicalIndex] = logicalIndex;
    }
    for (uint32_t logicalIndex = catalog.manifestTextureCount();
         logicalIndex < catalog.textures().size();
         ++logicalIndex) {
        const RuntimeTextureDefinition& definition =
            catalog.textures()[logicalIndex];
        const std::string key = textureSourceIdentityKey(definition.identity);
        const auto existing = descriptorByIdentity.find(key);
        if (existing != descriptorByIdentity.end()) {
            logicalToDescriptor[logicalIndex] = existing->second;
            continue;
        }

        const std::optional<uint32_t> descriptor =
            descriptorSpace.claimDiscoveredSlot();
        if (!descriptor) {
            throw std::runtime_error(
                "Discovered model textures exceed the remaining descriptor capacity");
        }
        definitions[*descriptor] = definition;
        descriptorByIdentity.emplace(key, *descriptor);
        logicalToDescriptor[logicalIndex] = *descriptor;
    }
    return logicalToDescriptor;
}

RuntimeModelTextures remapRuntimeModelTextures(
    const RuntimeModelTextures& model,
    std::span<const uint32_t> logicalToDescriptor)
{
    const auto descriptorFor = [logicalToDescriptor](uint32_t logicalIndex) {
        if (logicalIndex >= logicalToDescriptor.size()) {
            throw std::out_of_range(
                "Model material references an invalid runtime texture index");
        }
        return logicalToDescriptor[logicalIndex];
    };
    const auto remapOptional = [&descriptorFor](std::optional<uint32_t>& index) {
        if (index) {
            *index = descriptorFor(*index);
        }
    };

    RuntimeModelTextures mapped = model;
    for (uint32_t& texture : mapped.requiredTextures) {
        texture = descriptorFor(texture);
    }
    for (PrimitiveMaterialBinding& binding : mapped.primitiveMaterials) {
        if (binding.bindBaseColorTexture) {
            binding.textureIndex = descriptorFor(binding.textureIndex);
        }
        remapOptional(binding.normalTextureIndex);
        remapOptional(binding.metallicRoughnessTextureIndex);
        remapOptional(binding.emissiveTextureIndex);
        remapOptional(binding.occlusionTextureIndex);
    }
    return mapped;
}

} // namespace sokoban
