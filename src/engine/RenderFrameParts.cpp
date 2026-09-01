#include "engine/RenderFrameParts.hpp"

#include "engine/Rules.hpp"
#include "engine/render/MirrorConfig.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/SelectorRenderConfig.hpp"
#include "engine/render/WaterConfig.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// Bodies moved verbatim from RenderFrameBuilder.cpp. The only edit is that
// default arguments moved to the declarations in the header, where a
// definition in another translation unit cannot repeat them.

namespace sokoban::renderFrameParts {

// ---------------------------------------------- Small shared shaping helpers

Vec4 shade(Vec4 color, float multiplier)
{
    return {
        color.x * multiplier,
        color.y * multiplier,
        color.z * multiplier,
        color.w,
    };
}

void applyTileScale(RenderFrameData::Tile& tile, float scale)
{
    scale = std::clamp(scale, config::minTileScale, config::maxTileScale);
    if (std::abs(scale - 1.0f) < 0.0001f) {
        return;
    }

    const Vec2 center {
        tile.position.x + tile.size.x * 0.5f,
        tile.position.y + tile.size.y * 0.5f,
    };
    tile.size = { tile.size.x * scale, tile.size.y * scale };
    tile.position = {
        center.x - tile.size.x * 0.5f,
        center.y - tile.size.y * 0.5f,
    };
    tile.height *= scale;
}

uint32_t facingQuarterTurns(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Down:
        return 0;
    case MoveDirection::Left:
        return 1;
    case MoveDirection::Up:
        return 2;
    case MoveDirection::Right:
        return 3;
    }
    return 0;
}

// --------------------------------------------------------- Animation lookups

RenderAnimation animationFor(
    const AnimationCatalog* catalog,
    AnimationUse use,
    RenderAnimation fallback)
{
    return catalog != nullptr ? catalog->animation(use) : fallback;
}

float animationTimeFor(
    const AnimationCatalog* catalog,
    AnimationUse use,
    float timeSeconds)
{
    return catalog != nullptr
        ? timeSeconds * catalog->effectiveSpeed(use)
        : timeSeconds;
}

uint64_t authoredAnimationInstance(TileType tile, GridPosition3 cell)
{
    const uint64_t x = static_cast<uint32_t>(cell.x);
    const uint64_t y = static_cast<uint32_t>(cell.y);
    const uint64_t z = static_cast<uint32_t>(cell.z);
    return (uint64_t { 1 } << 63) |
        (static_cast<uint64_t>(tile) << 56) |
        ((z & 0xffffU) << 40) |
        ((y & 0xfffffU) << 20) |
        (x & 0xfffffU);
}

// ------------------------------------------------------------------- Ladders

void appendLadderRungFace(
    RenderFrameData& frame,
    GridPosition3 groundCell,
    GridPosition3 ladderCell,
    float rungCenter,
    bool preview)
{
    constexpr float rungLengthInset = 0.10f;
    constexpr float rungHalfThickness = 0.07f;
    constexpr float faceOffset = 0.003f;

    const Vec4 color = preview
        ? Vec4 { 0.43f, 0.22f, 0.08f, 0.62f }
        : tileColor(TileType::Ladder);
    const float bottom =
        static_cast<float>(groundCell.z) + rungCenter - rungHalfThickness;
    const float top =
        static_cast<float>(groundCell.z) + rungCenter + rungHalfThickness;
    const float gx = static_cast<float>(groundCell.x);
    const float gy = static_cast<float>(groundCell.y);

    auto appendFace = [&](std::array<Vec3, 4> vertices, Vec3 normal) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .normal = normal,
            .color = color,
        });
    };

    if (ladderCell.x < groundCell.x) {
        const float x = gx - faceOffset;
        const float y0 = gy + rungLengthInset;
        const float y1 = gy + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x, y1, bottom },
            Vec3 { x, y0, bottom },
            Vec3 { x, y0, top },
            Vec3 { x, y1, top },
        }, { -1.0f, 0.0f, 0.0f });
        return;
    }
    if (ladderCell.x > groundCell.x) {
        const float x = gx + 1.0f + faceOffset;
        const float y0 = gy + rungLengthInset;
        const float y1 = gy + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x, y0, bottom },
            Vec3 { x, y1, bottom },
            Vec3 { x, y1, top },
            Vec3 { x, y0, top },
        }, { 1.0f, 0.0f, 0.0f });
        return;
    }
    if (ladderCell.y < groundCell.y) {
        const float y = gy - faceOffset;
        const float x0 = gx + rungLengthInset;
        const float x1 = gx + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x0, y, bottom },
            Vec3 { x1, y, bottom },
            Vec3 { x1, y, top },
            Vec3 { x0, y, top },
        }, { 0.0f, -1.0f, 0.0f });
        return;
    }
    if (ladderCell.y > groundCell.y) {
        const float y = gy + 1.0f + faceOffset;
        const float x0 = gx + rungLengthInset;
        const float x1 = gx + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x1, y, bottom },
            Vec3 { x0, y, bottom },
            Vec3 { x0, y, top },
            Vec3 { x1, y, top },
        }, { 0.0f, 1.0f, 0.0f });
    }
}

