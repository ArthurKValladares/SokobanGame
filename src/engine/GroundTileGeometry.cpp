#include "engine/GroundTileGeometry.hpp"
#include "engine/GroundTileCapData.hpp"
#include "engine/GroundTileSquareCapData.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace sokoban {
namespace {

constexpr std::array<std::string_view, 6> modelPrefixes {
    "GroundRock", "GroundRockEnd", "GroundRockStrip", "GroundRockCorner", "GroundRockEdge", "GroundRockInterior",
};
constexpr std::array<std::string_view, 6> squareModelPrefixes {
    "GroundRockSquare", "GroundRockSquareEnd", "GroundRockSquareStrip", "GroundRockSquareCorner",
    "GroundRockSquareEdge", "GroundRockSquareInterior",
};
constexpr std::array<std::string_view, 6> bottomModelPrefixes {
    "GroundRockBottom", "GroundRockBottomEnd", "GroundRockBottomStrip", "GroundRockBottomCorner",
    "GroundRockBottomEdge", "GroundRockBottomInterior",
};
constexpr std::array<std::string_view, 6> bottomSquareModelPrefixes {
    "GroundRockBottomSquare", "GroundRockBottomSquareEnd", "GroundRockBottomSquareStrip", "GroundRockBottomSquareCorner",
    "GroundRockBottomSquareEdge", "GroundRockBottomSquareInterior",
};
constexpr std::array modelProfilePrefixes { modelPrefixes, squareModelPrefixes, bottomModelPrefixes, bottomSquareModelPrefixes };
constexpr std::array<std::string_view, 4> modelPathRoots {
    "custom/models/ground_modules/ground_", "custom/models/ground_modules/ground_square_",
    "custom/models/ground_modules/ground_bottom_", "custom/models/ground_modules/ground_bottom_square_",
};
constexpr std::array<std::string_view, 6> pathShapes { "island", "end", "strip", "corner", "edge", "interior" };

std::optional<GridPosition3> offsetCell(GridPosition3 cell, int x, int y, int z)
{
    const auto safe = [](int value, int offset) {
        return !((offset < 0 && value == std::numeric_limits<int>::min()) ||
            (offset > 0 && value == std::numeric_limits<int>::max()));
    };
    if (!safe(cell.x, x) || !safe(cell.y, y) || !safe(cell.z, z)) return std::nullopt;
    return GridPosition3 { cell.x + x, cell.y + y, cell.z + z };
}

std::optional<std::size_t> occupiedIndex(GridPosition3 cell, std::span<const GridPosition3> occupied)
{
    const auto found = std::lower_bound(occupied.begin(), occupied.end(), cell, tileModuleCellLess);
    if (found == occupied.end() || *found != cell) return std::nullopt;
    return static_cast<std::size_t>(found - occupied.begin());
}

bool touchesUpperGround(GridPosition3 cell, std::span<const GridPosition3> occupied)
{
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const auto upper = offsetCell(cell, x, y, 1);
            if (upper && occupiedIndex(*upper, occupied)) return true;
        }
    }
    return false;
}

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

std::string_view groundTileModelName(std::size_t shapeIndex, uint32_t variant, bool square, bool bottom)
{
    static const auto names = [] {
        std::array<std::array<std::array<std::string, groundRockVariantCount>, 6>, 4> result;
        for (std::size_t profile = 0; profile < result.size(); ++profile) {
            for (std::size_t shape = 0; shape < modelPrefixes.size(); ++shape) {
                for (uint32_t style = 0; style < groundRockVariantCount; ++style) {
                    result[profile][shape][style] = std::string(modelProfilePrefixes[profile][shape]) +
                        (style < 9 ? "0" : "") + std::to_string(style + 1);
                }
            }
        }
        return result;
    }();
    variant %= static_cast<uint32_t>(groundRockVariantCount);
    return names[(bottom ? 2 : 0) + (square ? 1 : 0)][shapeIndex % modelPrefixes.size()][variant];
}

