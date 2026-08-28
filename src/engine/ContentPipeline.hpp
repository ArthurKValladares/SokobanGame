#pragma once

#include "engine/TextureSource.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct ContentSourceRoots {
    std::filesystem::path assets;
    std::filesystem::path levels;
    std::filesystem::path shaders;
};

struct ContentFile {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uintmax_t size = 0;
};

struct ResolvedMaterialTexture {
    std::filesystem::path document;
    std::string assetLabel;
    uint32_t materialIndex = 0;
    std::string materialName;
    std::string textureName;
    MaterialTextureSemantic semantic = MaterialTextureSemantic::BaseColor;
    TextureSourceIdentity identity;
    uint32_t texcoord = 0;
    // Normal scale for normal maps, occlusion strength for occlusion maps,
    // one for the other core glTF slots.
    float scale = 1.0f;
};

struct ContentInventory {
    std::vector<ContentFile> files;
    // Unique decoded-image identities required by manifest textures and glTF
    // material maps. A source used in both color spaces appears twice.
    std::vector<TextureSourceIdentity> textureSources;
    // One entry per authored material use. Unlike textureSources this is not
    // deduplicated: it preserves the material-to-resource mapping and UV data.
    std::vector<ResolvedMaterialTexture> materialTextures;
    std::uintmax_t totalBytes = 0;
};

// Resolves only the core material-map uses in one glTF document. This is the
// shared semantic boundary used by staging and by the runtime texture catalog;
// `document` is relative to `assetRoot` and remains relative in every result.
[[nodiscard]] std::vector<ResolvedMaterialTexture>
resolveGltfMaterialTextures(
    const std::filesystem::path& assetRoot,
    const std::filesystem::path& document,
    std::string_view assetLabel);

// Resolves and validates every file needed by a distributable build. Manifest
// references remain relative to the assets root, while levels and compiled
// shaders are added under levels/ and shaders/ respectively.
[[nodiscard]] ContentInventory collectContentInventory(const ContentSourceRoots& roots);

// Replaces outputRoot with a clean, complete content tree and writes a
// versioned content.index. Staging through a sibling temporary directory keeps
// interrupted runs from leaving a partially updated package.
[[nodiscard]] ContentInventory stageContent(
    const ContentSourceRoots& roots,
    const std::filesystem::path& outputRoot,
    std::string_view gameVersion);

// Parses a staged content.index and verifies its version, declared count and
// total size, every listed regular file, and that no package file is omitted
// from the index. `root` must be the runtime assets directory.
void validateContentPackage(
    const std::filesystem::path& root,
    std::string_view expectedGameVersion);

} // namespace sokoban
