#include "engine/TileThumbnailBake.hpp"

#include "engine/AnimationCatalog.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/GateEffect.hpp"
#include "engine/GroundTileGeometry.hpp"
#include "engine/MinecartGateVisuals.hpp"
#include "engine/ParticleConfig.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/RenderFrameParts.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/MirrorConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sokoban::tileThumbnails {
namespace {

// "Conveyor Up" -> "conveyor_up", so the files sort readably and are safe on
// every filesystem.
[[nodiscard]] std::string slug(std::string_view name)
{
    std::string result;
    result.reserve(name.size());
    for (const char character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            result.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        } else if (!result.empty() && result.back() != '_') {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result;
}

// How much of the subject cell's projected size to leave as margin, so a tile
// is not cropped flush against its own silhouette.
constexpr float cropPadding = 0.28f;

} // namespace

std::string assetPathFor(TileType tile)
{
    // Retain the existing thumbnail for the original south-facing stand.
    if (tile == TileType::LecternSouth) {
        return "custom/thumbnails/tile_lectern.png";
    }
    // The original button is the north placement; keep its existing icon path.
    if (tile == TileType::ButtonNorth) {
        return "custom/thumbnails/tile_button.png";
    }
    return "custom/thumbnails/tile_" + slug(tileTypeName(tile)) + ".png";
}

bool shouldBake(TileType tile)
{
    return tile != TileType::Air;
}

RenderFrameData buildBakeFrame(
    TileType tile,
    const AssetManifest& manifest,
    const PresentationSettings& settings,
    const AnimationCatalog* animations)
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.skyCloudsEnabled = false;
    frame.backgroundColor = backdropColor;
    frame.levelWidth = studioSize;
    frame.levelHeight = studioSize;
    frame.levelDepth = 2;
    frame.cameraExtent = { 0, 0, 0, studioSize, studioSize, 2 };
    frame.cameraDistanceMultiplier = cameraDistanceMultiplier;
    frame.lighting = {
        .sun = { .direction = normalize(Vec3 { -0.55f, -0.65f, 0.9f }),
            .color = { 1.0f, 1.0f, 1.0f }, .intensity = 1.0f },
        .ambient = { .color = { 1.0f, 1.0f, 1.0f }, .intensity = 0.66f },
        .specularStrength = 0.48f,
    };
    frame.outputTransform.bloomEnabled = false;

    const bool ground = tileTypeIsGround(tile);
    RenderFrameData::Tile subject = tileVisual(
        tile,
        {
            static_cast<int>(subjectCell),
            static_cast<int>(subjectCell),
            ground ? 0 : 1,
        },
        manifest,
        settings);
    subject.baseElevation = 0.0f;
    subject.showGrid = false;
    subject.affectsCameraFit = true;
    if (tile == TileType::Ladder) {
        // Centre the narrow wall-mounted section in the crop.
        subject.position.y += 0.38f;
    }
    if (animations != nullptr &&
        (tileTypeIsPlayerStart(tile) || tile == TileType::Enemy)) {
        const AnimationUse use = tile == TileType::Enemy
            ? AnimationUse::ThumbnailEnemyIdle
            : AnimationUse::ThumbnailPlayerIdle;
        subject.animation = animations->animation(use);
        subject.animationTimeSeconds = 0.0f;
    }
    if (tileTypeIsWater(tile)) {
        frame.cameraPitchDegrees = 0.0f;
        frame.cameraYawDegrees = 0.0f;
        frame.waterGridBounds = { 0, 0, studioSize, studioSize };
        frame.waterRendering.reflectionStrength = 0.0f;
        frame.waterSurfaces.push_back({
            .cell = subject.cell,
            .position = subject.position,
            .color = frame.waterRendering.surfaceColor,
            .elevation = 0.025f,
            .streamDirection = renderFrameParts::waterStreamDirectionForTile(tile),
        });
    } else if (tileTypeIsGate(tile)) {
        // The energy volume and particles are the gate's visible geometry.
        appendGateEffect(frame,
            Level::Gate { .cell = {
                static_cast<int>(subjectCell), static_cast<int>(subjectCell), 0 },
                .enemyGate = tile == TileType::EnemyGate },
            manifest, 1.0f, 0.0f);
    } else if (tile == TileType::MinecartGate) {
        appendMinecartGateVisual(frame,
            { static_cast<int>(subjectCell), static_cast<int>(subjectCell), 0 },
            TileType::RailStraightNorthSouth);
    } else if (tileTypeIsPortal(tile)) {
        // A diagonal view keeps every cardinal doorway visible rather than
        // presenting east/west apertures exactly edge-on.
        frame.cameraYawDegrees = 45.0f;
        frame.cameraPitchDegrees = 55.0f;
        renderFrameParts::appendPortalVisual(
            frame,
            subject,
            tile,
            0.0f,
            manifest.findTextureIdByName(config::turretGlowTextureName));
    } else {
        if (const std::optional<CharacterType> character =
                wardrobeCharacterForTile(tile)) {
            renderFrameParts::appendWardrobeMarker(
                frame,
                subject.cell,
                *character,
                manifest,
                animations != nullptr
                    ? animations->animation(AnimationUse::ThumbnailPlayerIdle)
                    : manifest.playerIdleAnimation(),
                0.0f);
            frame.tiles.back().baseElevation = 1.15f;
        }
        frame.tiles.push_back(subject);
    }

    if (tileTypeHasSplatTop(tile)) {
        frame.groundRockSideTexture = manifest.findTextureIdByName(groundRockSideTextureName);
        frame.groundSplat = groundSplatTexturesForScreen(
            [&manifest](std::string_view name) { return manifest.findTextureIdByName(name); },
            std::nullopt);
    }
    if (ground) processGroundTileGeometry({ frame.tiles.data(), frame.tiles.size() }, manifest);
    return frame;
}