void appendLadderRungs(
    RenderFrameData& frame,
    GridPosition3 ladderCell,
    GridPosition3 groundCell,
    bool preview)
{
    appendLadderRungFace(frame, groundCell, ladderCell, 0.32f, preview);
    appendLadderRungFace(frame, groundCell, ladderCell, 0.68f, preview);
}

// --------------------------------------------------------------------- Water

void appendWaterSurface(
    RenderFrameData& frame,
    GridPosition3 cell,
    Vec2 position,
    Vec2 size,
    bool editorPreview,
    uint32_t shorelineMask,
    bool pickable)
{
    frame.waterSurfaces.push_back({
        .cell = cell,
        .position = position,
        .size = size,
        .color = frame.waterRendering.surfaceColor,
        .elevation = static_cast<float>(cell.z) + 1.0f -
            config::waterDepthBelowGround +
            (editorPreview ? 0.02f : 0.0f),
        .shorelineMask = shorelineMask,
        .isEditorPreview = editorPreview,
        .pickable = pickable,
    });
}

void appendWaterCellSurface(
    RenderFrameData& frame,
    GridPosition3 cell,
    bool editorPreview,
    uint32_t shorelineMask,
    bool pickable)
{
    appendWaterSurface(
        frame,
        cell,
        {
            static_cast<float>(cell.x),
            static_cast<float>(cell.y),
        },
        { 1.0f, 1.0f },
        editorPreview,
        shorelineMask,
        pickable);
}

RenderFrameData::WaterGridBounds waterGridBoundsFor(
    const std::optional<RenderFrameData::CameraExtent>& gameplayExtent)
{
    if (!gameplayExtent) {
        return {};
    }
    return {
        .originX = gameplayExtent->originX,
        .originY = gameplayExtent->originY,
        .width = gameplayExtent->width,
        .height = gameplayExtent->height,
    };
}

// ------------------------------------------------------------- Camera extent

void includeCameraCell(
    std::optional<RenderFrameData::CameraExtent>& extent,
    GridPosition3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0) {
        return;
    }
    if (!extent) {
        extent = RenderFrameData::CameraExtent {
            .originX = cell.x,
            .originY = cell.y,
            .originZ = cell.z,
        };
        return;
    }

    const int maxX = std::max(
        extent->originX + static_cast<int>(extent->width),
        cell.x + 1);
    const int maxY = std::max(
        extent->originY + static_cast<int>(extent->height),
        cell.y + 1);
    const int maxZ = std::max(
        extent->originZ + static_cast<int>(extent->depth),
        cell.z + 1);
    extent->originX = std::min(extent->originX, cell.x);
    extent->originY = std::min(extent->originY, cell.y);
    extent->originZ = std::min(extent->originZ, cell.z);
    extent->width = static_cast<uint32_t>(maxX - extent->originX);
    extent->height = static_cast<uint32_t>(maxY - extent->originY);
    extent->depth = static_cast<uint32_t>(maxZ - extent->originZ);
}

