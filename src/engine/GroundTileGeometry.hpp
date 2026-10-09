#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/GroundTileCapTypes.hpp"
#include "engine/TileModuleGeometry.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sokoban {

[[nodiscard]] std::string_view groundTileModelName(std::size_t shapeIndex, uint32_t variant);
// Recognize the native contract by both name and canonical path. Legacy and
// custom ground keep their original processing/fallback behavior.
[[nodiscard]] std::optional<uint32_t> authoredGroundTileVariant(
    const AssetManifest::Model& model) noexcept;

// The canonical cap contains the native broad chamfer and flat center. The
// lookup is serialized from Blender meshes; it does not synthesize geometry.
[[nodiscard]] const GroundTileCapSurface& groundTileCanonicalTopSurface(
    std::size_t shapeIndex, uint32_t variant, uint8_t concaveCorners) noexcept;

// Apply only the selected module's cell-center rotation and world transform.
// Native vertices are never deformed, and all render/shadow/paint paths share
// these authored facets, normals, and material coverage.
[[nodiscard]] GroundTileCapSurface groundTileTopSurface(const RenderFrameData::Tile& tile) noexcept;
[[nodiscard]] std::size_t groundTileTopPatchCountFor(const RenderFrameData::Tile& tile) noexcept;

// Resolve six authored bodies and their native caps against emitted same-layer
// ground. Diagonal gaps select the matching concave cap. Hover previews query
// the context without removing their neighbors' sides.
void processGroundTileGeometry(std::span<RenderFrameData::Tile> tiles, const AssetManifest& manifest,
    FrameArena* arena = nullptr);

} // namespace sokoban