std::optional<uint32_t> authoredGroundTileVariant(const AssetManifest::Model& model) noexcept
{
    if (model.geometry != ModelGeometry::Static || !model.preserveSourceScale || model.rotateHalfTurn ||
        !model.attachments.empty() || model.hasScrollingMaterial()) return std::nullopt;
    const std::string_view name = model.name;
    const std::string_view path = model.path;
    constexpr std::string_view extension = ".glb";
    for (std::size_t profile = 0; profile < modelProfilePrefixes.size(); ++profile) {
        const std::string_view root = modelPathRoots[profile];
        for (std::size_t shape = 0; shape < modelPrefixes.size(); ++shape) {
            const auto prefix = modelProfilePrefixes[profile][shape];
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
    }
    return std::nullopt;
}

const GroundTileCapSurface& groundTileCanonicalTopSurface(
    std::size_t shapeIndex, uint32_t variant, uint8_t concaveCorners, bool square) noexcept
{
    shapeIndex %= tileModuleCanonicalMasks.size();
    variant %= static_cast<uint32_t>(groundRockVariantCount);
    if (square) return groundTileSquareCaps[variant][shapeIndex];
    uint16_t index = groundTileNativeCapIndices[variant][shapeIndex][concaveCorners & groundAllSides];
    if (index == std::numeric_limits<uint16_t>::max()) {
        index = groundTileNativeCapIndices[variant][shapeIndex][0];
    }
    return groundTileNativeCaps[index].surface;
}

GroundTileCapSurface groundTileTopSurface(const RenderFrameData::Tile& tile) noexcept
{
    const TileModule module = tileModuleForMask(tile.groundModuleSideMask);
    const uint32_t quarterTurns = tile.modelRotationQuarterTurns & 3;
    const uint8_t canonicalConcave = rotateTileModuleMask(tile.groundModuleConcaveCorners, 4 - quarterTurns);
    GroundTileCapSurface surface = groundTileCanonicalTopSurface(module.shapeIndex, tile.groundRockVariant,
        canonicalConcave, tile.groundModuleSquare);
    for (auto& patch : std::span(surface.patches.data(), surface.count)) {
        for (auto& vertex : patch.vertices) {
            for (uint32_t turn = 0; turn < quarterTurns; ++turn) vertex = { 1 - vertex.y, vertex.x, vertex.z };
            vertex = { tile.position.x + vertex.x * tile.size.x, tile.position.y + vertex.y * tile.size.y,
                tile.baseElevation + vertex.z * std::max(tile.height, 0.0f) };
        }
        for (uint32_t turn = 0; turn < quarterTurns; ++turn) patch.normal = { -patch.normal.y, patch.normal.x, patch.normal.z };
        if (tile.size != Vec2 { 1, 1 } || tile.height != 1) {
            patch.normal = normalize(cross(patch.vertices[1] - patch.vertices[0], patch.vertices[2] - patch.vertices[0]));
        }
    }
    return surface;
}

std::size_t groundTileTopPatchCountFor(const RenderFrameData::Tile& tile) noexcept
{
    const TileModule module = tileModuleForMask(tile.groundModuleSideMask);
    const uint8_t canonicalConcave = rotateTileModuleMask(tile.groundModuleConcaveCorners,
        4 - (tile.modelRotationQuarterTurns & 3));
    return groundTileCanonicalTopSurface(module.shapeIndex, tile.groundRockVariant,
        canonicalConcave, tile.groundModuleSquare).count;
}

void processGroundTileGeometry(std::span<RenderFrameData::Tile> tiles, const AssetManifest& manifest, FrameArena* arena)
{
    if (std::ranges::none_of(tiles, [](const auto& tile) { return tile.groundModule; })) return;
    const auto count = static_cast<std::size_t>(std::ranges::count_if(tiles, [](const auto& tile) {
        return compatibleGround(tile) && !tile.isEditorPreview;
    }));
    // The arena path does not need owning containers, including the checked
    // iterator bookkeeping allocated by empty vectors in MSVC Debug builds.
    std::optional<std::vector<GridPosition3>> owned;
    std::optional<std::vector<std::size_t>> ownedParents;
    std::optional<std::vector<uint8_t>> ownedSquare;
    GridPosition3* storage = nullptr;
    std::size_t* parents = nullptr;
    uint8_t* squareCohorts = nullptr;
    if (count != 0) {
        if (arena) {
            storage = arena->allocateUninitialized<GridPosition3>(count);
            parents = arena->allocateUninitialized<std::size_t>(count);
            squareCohorts = arena->allocateUninitialized<uint8_t>(count);
        } else {
            storage = owned.emplace(count).data();
            parents = ownedParents.emplace(count).data();
            squareCohorts = ownedSquare.emplace(count).data();
        }
    }
    std::span<GridPosition3> occupied;
    const bool occupancyComplete = count == 0 || (storage && parents && squareCohorts);
    if (storage && parents && squareCohorts) {
        std::size_t index = 0;
        for (const auto& tile : tiles) if (compatibleGround(tile) && !tile.isEditorPreview) storage[index++] = tile.cell;
        occupied = { storage, count };
    }
    // Scratch exhaustion selects complete islands, keeping every side visible.
    std::ranges::sort(occupied, tileModuleCellLess);
    occupied = occupied.first(static_cast<std::size_t>(std::unique(occupied.begin(), occupied.end()) - occupied.begin()));
    for (std::size_t index = 0; index < occupied.size(); ++index) {
        parents[index] = index;
        squareCohorts[index] = 0;
    }
    const auto rootFor = [parents](std::size_t index) {
        while (parents[index] != index) {
            parents[index] = parents[parents[index]];
            index = parents[index];
        }
        return index;
    };
    // A square cap and chamfered cap disagree at the endpoints of a shared
    // exposed rim. Keep the profile consistent across each same-layer cohort.
    constexpr std::array<GridPosition, 2> forwardOffsets {{ { 1, 0 }, { 0, 1 } }};
    for (std::size_t index = 0; index < occupied.size(); ++index) {
        for (const auto offset : forwardOffsets) {
            const auto cell = offsetCell(occupied[index], offset.x, offset.y, 0);
            const auto neighbor = cell ? occupiedIndex(*cell, occupied) : std::nullopt;
            if (neighbor) parents[rootFor(*neighbor)] = rootFor(index);
        }
    }
    for (std::size_t index = 0; index < occupied.size(); ++index) {
        if (touchesUpperGround(occupied[index], occupied)) squareCohorts[rootFor(index)] = 1;
    }
    const auto isSquare = [&](GridPosition3 cell) {
        if (const auto index = occupiedIndex(cell, occupied)) return squareCohorts[rootFor(*index)] != 0;
        // A preview joins a neighboring cohort for its own appearance, while
        // emitted tiles retain the geometry selected without that preview.
        if (touchesUpperGround(cell, occupied)) return true;
        constexpr std::array<GridPosition, 4> offsets {{ { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }};
        for (const auto offset : offsets) {
            const auto position = offsetCell(cell, offset.x, offset.y, 0);
            const auto neighbor = position ? occupiedIndex(*position, occupied) : std::nullopt;
            if (neighbor && squareCohorts[rootFor(*neighbor)] != 0) return true;
        }
        return false;
    };
    using ProfileModels = std::array<std::array<std::optional<RenderModel>, groundRockVariantCount>, 6>;
    std::array<ProfileModels, 4> models;
    for (std::size_t profile = 0; profile < models.size(); ++profile) {
        for (std::size_t shape = 0; shape < models[profile].size(); ++shape) {
            for (uint32_t variant = 0; variant < groundRockVariantCount; ++variant) {
                const auto model = manifest.findModelIdByName(groundTileModelName(shape, variant, (profile & 1) != 0, profile >= 2));
                if (model && authoredGroundTileVariant(manifest.model(*model)) == variant) models[profile][shape][variant] = model;
            }
        }
    }
    for (auto& tile : tiles) {
        if (!tile.groundModule) continue;
        const uint32_t variant = tile.groundRockVariant % static_cast<uint32_t>(groundRockVariantCount);
        const uint8_t exposed = compatibleGround(tile) ? tileModuleExposedSides(tile.cell, occupied) : groundAllSides;
        const uint8_t concave = compatibleGround(tile) ? tileModuleConcaveCorners(tile.cell, exposed, occupied) : 0;
        const bool square = compatibleGround(tile) && isSquare(tile.cell);
        const auto lower = offsetCell(tile.cell, 0, 0, -1);
        const bool bottom = occupancyComplete && compatibleGround(tile) && !tile.groundModuleSupported &&
            !(lower && occupiedIndex(*lower, occupied));
        const auto& regularModels = models[square ? 1 : 0];
        const auto& bottomModels = models[(square ? 1 : 0) + 2];
        const auto selected = tileModuleForMask(exposed, tile.authoredRotationQuarterTurns);
        const auto regularModel = regularModels[selected.shapeIndex][variant];
        const auto bottomModel = bottom ? bottomModels[selected.shapeIndex][variant] : std::nullopt;
        if (const auto model = bottomModel ? bottomModel : regularModel) {
            tile.model = *model;
            tile.modelRotationQuarterTurns = selected.quarterTurns;
            tile.groundModuleSideMask = exposed;
            tile.groundModuleConcaveCorners = square ? 0 : concave;
            tile.groundModuleSquare = square;
            tile.groundModuleBottom = bottomModel.has_value();
        } else {
            // A partial manifest fails open to its complete standalone model.
            // If that source is also custom, retain its square legacy cap.
            tile.model = regularModels[0][variant].value_or(models[0][0][variant].value_or(tile.model));
            tile.modelRotationQuarterTurns = tile.authoredRotationQuarterTurns & 3;
            tile.groundModuleSideMask = groundAllSides;
            tile.groundModuleConcaveCorners = 0;
            tile.groundModuleSquare = !tile.model.isCube() &&
                authoredGroundTileVariant(manifest.model(tile.model)) &&
                manifest.model(tile.model).name.starts_with("GroundRockSquare");
            tile.groundModuleBottom = false;
            if (tile.model.isCube() || !authoredGroundTileVariant(manifest.model(tile.model))) tile.groundModule = false;
        }
        tile.groundGeometryEligible = false;
        tile.groundRimWidth = tile.groundRimDepth = 0;
    }
}

} // namespace sokoban
