#include "engine/CliffWallGeometry.hpp"

#include <algorithm>
#include <vector>

namespace sokoban {
namespace {

bool compatibleCliff(const RenderFrameData::Tile& tile)
{
    return tile.cliffWall && !tile.pickOnly && !tile.blurBehind &&
        (tile.color.w == 1.0f || tile.isEditorPreview) && tile.groundTop &&
        (tile.effect == RenderSurfaceEffect::GroundSplat ||
            tile.effect == RenderSurfaceEffect::Standard) &&
        !tile.modelTransform && tile.modelRotationOffsetRadians == 0.0f &&
        tile.size == Vec2 { 1.0f, 1.0f } && tile.height == 1.0f &&
        tile.position == Vec2 { static_cast<float>(tile.cell.x),
            static_cast<float>(tile.cell.y) } &&
        (tile.isEditorPreview || tile.baseElevation == static_cast<float>(tile.cell.z));
}

} // namespace

std::array<std::array<Vec3, 4>, cliffWallTopPatchCount> cliffWallTopPatches(
    const RenderFrameData::Tile& tile)
{
    const auto boundary = cliffWallTopBoundary(tile.cliffWallSideMask, tile.cliffWallVariant);
    const float topHeight = tile.baseElevation + std::max(tile.height, 0.0f);
    const auto worldPoint = [&](Vec2 point) {
        return Vec3 { tile.position.x + point.x * tile.size.x,
            tile.position.y + point.y * tile.size.y, topHeight };
    };
    const Vec3 center = worldPoint({ .5f, .5f });
    std::array<std::array<Vec3, 4>, cliffWallTopPatchCount> patches {};
    for (std::size_t patch = 0; patch < patches.size(); ++patch) {
        const std::size_t first = patch * 2;
        patches[patch] = { center, worldPoint(boundary[first]),
            worldPoint(boundary[(first + 1) % boundary.size()]),
            worldPoint(boundary[(first + 2) % boundary.size()]) };
    }
    return patches;
}

void processCliffWallGeometry(
    std::span<RenderFrameData::Tile> tiles, const AssetManifest& manifest)
{
    if (std::ranges::none_of(tiles, [](const auto& tile) { return tile.cliffWall; })) return;
    std::vector<GridPosition3> occupied;
    for (const auto& tile : tiles) {
        if (compatibleCliff(tile) && !tile.isEditorPreview) occupied.push_back(tile.cell);
    }
    std::ranges::sort(occupied, tileModuleCellLess);
    occupied.erase(std::unique(occupied.begin(), occupied.end()), occupied.end());
    std::array<std::array<std::optional<RenderModel>, 2>, 6> models;
    for (std::size_t shape = 0; shape < models.size(); ++shape) {
        for (std::size_t variant = 0; variant < 2; ++variant) {
            models[shape][variant] = manifest.findModelIdByName(cliffWallModelNames[shape][variant]);
        }
    }
    for (auto& tile : tiles) {
        if (!tile.cliffWall) continue;
        const std::size_t variant = tile.cliffWallVariant % 2;
        const uint8_t exposed = compatibleCliff(tile)
            ? tileModuleExposedSides(tile.cell, occupied) : groundAllSides;
        const auto selected = cliffWallModuleForMask(exposed);
        if (const auto model = models[selected.shapeIndex][variant]) {
            tile.model = *model;
            tile.modelRotationQuarterTurns = selected.quarterTurns;
            tile.cliffWallSideMask = exposed;
        } else {
            // A partial/custom manifest still draws a complete isolated body.
            tile.model = models[0][variant].value_or(manifest.modelForTile(
                variant == 0 ? TileType::CliffWall : TileType::CliffWall02));
            tile.modelRotationQuarterTurns = 0;
            tile.cliffWallSideMask = groundAllSides;
        }
    }
}

} // namespace sokoban
