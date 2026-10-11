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

[[nodiscard]] std::string_view groundTileModelName(std::size_t shapeIndex, uint32_t variant,
    bool square = false, bool bottom = false);
// Native bottom bodies extend one cell down and may protrude along exposed
// sides. Visual bounds include both margins; logical picking does not.
inline constexpr float groundTileBottomDepth = 1.0f;
inline constexpr float groundTileBottomOverhang = 0.04f;
[[nodiscard]] constexpr bool groundTileBottomSupport(TileType tile) noexcept
{
    return tileTypeIsGround(tile) || tileTypeIsWall(tile) ||
        tile == TileType::Decorative || tile == TileType::Ice;
}
// Recognize the native contract by both name and canonical path. Legacy and
// custom ground keep their original processing/fallback behavior.
[[nodiscard]] std::optional<uint32_t> authoredGroundTileVariant(
    const AssetManifest::Model& model) noexcept;

// The canonical cap contains the native broad chamfer and flat center. The
// lookup is serialized from Blender meshes; it does not synthesize geometry.
[[nodiscard]] const GroundTileCapSurface& groundTileCanonicalTopSurface(
    std::size_t shapeIndex, uint32_t variant, uint8_t concaveCorners, bool square = false) noexcept;

// Apply only the selected module's cell-center rotation and world transform.
// Native vertices are never deformed, and all render/shadow/paint paths share
// these authored facets, normals, and material coverage.
[[nodiscard]] GroundTileCapSurface groundTileTopSurface(const RenderFrameData::Tile& tile) noexcept;
[[nodiscard]] std::size_t groundTileTopPatchCountFor(const RenderFrameData::Tile& tile) noexcept;

// Resolve six authored bodies and their native caps against emitted same-layer
// ground. Diagonal gaps select the matching concave cap. Cohorts touching ground
// one layer above use square caps to close steps and stacked bodies. Hover
// previews query the context without changing their neighbors' geometry.
// Unsupported tiles select native jagged bottom bodies. Builders resolve full
// authored support even when the supporting layer is hidden; direct callers
// can set groundModuleSupported, or rely on emitted native ground below.
void processGroundTileGeometry(std::span<RenderFrameData::Tile> tiles, const AssetManifest& manifest,
    FrameArena* arena = nullptr);

} // namespace sokoban