CropRect cropFor(
    const RenderFrameData& frame, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return { .x = 0, .y = 0, .width = 1, .height = 1 };
    }

    // Project through the same camera the frame will be rendered with, so the
    // crop tracks the subject rather than assuming where it lands.
    PreparedRenderScene scene;
    const IsoScenePreparer preparer;
    preparer.prepare(
        frame,
        { static_cast<float>(width), static_cast<float>(height) },
        scene);

    const auto centre = static_cast<float>(subjectCell);
    float minX = static_cast<float>(width);
    float minY = static_cast<float>(height);
    float maxX = 0.0f;
    float maxY = 0.0f;
    Vec3 subjectMinimum { centre, centre, 0.0f };
    Vec3 subjectMaximum { centre + 1.0f, centre + 1.0f, 1.0f };
    const bool waterThumbnail = !frame.waterSurfaces.empty();
    if (waterThumbnail) {
        const auto& water = frame.waterSurfaces.front();
        subjectMinimum = { water.position.x, water.position.y, water.elevation };
        subjectMaximum = {
            water.position.x + water.size.x,
            water.position.y + water.size.y,
            water.elevation,
        };
    }
    // Rocky bottoms can extend beyond the normal subject cell.
    for (const auto& renderable : scene.renderables) {
        if (renderable.kind != PreparedRenderable::Kind::Tile ||
            renderable.sourceIndex >= frame.tiles.size()) continue;
        const auto& tile = frame.tiles[renderable.sourceIndex];
        if (!tile.groundModule || !tile.groundModuleBottom ||
            tile.cell.x != static_cast<int>(subjectCell) ||
            tile.cell.y != static_cast<int>(subjectCell)) continue;
        const auto& bounds = renderable.worldBounds;
        if (!bounds.valid()) continue;
        subjectMinimum = {
            std::min(subjectMinimum.x, bounds.minimum.x),
            std::min(subjectMinimum.y, bounds.minimum.y),
            std::min(subjectMinimum.z, bounds.minimum.z),
        };
        subjectMaximum = {
            std::max(subjectMaximum.x, bounds.maximum.x),
            std::max(subjectMaximum.y, bounds.maximum.y),
            std::max(subjectMaximum.z, bounds.maximum.z),
        };
    }
    // Include both the taller doorway and its horizontal plume, so no
    // cardinal orientation clips the soft glow at the icon's edge.
    for (const auto& face : frame.isoFaces) {
        if (face.effect != RenderSurfaceEffect::PortalEnergy &&
            face.effect != RenderSurfaceEffect::PortalBeam) continue;
        for (const Vec3 point : face.vertices) {
            subjectMinimum = {
                std::min(subjectMinimum.x, point.x),
                std::min(subjectMinimum.y, point.y),
                std::min(subjectMinimum.z, point.z),
            };
            subjectMaximum = {
                std::max(subjectMaximum.x, point.x),
                std::max(subjectMaximum.y, point.y),
                std::max(subjectMaximum.z, point.z),
            };
        }
    }
    for (const float x : { subjectMinimum.x, subjectMaximum.x }) {
        for (const float y : { subjectMinimum.y, subjectMaximum.y }) {
            for (const float z : { subjectMinimum.z, subjectMaximum.z }) {
                const Vec3 clip = IsoScenePreparer::projectIsoPoint(
                    scene.isoLayout, scene.renderExtent, { x, y, z });
                const float pixelX =
                    (clip.x + 1.0f) * 0.5f * static_cast<float>(width);
                const float pixelY =
                    (1.0f - clip.y) * 0.5f * static_cast<float>(height);
                minX = std::min(minX, pixelX);
                minY = std::min(minY, pixelY);
                maxX = std::max(maxX, pixelX);
                maxY = std::max(maxY, pixelY);
            }
        }
    }

    if (waterThumbnail) {
        // Only capture whole pixels inside the projected surface, so the
        // icon contains water even at the antialiased edges of the crop.
        const auto left = static_cast<int32_t>(std::ceil(minX));
        const auto top = static_cast<int32_t>(std::ceil(minY));
        const auto insideWidth = static_cast<int32_t>(std::floor(maxX)) - left;
        const auto insideHeight = static_cast<int32_t>(std::floor(maxY)) - top;
        const auto side = std::min(insideWidth, insideHeight);
        if (side > 0) {
            return {
                .x = left + (insideWidth - side) / 2,
                .y = top + (insideHeight - side) / 2,
                .width = static_cast<uint32_t>(side),
                .height = static_cast<uint32_t>(side),
            };
        }
    }

    // Square, so the saved picture is not stretched by the window's aspect.
    const float centreX = (minX + maxX) * 0.5f;
    const float centreY = (minY + maxY) * 0.5f;
    const float span =
        std::max(maxX - minX, maxY - minY) * (1.0f + cropPadding);
    const float limit =
        static_cast<float>(std::min(width, height));
    const float side = std::clamp(span, 1.0f, limit);

    const float left = std::clamp(
        centreX - side * 0.5f, 0.0f, static_cast<float>(width) - side);
    const float top = std::clamp(
        centreY - side * 0.5f, 0.0f, static_cast<float>(height) - side);

    return {
        .x = static_cast<int32_t>(std::lround(left)),
        .y = static_cast<int32_t>(std::lround(top)),
        .width = static_cast<uint32_t>(std::lround(side)),
        .height = static_cast<uint32_t>(std::lround(side)),
    };
}

} // namespace sokoban::tileThumbnails