// -------------------------------------------------------------- Ground splat

// Ground splat textures are optional: a manifest without them leaves the ids
// unset and ground falls back to the flat tile color. `location` selects that
// screen's splat map; an empty location (the editor) takes the shared map.
[[nodiscard]] GroundSplatTextures groundSplatTextures(
    const AssetManifest& manifest,
    std::optional<LevelLocation> location)
{
    return groundSplatTexturesForScreen(
        [&manifest](std::string_view name) {
            return manifest.findTextureIdByName(name);
        },
        location);
}

// --------------------------------------------------------------- Decorations

RenderFrameData::Tile decorationVisual(
    const Level::Decoration& decoration,
    const AssetManifest& manifest,
    bool preview,
    std::optional<std::size_t> editorIndex,
    RenderFrameData::EditorDecorationHighlight highlight)
{
    constexpr float radiansPerDegree =
        3.14159265358979323846f / 180.0f;
    const RenderModel model = manifest.modelIdByName(decoration.model);
    const Vec3 pivot = manifest.model(model).preserveSourceScale
        ? Vec3 { 0.0f, 0.0f, 0.0f }
        : Vec3 { 0.5f, 0.5f, 0.0f };
    return {
        .cell = {
            static_cast<int>(std::floor(decoration.position.x)),
            static_cast<int>(std::floor(decoration.position.y)),
            static_cast<int>(std::floor(decoration.position.z)),
        },
        .position = {
            decoration.position.x - decoration.scale.x * 0.5f,
            decoration.position.y - decoration.scale.y * 0.5f,
        },
        .size = { decoration.scale.x, decoration.scale.y },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        .baseElevation = decoration.position.z,
        .height = decoration.scale.z,
        .pickable = false,
        .showGrid = false,
        .isEditorPreview = preview,
        .affectsCameraFit = false,
        .model = model,
        .modelTransform = RenderFrameData::ModelTransform {
            .translation = decoration.position,
            .rotationRadians = {
                decoration.rotationDegrees.x * radiansPerDegree,
                decoration.rotationDegrees.y * radiansPerDegree,
                decoration.rotationDegrees.z * radiansPerDegree,
            },
            .scale = decoration.scale,
            .pivot = pivot,
        },
        .editorDecorationIndex = editorIndex
            ? std::optional<uint32_t>(static_cast<uint32_t>(*editorIndex))
            : std::nullopt,
        .editorDecorationHighlight = highlight,
    };
}

Vec3 decorationLightPosition(const Level::Decoration& decoration)
{
    if (!decoration.pointLight) {
        return decoration.position;
    }
    constexpr float radiansPerDegree =
        3.14159265358979323846f / 180.0f;
    Vec3 offset {
        decoration.pointLight->offset.x * decoration.scale.x,
        decoration.pointLight->offset.y * decoration.scale.y,
        decoration.pointLight->offset.z * decoration.scale.z,
    };
    const Vec3 rotation {
        decoration.rotationDegrees.x * radiansPerDegree,
        decoration.rotationDegrees.y * radiansPerDegree,
        decoration.rotationDegrees.z * radiansPerDegree,
    };
    float cosine = std::cos(rotation.x);
    float sine = std::sin(rotation.x);
    offset = { offset.x,
        cosine * offset.y - sine * offset.z,
        sine * offset.y + cosine * offset.z };
    cosine = std::cos(rotation.y);
    sine = std::sin(rotation.y);
    offset = { cosine * offset.x + sine * offset.z,
        offset.y,
        -sine * offset.x + cosine * offset.z };
    cosine = std::cos(rotation.z);
    sine = std::sin(rotation.z);
    offset = { cosine * offset.x - sine * offset.y,
        sine * offset.x + cosine * offset.y,
        offset.z };
    return { decoration.position.x + offset.x,
        decoration.position.y + offset.y,
        decoration.position.z + offset.z };
}

