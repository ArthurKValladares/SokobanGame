#include "engine/AnimationPreviewScene.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/PresentationSettings.hpp"

#include <stdexcept>

namespace sokoban::animationPreviewScene {

RenderFrameData build(
    RenderModel model,
    const AssetManifest& manifest,
    const PresentationSettings& settings)
{
    if (model.isCube() ||
        manifest.model(model).geometry != ModelGeometry::Skinned) {
        throw std::invalid_argument(
            "animation preview requires a skinned manifest model");
    }

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = bedSize;
    frame.levelHeight = bedSize;
    frame.levelDepth = 2;
    frame.cameraExtent = { 0, 0, 0, bedSize, bedSize, 2 };
    frame.cameraDistanceMultiplier = cameraDistanceMultiplier;
    frame.lighting = settings.renderLighting();
    frame.gridOverlay = settings.renderGridOverlay();
    frame.tiles.reserve(bedSize * bedSize + 1);

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
                .showGrid = true,
                .affectsCameraFit = true,
            });
        }
    }

    frame.tiles.push_back({
        .cell = {
            static_cast<int>(bedCenter),
            static_cast<int>(bedCenter),
            1,
        },
        .position = {
            static_cast<float>(bedCenter),
            static_cast<float>(bedCenter),
        },
        .size = { 1.0f, 1.0f },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        .baseElevation = 0.0f,
        .height = 1.0f,
        .showGrid = false,
        .affectsCameraFit = true,
        .model = model,
        .animationInstanceId = animationInstanceId,
    });
    return frame;
}

} // namespace sokoban::animationPreviewScene
