#pragma once

#include "engine/TextureSource.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct GltfAssetDependencies;

struct ContentSourceRoots {
    std::filesystem::path assets;
    std::filesystem::path levels;
    std::filesystem::path shaders;
};

struct ContentFile {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uintmax_t size = 0;
    // Generated files have no source path; stageContent writes their bytes
    // after source files have been copied.
    bool generated = false;
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

// Resolves every external buffer and image referenced by one glTF/GLB
// document. Returned paths are normalized relative to assetRoot, deduplicated,
// and verified to name regular files within that root.
[[nodiscard]] std::vector<std::filesystem::path> resolveGltfExternalFiles(
    const std::filesystem::path& assetRoot,
    const std::filesystem::path& document,
    std::string_view assetLabel);

// Resolves only the core material-map uses in one glTF document. This is the
// shared semantic boundary used by staging and by the runtime texture catalog;
// `document` is relative to `assetRoot` and remains relative in every result.
[[nodiscard]] std::vector<ResolvedMaterialTexture>
resolveGltfMaterialTextures(
    const std::filesystem::path& assetRoot,
    const std::filesystem::path& document,
    std::string_view assetLabel);

// Reuses metadata already parsed by a caller that also needs structural size
// information. Path containment and source-file validation remain identical
// to the ordinary overload.
[[nodiscard]] std::vector<ResolvedMaterialTexture>
resolveGltfMaterialTextures(
    const std::filesystem::path& assetRoot,
    const std::filesystem::path& document,
    std::string_view assetLabel,
    const GltfAssetDependencies& dependencies);

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

// Developer-build accelerations for the stage above. None of them changes the
// package a stage produces: a stage that skips, or that takes every texture
// from the cache, leaves the same files and content.index as a clean one.
struct ContentStageOptions {
    // A directory of compressed textures keyed by the bytes of every file a
    // texture source reads, its interpretation, and the encoder revision.
    // Keep it outside the output tree; it survives restaging and is safe to
    // delete. Empty disables caching.
    std::filesystem::path textureCache;
    // Leave outputRoot untouched when the inventory, every source file's size
    // and modification time, the game version, toolIdentity, and the staged
    // content.index all match the record the previous successful stage wrote
    // beside outputRoot (<outputRoot>.stage-record). Anything that rewrites
    // the staged index, such as an editor publication, forces the next stage.
    bool skipWhenUpToDate = false;
    // Folded into that record so a rebuilt tool restages. The content tool
    // passes its own executable's size and modification time.
    std::string toolIdentity;
    // Concurrent texture encodes. Zero picks a count from the hardware; one
    // encodes on the calling thread.
    unsigned encoderThreads = 0;
};

struct ContentStageReport {
    ContentInventory inventory;
    // True when skipWhenUpToDate found nothing to do. The inventory still
    // describes the staged package, compressed textures included.
    bool upToDate = false;
    std::size_t texturesEncoded = 0;
    std::size_t texturesFromCache = 0;
    // Encoded textures the cache could not store. The stage still succeeded;
    // the next one encodes them again.
    std::size_t textureCacheWriteFailures = 0;
};

[[nodiscard]] ContentStageReport stageContent(
    const ContentSourceRoots& roots,
    const std::filesystem::path& outputRoot,
    std::string_view gameVersion,
    const ContentStageOptions& options);

// Parses a staged content.index and verifies its version, declared count and
// total size, every listed regular file, and that no package file is omitted
// from the index. `root` must be the runtime assets directory.
void validateContentPackage(
    const std::filesystem::path& root,
    std::string_view expectedGameVersion);

// Rebuilds an existing runtime content.index from the files currently in the
// package while preserving its staged game version. Returns false when `root`
// is not a staged package, allowing source-only editor fixtures to opt out.
// The replacement is atomic and the completed package is validated before
// this function returns.
[[nodiscard]] bool refreshContentPackageIndex(
    const std::filesystem::path& root);

} // namespace sokoban
