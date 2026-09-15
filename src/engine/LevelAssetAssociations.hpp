#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/LevelCatalog.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace sokoban {

struct LevelLocationAssociationRemap {
    LevelLocation source;
    std::optional<LevelLocation> destination;
};

struct DeletedLevelAssetAssociations {
    int originalLevel = 0;
    std::vector<AssetManifest::Texture> splatMaps;
    std::optional<AssetManifest::MusicTrack> music;
};

inline constexpr const char* deletedLevelAssetAssociationsFilename =
    ".asset-associations.json";

void remapLevelAssetAssociations(
    const std::filesystem::path& manifestPath,
    std::span<const LevelLocationAssociationRemap> remaps);

[[nodiscard]] DeletedLevelAssetAssociations captureLevelAssetAssociations(
    const std::filesystem::path& manifestPath,
    int level);
void writeDeletedLevelAssetAssociations(
    const std::filesystem::path& levelRoot,
    const DeletedLevelAssetAssociations& associations);
[[nodiscard]] std::optional<DeletedLevelAssetAssociations>
readDeletedLevelAssetAssociations(const std::filesystem::path& levelRoot);
void removeDeletedLevelAssetAssociations(
    const std::filesystem::path& levelRoot);
void restoreLevelAssetAssociations(
    const std::filesystem::path& manifestPath,
    const DeletedLevelAssetAssociations& associations,
    int restoredLevel);

} // namespace sokoban
