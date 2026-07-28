#include "engine/TileThumbnailBake.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/RenderFrameBuilder.hpp"
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
constexpr float cropPadding = 0.35f;

} // namespace

std::string assetPathFor(TileType tile)
{
    return "custom/thumbnails/tile_" + slug(tileTypeName(tile)) + ".png";
}

bool shouldBake(TileType tile)
{
    // Air is the eraser and has nothing to draw; Water is excluded from the
    // palette entirely because it is a layer property rather than a brush.
    return tile != TileType::Air && tile != TileType::Water;
}

RenderFrameData buildBakeFrame(
    TileType tile,
    const AssetManifest& manifest,
    const PresentationSettings& settings)
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = bedSize;
    frame.levelHeight = bedSize;
    frame.levelDepth = 2;
    frame.cameraExtent = { 0, 0, 0, bedSize, bedSize, 2 };
    frame.cameraDistanceMultiplier = cameraDistanceMultiplier;
    // The game's lighting, not the frame defaults - those have shadows and
    // ambient occlusion off, which is most of what the bed is there to show.
    frame.lighting = settings.renderLighting();
    // A palette icon shows the asset, not the editor's guides.
    frame.gridOverlay = {};

    // The bed. Flat, neutral, and drawn as plain cubes rather than Ground, so
    // a screen's splat map cannot change what the thumbnails look like.
    for (uint32_t y = 0; y < bedSize; ++y) {
        for (uint32_t x = 0; x < bedSize; ++x) {
            frame.tiles.push_back({
                .cell = {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    0,
                },
                .position = {
                    static_cast<float>(x),
                    static_cast<float>(y),
                },
                .size = { 1.0f, 1.0f },
                .color = bedColor,
                .height = 0.0f,
                .showGrid = false,
                // Every bed cell counts toward the fit, which is what makes
                // the camera identical for every tile.
                .affectsCameraFit = true,
            });
        }
    }

    // Ground is the one tile whose look comes from the splat shader rather
    // than a model, so it replaces the bed cell instead of standing on it -
    // otherwise it would z-fight with the grey underneath.
    const bool replacesBedCell = tile == TileType::Ground;
    if (replacesBedCell) {
        frame.tiles.erase(
            frame.tiles.begin() +
            static_cast<std::ptrdiff_t>(bedCentre * bedSize + bedCentre));
    }

    // Built by the same function the editor draws with, rather than restating
    // its rules here. Re-deriving them is exactly how the conveyors ended up
    // flat and all facing the same way.
    RenderFrameData::Tile subject = tileVisual(
        tile,
        {
            static_cast<int>(bedCentre),
            static_cast<int>(bedCentre),
            replacesBedCell ? 0 : 1,
        },
        manifest,
        settings);
    // Sits directly on the bed's surface. The bed is flat (height 0), so
    // ground level is 0 whichever cell the subject claims; contact shadows and
    // ambient occlusion then read the way they do on a real board.
    subject.baseElevation = 0.0f;
    // A palette icon shows the asset, not the editor's guides, and every tile
    // must count toward the fit so the camera is identical for all of them.
    subject.showGrid = false;
    subject.affectsCameraFit = true;
    frame.tiles.push_back(subject);

    if (tile == TileType::Ground) {
        frame.groundSplat = {
            .base = manifest.findTextureIdByName(groundSplatBaseTextureName),
            .detail = manifest.findTextureIdByName(groundSplatDetailTextureName),
            .splatMap = manifest.findTextureIdByName(groundSplatMapTextureName),
        };
    }
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

    const auto centre = static_cast<float>(bedCentre);
    float minX = static_cast<float>(width);
    float minY = static_cast<float>(height);
    float maxX = 0.0f;
    float maxY = 0.0f;
    // The subject occupies the centre cell and may stand a unit tall.
    for (const float x : { centre, centre + 1.0f }) {
        for (const float y : { centre, centre + 1.0f }) {
            for (const float z : { 0.0f, 1.0f }) {
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
