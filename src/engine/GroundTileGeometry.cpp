#include "engine/GroundTileGeometry.hpp"
#include "engine/GroundTileCapData.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <vector>

namespace sokoban {
namespace {

constexpr std::array<std::string_view, 6> modelPrefixes {
    "GroundRock", "GroundRockEnd", "GroundRockStrip", "GroundRockCorner", "GroundRockEdge", "GroundRockInterior",
};
constexpr std::array<std::string_view, 6> pathShapes { "island", "end", "strip", "corner", "edge", "interior" };

bool compatibleGround(const RenderFrameData::Tile& tile)
{
    return tile.groundModule && !tile.pickOnly && !tile.blurBehind &&
        (tile.color.w == 1.0f || tile.isEditorPreview) && tile.groundTop &&
        (tile.effect == RenderSurfaceEffect::GroundSplat || tile.effect == RenderSurfaceEffect::Standard) &&
        !tile.modelTransform && tile.modelRotationOffsetRadians == 0.0f &&
        tile.animation.isNone() && tile.animationFallback.isNone() &&
        tile.animationInstanceId == 0 && tile.renderableId == 0 &&
        tile.size == Vec2 { 1.0f, 1.0f } && tile.height == 1.0f &&
        tile.position == Vec2 { static_cast<float>(tile.cell.x), static_cast<float>(tile.cell.y) } &&
        (tile.isEditorPreview || tile.baseElevation == static_cast<float>(tile.cell.z));
}

} // namespace

std::string_view groundTileModelName(std::size_t shapeIndex, uint32_t variant)
{
    static const auto names = [] {
        std::array<std::array<std::string, groundRockVariantCount>, 6> result;
        for (std::size_t shape = 0; shape < result.size(); ++shape) {
            for (uint32_t style = 0; style < groundRockVariantCount; ++style) {
                result[shape][style] = std::string(modelPrefixes[shape]) +
                    (style < 9 ? "0" : "") + std::to_string(style + 1);
            }
        }
        return result;
    }();
    variant %= static_cast<uint32_t>(groundRockVariantCount);
    return names[shapeIndex % modelPrefixes.size()][variant];
}

std::optional<uint32_t> authoredGroundTileVariant(const AssetManifest::Model& model) noexcept
{
    if (model.geometry != ModelGeometry::Static || !model.preserveSourceScale || model.rotateHalfTurn ||
        !model.attachments.empty() || model.hasScrollingMaterial()) return std::nullopt;
    const std::string_view name = model.name;
    const std::string_view path = model.path;
    constexpr std::string_view root = "custom/models/ground_modules/ground_";
    constexpr std::string_view extension = ".glb";
    for (std::size_t shape = 0; shape < modelPrefixes.size(); ++shape) {
        const auto prefix = modelPrefixes[shape];
        if (!name.starts_with(prefix) || name.size() != prefix.size() + 2) continue;
        const auto suffix = name.substr(prefix.size());
        const bool numbered = (suffix[0] == '0' && suffix[1] >= '1' && suffix[1] <= '9') || suffix == "10";
        if (!numbered || path.size() != root.size() + pathShapes[shape].size() + 1 + 2 + extension.size() ||
            !path.starts_with(root) || !path.ends_with(extension) ||
            path.substr(root.size(), pathShapes[shape].size()) != pathShapes[shape] ||
            path[root.size() + pathShapes[shape].size()] != '_' ||
            path.substr(path.size() - extension.size() - 2, 2) != suffix) continue;
        return suffix == "10" ? 9U : static_cast<uint32_t>(suffix[1] - '1');
    }
    return std::nullopt;
}

const GroundTileCapSurface& groundTileCanonicalTopSurface(
    std::size_t shapeIndex, uint32_t variant, uint8_t concaveCorners) noexcept
{
    shapeIndex %= tileModuleCanonicalMasks.size();
    variant %= static_cast<uint32_t>(groundRockVariantCount);
    uint16_t index = groundTileNativeCapIndices[variant][shapeIndex][concaveCorners & groundAllSides];
    if (index == std::numeric_limits<uint16_t>::max()) {
        index = groundTileNativeCapIndices[variant][shapeIndex][0];
    }
    return groundTileNativeCaps[index].surface;
}

GroundTileCapSurface groundTileTopSurface(const RenderFrameData::Tile& tile) noexcept
{
    const TileModule module = tileModuleForMask(tile.groundModuleSideMask);
    const uint8_t canonicalConcave = rotateTileModuleMask(tile.groundModuleConcaveCorners, 4 - module.quarterTurns);
    GroundTileCapSurface surface = groundTileCanonicalTopSurface(module.shapeIndex, tile.groundRockVariant, canonicalConcave);
    for (auto& patch : std::span(surface.patches.data(), surface.count)) {
        for (auto& vertex : patch.vertices) {
            for (uint32_t turn = 0; turn < module.quarterTurns; ++turn) vertex = { 1 - vertex.y, vertex.x, vertex.z };
            vertex = { tile.position.x + vertex.x * tile.size.x, tile.position.y + vertex.y * tile.size.y,
                tile.baseElevation + vertex.z * std::max(tile.height, 0.0f) };
        }
        for (uint32_t turn = 0; turn < module.quarterTurns; ++turn) patch.normal = { -patch.normal.y, patch.normal.x, patch.normal.z };
        if (tile.size != Vec2 { 1, 1 } || tile.height != 1) {
            patch.normal = normalize(cross(patch.vertices[1] - patch.vertices[0], patch.vertices[2] - patch.vertices[0]));
        }
    }
    return surface;
}

std::size_t groundTileTopPatchCountFor(const RenderFrameData::Tile& tile) noexcept
{
    const TileModule module = tileModuleForMask(tile.groundModuleSideMask);
    const uint8_t canonicalConcave = rotateTileModuleMask(tile.groundModuleConcaveCorners, 4 - module.quarterTurns);
    return groundTileCanonicalTopSurface(module.shapeIndex, tile.groundRockVariant, canonicalConcave).count;
}

void processGroundTileGeometry(std::span<RenderFrameData::Tile> tiles, const AssetManifest& manifest, FrameArena* arena)
{
    if (std::ranges::none_of(tiles, [](const auto& tile) { return tile.groundModule; })) return;
    const auto count = static_cast<std::size_t>(std::ranges::count_if(tiles, [](const auto& tile) {
        return compatibleGround(tile) && !tile.isEditorPreview;
    }));
    std::vector<GridPosition3> owned;
    GridPosition3* storage = nullptr;
    if (count != 0) {
        if (arena) storage = arena->allocateUninitialized<GridPosition3>(count);
        else { owned.resize(count); storage = owned.data(); }
    }
    std::span<GridPosition3> occupied;
    if (storage) {
        std::size_t index = 0;
        for (const auto& tile : tiles) if (compatibleGround(tile) && !tile.isEditorPreview) storage[index++] = tile.cell;
        occupied = { storage, count };
    }
    // Scratch exhaustion selects complete islands, keeping every side visible.
    std::ranges::sort(occupied, tileModuleCellLess);
    occupied = occupied.first(static_cast<std::size_t>(std::unique(occupied.begin(), occupied.end()) - occupied.begin()));
    std::array<std::array<std::optional<RenderModel>, groundRockVariantCount>, 6> models;
    for (std::size_t shape = 0; shape < models.size(); ++shape) {
        for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
            const auto model = manifest.findModelIdByName(groundTileModelName(shape, variant));
            if (model && authoredGroundTileVariant(manifest.model(*model)) == variant) models[shape][variant] = model;
        }
    }
    for (auto& tile : tiles) {
        if (!tile.groundModule) continue;
        const uint32_t variant = tile.groundRockVariant % static_cast<uint32_t>(groundRockVariantCount);
        const uint8_t exposed = compatibleGround(tile) ? tileModuleExposedSides(tile.cell, occupied) : groundAllSides;
        const uint8_t concave = compatibleGround(tile) ? tileModuleConcaveCorners(tile.cell, exposed, occupied) : 0;
        const auto selected = tileModuleForMask(exposed);
        if (const auto model = models[selected.shapeIndex][variant]) {
            tile.model = *model;
            tile.modelRotationQuarterTurns = selected.quarterTurns;
            tile.groundModuleSideMask = exposed;
            tile.groundModuleConcaveCorners = concave;
        } else {
            // A partial manifest fails open to its complete standalone model.
            // If that source is also custom, retain its square legacy cap.
            tile.model = models[0][variant].value_or(tile.model);
            tile.modelRotationQuarterTurns = 0;
            tile.groundModuleSideMask = groundAllSides;
            tile.groundModuleConcaveCorners = 0;
            if (tile.model.isCube() || !authoredGroundTileVariant(manifest.model(tile.model))) tile.groundModule = false;
        }
        tile.groundGeometryEligible = false;
        tile.groundRimWidth = tile.groundRimDepth = 0;
    }
}

} // namespace sokoban
