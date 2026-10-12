#include "engine/RenderFrameParts.hpp"

#include "engine/Rules.hpp"
#include "engine/render/MirrorConfig.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/render/SceneConfig.hpp"
#include "engine/render/SelectorRenderConfig.hpp"
#include "engine/render/WaterGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// Stateless frame-building helpers shared by gameplay and editor scene paths.

namespace sokoban::renderFrameParts {

void applyGroundSplat(
    RenderFrameData::Tile& tile, const AssetManifest& manifest,
    const std::vector<Level::GroundSplat>& splats,
    const std::vector<Level::GroundPaint>& paint,
    GridPosition origin, bool showColors)
{
    if (tile.effect != RenderSurfaceEffect::GroundSplat) return;
    const Level::GroundSplat* splat = Level::groundSplatAt(splats, paint,
        { tile.cell.x - origin.x, tile.cell.y - origin.y, tile.cell.z });
    if (!splat) return;
    tile.groundSplat = groundSplatTexturesForMaterials(
        [&manifest](std::string_view name) { return manifest.findTextureIdByName(name); },
        splat->base, splat->detail, manifest.findTextureIdByName(splat->mask));
    tile.groundSplatOrigin = origin;
    if (showColors) {
        tile.color = { splat->color.x, splat->color.y, splat->color.z, tile.color.w };
        tile.effect = RenderSurfaceEffect::Standard;
    }
}

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

void appendLinkedObjectAura(
    RenderFrameData& frame,
    const RenderFrameData::Tile& object,
    Vec3 color)
{
    RenderFrameData::Tile aura = object;
    const float oldHeight = aura.height;
    applyTileScale(aura, config::linkedObjectAuraScale);
    aura.baseElevation -= (aura.height - oldHeight) * 0.5f;
    aura.color = {
        color.x,
        color.y,
        color.z,
        config::linkedObjectAuraOpacity,
    };
    aura.blurBehind = false;
    aura.pickOnly = false;
    aura.pickable = false;
    aura.showGrid = false;
    aura.affectsCameraFit = false;
    aura.isPrimaryPlayer = false;
    aura.interactionOutline = false;
    aura.effect = RenderSurfaceEffect::LinkedObjectAura;
    if (aura.renderableId != 0) {
        aura.renderableId ^= uint64_t { 1 } << 63;
    }
    frame.tiles.push_back(aura);
}

void appendPortalVisual(
    RenderFrameData& frame,
    const RenderFrameData::Tile& tile,
    TileType type,
    float timeSeconds,
    RenderTexture glowTexture)
{
    const GridPosition edge = portalEdgeOffset(type);
    const Vec3 normal { static_cast<float>(edge.x),
                        static_cast<float>(edge.y),
                        0 };
    const Vec3 tangent { -normal.y, normal.x, 0 };
    const Vec3 edgeCenter {
        static_cast<float>(tile.cell.x) + 0.5f + normal.x * 0.5f,
        static_cast<float>(tile.cell.y) + 0.5f + normal.y * 0.5f,
        tile.baseElevation,
    };
    // The doorway and its pick target share the actual crossing edge. Only
    // the diffuse beam extends into the owning tile, along the facing axis.
    RenderFrameData::Tile aperture = tile;
    aperture.model = cubeModel;
    aperture.size = edge.x == 0 ? Vec2 { 1.0f, 0.12f } : Vec2 { 0.12f, 1.0f };
    aperture.position = { edgeCenter.x - aperture.size.x * 0.5f,
                          edgeCenter.y - aperture.size.y * 0.5f };
    aperture.height = 1.60f;
    aperture.pickOnly = true;
    aperture.interactionOutline = false;
    aperture.showGrid = false;
    frame.tiles.push_back(aperture);
    if (tile.pickOnly) {
        return;
    }

    const float seed =
        static_cast<float>(tile.cell.x) * 0.37f + static_cast<float>(tile.cell.y) * 0.61f + static_cast<float>(tile.cell.z) * 0.19f;
    const Vec3 center = edgeCenter - normal * 0.018f + Vec3 { 0, 0, 0.80f };
    const auto appendEnergyPlane = [&](Vec3 at, Vec3 right, Vec3 up,
                                       float opacity, RenderSurfaceEffect effect) {
        frame.isoFaces.push_back({
            .vertices = { at - right - up, at + right - up,
                          at + right + up, at - right + up },
            .normal = normalize(cross(right, up)),
            .color = { tile.color.x, tile.color.y, tile.color.z, tile.color.w * opacity },
            .effect = effect,
            .translucent = true,
            .castsShadows = false,
        });
    };

    // A single upright quad holds the animated nested ovals and the dark
    // aperture, so overlapping translucent disks cannot cover the rings.
    appendEnergyPlane(center, tangent * 0.53f, Vec3 { 0, 0, 0.86f },
        1.0f, RenderSurfaceEffect::PortalEnergy);

    // Radial sheets share a smooth density field. Their long axis follows
    // the cardinal facing direction, fading away from the doorway instead
    // of turning into an upward column or individual electrical trails.
    constexpr float beamLength = 1.10f;
    constexpr int beamSheetCount = 12;
    const Vec3 beamAxis = normal * -beamLength;
    const Vec3 beamCenter = center + beamAxis * 0.5f;
    for (int sheet = 0; sheet < beamSheetCount; ++sheet) {
        const float angle = static_cast<float>(sheet) * pi /
            static_cast<float>(beamSheetCount);
        const Vec3 transverse = tangent * (std::cos(angle) * 0.49f) +
            Vec3 { 0, 0, std::sin(angle) * 0.76f };
        appendEnergyPlane(beamCenter, transverse, beamAxis * 0.5f,
            0.12f, RenderSurfaceEffect::PortalBeam);
    }

    // Small round motes float slowly through the energy. Squared sampling
    // concentrates them near its center and mouth; no elongated streaks.
    constexpr int moteCount = 64;
    for (int spark = 0; spark < moteCount; ++spark) {
        const float index = static_cast<float>(spark);
        const float cycle = index / static_cast<float>(moteCount) +
            timeSeconds * (0.10f + index * 0.0007f) + seed;
        const float phase = cycle - std::floor(cycle);
        const float angle = index * 2.399963f + timeSeconds * 0.16f;
        const float sample = index * 0.618034f + 0.21f;
        const float radialSample = sample - std::floor(sample);
        const float radius = radialSample * radialSample;
        const Vec3 position = center - normal * (1.05f * std::pow(phase, 1.7f)) +
            tangent * (std::cos(angle) * radius * 0.42f +
                std::sin(timeSeconds * 0.8f + index) * 0.012f) +
            Vec3 { 0, 0, std::sin(angle) * radius * 0.62f +
                std::sin(timeSeconds * 0.63f + index * 1.3f) * 0.025f };
        const float fade = std::pow(1.0f - phase, 0.6f);
        const float twinkle = 0.75f + 0.25f * std::sin(timeSeconds * 2.1f + index * 1.9f);
        const float size = 0.008f + 0.006f * (0.5f + 0.5f * std::sin(index * 2.7f));
        frame.particles.push_back({
            .position = position,
            .size = { size, size },
            .color = { tile.color.x * 0.75f + 0.25f,
                       tile.color.y * 0.75f + 0.25f,
                       tile.color.z * 0.75f + 0.25f,
                       tile.color.w * (0.11f + std::sin(pi * phase) * 0.14f) * fade * twinkle },
            .emissiveStrength = 2.0f,
            .texture = glowTexture,
        });
    }
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

void appendWardrobeMarker(
    RenderFrameData& frame,
    GridPosition3 cell,
    CharacterType character,
    const AssetManifest& manifest,
    RenderAnimation idleAnimation,
    float animationTimeSeconds,
    bool editorPreview)
{
    constexpr float markerSize = 0.30f;
    RenderFrameData::Tile marker {
        .cell = cell,
        .position = {
            static_cast<float>(cell.x) + (1.0f - markerSize) * 0.5f,
            static_cast<float>(cell.y) + (1.0f - markerSize) * 0.5f,
        },
        .size = { markerSize, markerSize },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        // Perch the miniature on the brass crown at the top of the arches.
        .baseElevation = static_cast<float>(cell.z) + 1.15f,
        .height = markerSize,
        .pickable = false,
        .showGrid = false,
        .isEditorPreview = editorPreview,
        .affectsCameraFit = false,
        .interactionOutline = true,
        .model = manifest.characterModel(character),
        .animation = idleAnimation,
        .animationInstanceId = authoredAnimationInstance(
            wardrobeTileForCharacter(character), cell),
        .animationLoops = true,
        .animationTimeSeconds = animationTimeSeconds,
    };
    frame.tiles.push_back(marker);
}

// ------------------------------------------------------------------- Ladders

void appendLadderSegment(
    RenderFrameData& frame,
    GridPosition3 ladderCell,
    GridPosition3 groundCell,
    const AssetManifest& manifest,
    bool preview)
{
    // The source section is mounted on the north edge, facing into its cell.
    // Rotate about the cell centre to reach every other ground-facing edge.
    const uint32_t quarterTurns = groundCell.x > ladderCell.x
        ? 1U
        : groundCell.y > ladderCell.y
        ? 2U
        : groundCell.x < ladderCell.x
        ? 3U
        : 0U;
    frame.tiles.push_back({
        .cell = ladderCell,
        .position = {
            static_cast<float>(ladderCell.x),
            static_cast<float>(ladderCell.y),
        },
        .color = { 1.0f, 1.0f, 1.0f, preview ? 0.62f : 1.0f },
        .baseElevation = static_cast<float>(ladderCell.z),
        .height = 1.0f,
        .showGrid = false,
        .isEditorPreview = preview,
        .affectsCameraFit = false,
        .interactionOutline = true,
        .model = manifest.modelForTile(TileType::Ladder),
        .modelRotationQuarterTurns = quarterTurns,
    });
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
    const std::function<bool(GridPosition3)>& visibleCell,
    GridPosition origin)
{
    for (std::size_t index = 0; index < decorations.size(); ++index) {
        const Level::Decoration& decoration = decorations[index];
        const GridPosition3 cell {
            static_cast<int>(std::floor(decoration.position.x)) + origin.x,
            static_cast<int>(std::floor(decoration.position.y)) + origin.y,
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
            Vec3 lightPosition = decorationLightPosition(decoration);
            lightPosition.x += static_cast<float>(origin.x);
            lightPosition.y += static_cast<float>(origin.y);
            frame.lighting.pointLights[frame.lighting.pointLightCount++] = {
                .position = lightPosition,
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
        auto tile = decorationVisual(
            decorations[index],
            manifest,
            false,
            editorDecorations ? std::optional<std::size_t>(index) : std::nullopt,
            highlight);
        tile.cell.x += origin.x;
        tile.cell.y += origin.y;
        tile.position.x += static_cast<float>(origin.x);
        tile.position.y += static_cast<float>(origin.y);
        tile.modelTransform->translation.x += static_cast<float>(origin.x);
        tile.modelTransform->translation.y += static_cast<float>(origin.y);
        frame.tiles.push_back(tile);
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
        .interactionOutline = true,
        .model = model,
        .modelTransform = RenderFrameData::ModelTransform {
            .translation = translation,
            .scale = { flagScale, flagScale, flagScale },
            .pivot = { 0.0f, 0.0f, 0.0f },
        },
    });
}

} // namespace sokoban::renderFrameParts