void appendDecorations(
    RenderFrameData& frame,
    const std::vector<Level::Decoration>& decorations,
    const AssetManifest& manifest,
    std::optional<std::size_t> selected,
    std::optional<std::size_t> hovered,
    bool editorDecorations,
    const std::function<bool(GridPosition3)>& visibleCell)
{
    for (std::size_t index = 0; index < decorations.size(); ++index) {
        const Level::Decoration& decoration = decorations[index];
        const GridPosition3 cell {
            static_cast<int>(std::floor(decoration.position.x)),
            static_cast<int>(std::floor(decoration.position.y)),
            static_cast<int>(std::floor(decoration.position.z)),
        };
        if (visibleCell && !visibleCell(cell)) {
            continue;
        }
        const std::size_t decorationTileIndex = frame.tiles.size();
        if (decoration.pointLight &&
            frame.lighting.pointLightCount <
                RenderFrameData::pointLightCapacity) {
            const Level::Decoration::PointLight& source =
                *decoration.pointLight;
            frame.lighting.pointLights[frame.lighting.pointLightCount++] = {
                .position = decorationLightPosition(decoration),
                .color = source.color,
                .intensity = source.intensity,
                .range = source.range,
                .castsShadows = source.castsShadows,
                .shadowBias = source.shadowBias,
                .shadowOpacity = source.shadowOpacity,
                .emitterTileIndex = decorationTileIndex,
            };
        }
        const RenderFrameData::EditorDecorationHighlight highlight =
            selected == index
            ? RenderFrameData::EditorDecorationHighlight::Selected
            : (hovered == index
                ? RenderFrameData::EditorDecorationHighlight::Hovered
                : RenderFrameData::EditorDecorationHighlight::None);
        frame.tiles.push_back(decorationVisual(
            decorations[index],
            manifest,
            false,
            editorDecorations ? std::optional<std::size_t>(index) : std::nullopt,
            highlight));
    }
}

// ----------------------------------------------------------- Screen selector

void appendSelector(
    RenderFrameData& frame,
    const Level::ScreenSelector& selector,
    const AssetManifest& manifest,
    const std::function<ScreenSelectorViewState(LevelLocation)>& stateFor,
    bool preview,
    bool pickable)
{
    constexpr float flagScale = 0.65f;
    ScreenSelectorViewState state;
    if (selector.target) {
        state = stateFor
            ? stateFor(*selector.target)
            : ScreenSelectorViewState {
                .status = ScreenSelectorStatus::Playable,
            };
    }
    const RenderModel model = manifest.modelIdByName(
        selectorRender::modelName(state));
    const Vec3 translation {
        static_cast<float>(selector.cell.x) + 0.5f,
        static_cast<float>(selector.cell.y) + 0.5f,
        static_cast<float>(selector.cell.z),
    };
    if (preview && pickable) {
        frame.tiles.push_back({
            .cell = selector.cell,
            .position = {
                translation.x - flagScale * 0.5f,
                translation.y - flagScale * 0.5f,
            },
            .size = { flagScale, flagScale },
            .baseElevation = translation.z,
            .height = flagScale * 1.5f,
            .pickOnly = true,
            .showGrid = false,
            .affectsCameraFit = false,
        });
    }
    frame.tiles.push_back({
        .cell = selector.cell,
        .position = {
            translation.x - flagScale * 0.5f,
            translation.y - flagScale * 0.5f,
        },
        .size = { flagScale, flagScale },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        .baseElevation = translation.z,
        .height = flagScale * 1.5f,
        .pickable = pickable,
        .showGrid = false,
        .isEditorPreview = preview,
        .affectsCameraFit = false,
        .model = model,
        .modelTransform = RenderFrameData::ModelTransform {
            .translation = translation,
            .scale = { flagScale, flagScale, flagScale },
            .pivot = { 0.0f, 0.0f, 0.0f },
        },
    });
}

} // namespace sokoban::renderFrameParts
