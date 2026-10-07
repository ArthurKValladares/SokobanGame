#include "TestHarness.hpp"

#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/PointShadowFaceCache.hpp"
#include "engine/render/GroundRimSurface.hpp"
#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"
#include "engine/TaskSystem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}

sokoban::Vec4 transformPoint(
    const std::array<sokoban::Vec4, 4>& columns,
    sokoban::Vec3 point)
{
    return {
        columns[0].x * point.x + columns[1].x * point.y +
            columns[2].x * point.z + columns[3].x,
        columns[0].y * point.x + columns[1].y * point.y +
            columns[2].y * point.z + columns[3].y,
        columns[0].z * point.x + columns[1].z * point.y +
            columns[2].z * point.z + columns[3].z,
        columns[0].w * point.x + columns[1].w * point.y +
            columns[2].w * point.z + columns[3].w,
    };
}

void checkNear(sokoban::Vec4 left, sokoban::Vec4 right)
{
    CHECK(near(left.x, right.x));
    CHECK(near(left.y, right.y));
    CHECK(near(left.z, right.z));
    CHECK(near(left.w, right.w));
}

sokoban::PreparedRenderScene prepareScene(
    const sokoban::RenderFrameData& frame,
    sokoban::Vec2 renderExtent)
{
    sokoban::PreparedRenderScene scene;
    sokoban::IsoScenePreparer {}.prepare(
        frame, renderExtent, scene);
    return scene;
}

sokoban::RenderFrameData::Tile cube(
    int x,
    int y,
    bool blurBehind = false)
{
    return {
        .cell = { x, y, 0 },
        .position = {
            static_cast<float>(x),
            static_cast<float>(y),
        },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        .height = 1.0f,
        .blurBehind = blurBehind,
    };
}

sokoban::RenderFrameData sceneFrame()
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = 4;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    frame.tiles = {
        cube(0, 0),
        cube(1, 0, true),
        cube(2, 0),
        cube(3, 0),
        cube(0, 1),
    };
    frame.tiles[2].model = { 1 };
    frame.tiles[3].pickOnly = true;
    frame.tiles[4].isEditorPreview = true;
    frame.waterSurfaces.push_back({
        .cell = { 1, 2, 0 },
        .position = { 1.0f, 2.0f },
        .color = { 0.05f, 0.38f, 0.72f, 0.64f },
        .elevation = 0.82f,
        .shorelineMask =
            sokoban::waterShorelineBit(
                sokoban::WaterShorelineEdge::NegativeY) |
            sokoban::waterShorelineBit(
                sokoban::WaterShorelineEdge::PositiveX),
    });
    frame.isoFaces.push_back({
        .vertices = {
            sokoban::Vec3 { 0.0f, 2.0f, 0.0f },
            sokoban::Vec3 { 1.0f, 2.0f, 0.0f },
            sokoban::Vec3 { 1.0f, 3.0f, 0.0f },
            sokoban::Vec3 { 0.0f, 3.0f, 0.0f },
        },
        .normal = { 0.0f, 0.0f, 1.0f },
        .color = { 0.5f, 0.5f, 0.5f, 1.0f },
    });
    return frame;
}

void checkPreparationOutputsMatch(
    const sokoban::PreparedRenderScene& expected,
    const sokoban::PreparedRenderScene& actual)
{
    CHECK(expected.opaqueFaceIndices == actual.opaqueFaceIndices);
    CHECK(expected.translucentFaceIndices == actual.translucentFaceIndices);
    CHECK(expected.pickFaceIndices == actual.pickFaceIndices);
    CHECK(expected.opaqueModelIndices == actual.opaqueModelIndices);
    CHECK(expected.translucentModelIndices == actual.translucentModelIndices);
    CHECK(expected.shadowFaces == actual.shadowFaces);
    CHECK(expected.shadowFaceBounds == actual.shadowFaceBounds);
    CHECK(expected.shadowModelIndices == actual.shadowModelIndices);
    CHECK(expected.groundChunks == actual.groundChunks);
    CHECK(expected.groundChunkTileMask == actual.groundChunkTileMask);
    CHECK(expected.groundChunkDraws.size() == actual.groundChunkDraws.size());
    for (std::size_t index = 0; index < expected.groundChunkDraws.size(); ++index) {
        const auto& left = expected.groundChunkDraws[index];
        const auto& right = actual.groundChunkDraws[index];
        CHECK(left.chunkIndex == right.chunkIndex);
        CHECK(left.tileIndices == right.tileIndices);
        CHECK(left.tileCount == right.tileCount);
        CHECK(left.mainSceneVisible == right.mainSceneVisible);
        CHECK(left.pickableTiles == right.pickableTiles);
    }
    CHECK(expected.pointShadowFaceCandidates ==
          actual.pointShadowFaceCandidates);
    CHECK(expected.pointShadowFacesInRange == actual.pointShadowFacesInRange);
    CHECK(expected.pointShadowFacesCulled == actual.pointShadowFacesCulled);
    for (std::size_t lightIndex = 0;
         lightIndex < sokoban::RenderFrameData::pointLightCapacity;
         ++lightIndex) {
        CHECK(expected.pointShadowCasters[lightIndex].faceIndices ==
              actual.pointShadowCasters[lightIndex].faceIndices);
        CHECK(expected.pointShadowCasters[lightIndex].modelTileIndices ==
              actual.pointShadowCasters[lightIndex].modelTileIndices);
    }
    CHECK(expected.opaqueBlendedFirst == actual.opaqueBlendedFirst);
    CHECK(expected.hasTranslucentContent == actual.hasTranslucentContent);
    CHECK(expected.reusedGroundRimSurfaces == actual.reusedGroundRimSurfaces);
    CHECK(expected.generatedGroundRimSurfaces == actual.generatedGroundRimSurfaces);
    CHECK(expected.groundRimSurfaceCacheHits == actual.groundRimSurfaceCacheHits);
    CHECK(expected.groundRimSurfaceCacheRebuilds == actual.groundRimSurfaceCacheRebuilds);
    CHECK(expected.groundRimSurfaceCacheBytes == actual.groundRimSurfaceCacheBytes);
    CHECK(expected.isoFaces.size() == actual.isoFaces.size());
    CHECK(expected.renderables.size() == actual.renderables.size());
    CHECK(expected.particles.size() == actual.particles.size());
    for (std::size_t index = 0; index < expected.isoFaces.size(); ++index) {
        CHECK(expected.isoFaces[index].worldVertices ==
              actual.isoFaces[index].worldVertices);
        CHECK(expected.isoFaces[index].vertices ==
              actual.isoFaces[index].vertices);
        CHECK(expected.isoFaces[index].depth == actual.isoFaces[index].depth);
        CHECK(expected.isoFaces[index].groundRimSurface ==
              actual.isoFaces[index].groundRimSurface);
        CHECK(expected.isoFaces[index].groundRimWallCoverage ==
              actual.isoFaces[index].groundRimWallCoverage);
        CHECK(expected.isoFaces[index].material == actual.isoFaces[index].material);
        CHECK(expected.isoFaces[index].groundSplat == actual.isoFaces[index].groundSplat);
        CHECK(expected.isoFaces[index].groundSplatOrigin == actual.isoFaces[index].groundSplatOrigin);
        CHECK(expected.isoFaces[index].color == actual.isoFaces[index].color);
    }
    for (std::size_t index = 0; index < expected.renderables.size(); ++index) {
        CHECK(expected.renderables[index].identity ==
              actual.renderables[index].identity);
        CHECK(expected.renderables[index].boundsRevision ==
              actual.renderables[index].boundsRevision);
        CHECK(expected.renderables[index].boundsReused ==
              actual.renderables[index].boundsReused);
        CHECK(expected.renderables[index].mainSceneVisible ==
              actual.renderables[index].mainSceneVisible);
    }
    for (std::size_t index = 0; index < expected.particles.size(); ++index) {
        CHECK(expected.particles[index].vertices ==
              actual.particles[index].vertices);
        CHECK(expected.particles[index].color == actual.particles[index].color);
        CHECK(expected.particles[index].emissiveStrength ==
              actual.particles[index].emissiveStrength);
        CHECK(expected.particles[index].textureNineSlice ==
              actual.particles[index].textureNineSlice);
        CHECK(expected.particles[index].texture ==
              actual.particles[index].texture);
        CHECK(expected.particles[index].depth == actual.particles[index].depth);
        CHECK(expected.particles[index].flipTextureV ==
              actual.particles[index].flipTextureV);
        CHECK(expected.particles[index].drawOnTop ==
              actual.particles[index].drawOnTop);
        CHECK(expected.particles[index].drawOrder ==
              actual.particles[index].drawOrder);
    }
}

void testParallelAuxiliaryPreparationMatchesSerialOutput()
{
    using namespace sokoban;

    RenderFrameData frame = sceneFrame();
    frame.tiles[2].groundTop = true;
    frame.tiles[2].groundRimSides = groundNorthSide | groundWestSide;
    frame.tiles[2].groundRimWidth = 0.12f;
    frame.tiles[2].groundRimDepth = 0.10f;
    frame.particles = {
        {
            .position = { 0.5f, 0.5f, 0.7f },
            .size = { 0.4f, 0.6f },
            .rotationRadians = 0.35f,
            .color = { 1.0f, 0.5f, 0.2f, 0.8f },
            .texture = { 1 },
        },
        {
            .position = { 2.0f, 1.0f, 1.2f },
            .size = { 0.8f, 0.3f },
            .rotationRadians = -0.2f,
            .color = { 0.2f, 0.7f, 1.0f, 1.0f },
            .texture = { 2 },
            .drawOnTop = true,
        },
    };

    IsoScenePreparer serialPreparer;
    IsoScenePreparer parallelPreparer;
    TaskSystem preparationTasks(1);
    PreparedRenderScene serialScene;
    PreparedRenderScene parallelScene;
    const Vec2 extent { 1920.0f, 1080.0f };

    serialPreparer.prepare(frame, extent, serialScene);
    parallelPreparer.prepare(
        frame, extent, parallelScene, &preparationTasks);
    const auto firstTaskDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);
    while (preparationTasks.executedTaskCount() != 1 &&
        std::chrono::steady_clock::now() < firstTaskDeadline) {
        std::this_thread::yield();
    }
    CHECK(preparationTasks.executedTaskCount() == 1);
    CHECK(std::ranges::any_of(serialScene.isoFaces, [](const auto& face) {
        return face.groundRimSurface;
    }));
    CHECK(std::ranges::any_of(serialScene.isoFaces, [](const auto& face) {
        return face.groundRimSurface &&
            std::ranges::any_of(face.groundRimWallCoverage, [](float coverage) {
                return coverage == 1.0f;
            });
    }));
    CHECK(serialScene.generatedGroundRimSurfaces == 1);
    CHECK(serialScene.reusedGroundRimSurfaces == 0);
    checkPreparationOutputsMatch(serialScene, parallelScene);

    // A second frame also exercises retained-bound reuse on the foreground
    // path while the auxiliary vectors are rebuilt by the worker.
    serialPreparer.prepare(frame, extent, serialScene);
    parallelPreparer.prepare(
        frame, extent, parallelScene, &preparationTasks);
    const auto secondTaskDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);
    while (preparationTasks.executedTaskCount() != 2 &&
        std::chrono::steady_clock::now() < secondTaskDeadline) {
        std::this_thread::yield();
    }
    CHECK(preparationTasks.executedTaskCount() == 2);
    CHECK(serialScene.reusedRenderableBounds ==
          parallelScene.reusedRenderableBounds);
    CHECK(serialScene.generatedGroundRimSurfaces == 0);
    CHECK(serialScene.reusedGroundRimSurfaces == 1);
    checkPreparationOutputsMatch(serialScene, parallelScene);

    frame.tiles[2].groundRimWidth = 0.16f;
    frame.tiles[2].color = { 0.2f, 0.7f, 0.3f, 1.0f };
    frame.cameraYawDegrees = 27.0f;
    serialPreparer.prepare(frame, extent, serialScene);
    parallelPreparer.prepare(frame, extent, parallelScene, &preparationTasks);
    CHECK(serialScene.generatedGroundRimSurfaces == 1);
    CHECK(serialScene.reusedGroundRimSurfaces == 0);
    checkPreparationOutputsMatch(serialScene, parallelScene);
}

void testPointShadowCastersAreRangeCulledConservatively()
{
    using namespace sokoban;

    RenderFrameData frame = sceneFrame();
    frame.lighting.pointLightCount = 3;
    frame.lighting.pointLights[0] = {
        .position = { 0.5f, 0.5f, 0.5f },
        .intensity = 2.0f,
        .range = 0.75f,
        .emitterTileIndex = 2,
    };
    frame.lighting.pointLights[1] = {
        .position = { 100.0f, 100.0f, 100.0f },
        .intensity = 1.0f,
        .range = 1.0f,
    };
    frame.lighting.pointLights[2] = {
        .position = { 0.5f, 0.5f, 0.5f },
        .intensity = 1.0f,
        .range = 10.0f,
        .castsShadows = false,
    };

    const PreparedRenderScene scene =
        prepareScene(frame, { 1920.0f, 1080.0f });
    CHECK(scene.shadowFaces.size() == 11);
    CHECK(scene.shadowFaceBounds.size() == scene.shadowFaces.size());
    CHECK(scene.pointShadowFaceCandidates == 22);
    CHECK(scene.pointShadowFacesInRange == 9);
    CHECK(scene.pointShadowFacesCulled == 13);
    CHECK(scene.pointShadowCasters[0].faceIndices.size() == 9);
    CHECK(scene.pointShadowCasters[0].modelTileIndices.empty());
    CHECK(scene.pointShadowCasters[1].faceIndices.empty());
    CHECK(scene.pointShadowCasters[1].modelTileIndices.size() == 1);
    CHECK(scene.pointShadowCasters[1].modelTileIndices[0] == 2);
    CHECK(scene.pointShadowCasters[2].faceIndices.empty());
    CHECK(scene.pointShadowCasters[2].modelTileIndices.empty());

    // Stable source order is part of the rendering contract: filtering may
    // remove indices but must never reorder the survivors.
    CHECK(std::ranges::is_sorted(
        scene.pointShadowCasters[0].faceIndices));

    IsoScenePreparer legacyPreparer;
    legacyPreparer.setPointShadowRangeCulling(false);
    PreparedRenderScene legacy;
    legacyPreparer.prepare(frame, { 1920.0f, 1080.0f }, legacy);
    CHECK(legacy.pointShadowFaceCandidates == 22);
    CHECK(legacy.pointShadowFacesInRange == 22);
    CHECK(legacy.pointShadowFacesCulled == 0);
}

void testPointShadowFaceCacheRequiresExactStableGeometry()
{
    using namespace sokoban;

    PointShadowFaceCache cache;
    RenderFrameData::PointLight light {
        .position = { 1.0f, 2.0f, 3.0f },
        .intensity = 1.0f,
        .range = 5.0f,
    };
    std::vector<std::array<Vec3, 4>> faces {
        {
            Vec3 { 0.0f, 0.0f, 0.0f },
            Vec3 { 1.0f, 0.0f, 0.0f },
            Vec3 { 1.0f, 1.0f, 0.0f },
            Vec3 { 0.0f, 1.0f, 0.0f },
        },
        {
            Vec3 { 2.0f, 0.0f, 0.0f },
            Vec3 { 3.0f, 0.0f, 0.0f },
            Vec3 { 3.0f, 1.0f, 0.0f },
            Vec3 { 2.0f, 1.0f, 0.0f },
        },
    };
    const std::vector<std::size_t> indices { 0, 1 };
    const std::span<const PointShadowModelState> noModels;

    CHECK(!cache.reusable(0, light, faces, indices, noModels));
    cache.markRendered(0, light, faces, indices, noModels);
    CHECK(cache.reusable(0, light, faces, indices, noModels));
    CHECK(!cache.reusable(1, light, faces, indices, noModels));

    RenderFrameData::PointLight moved = light;
    moved.position.x += 0.001f;
    CHECK(!cache.reusable(0, moved, faces, indices, noModels));
    RenderFrameData::PointLight reranged = light;
    reranged.range += 0.001f;
    CHECK(!cache.reusable(0, reranged, faces, indices, noModels));

    faces[1][0].z += 0.001f;
    CHECK(!cache.reusable(0, light, faces, indices, noModels));
    faces[1][0].z -= 0.001f;
    const std::vector<std::size_t> reordered { 1, 0 };
    CHECK(!cache.reusable(0, light, faces, reordered, noModels));

    std::vector<PointShadowModelState> models {
        {
            .tileIndex = 3,
            .tile = cube(3, 2),
            .ready = true,
        },
    };
    CHECK(!cache.reusable(0, light, faces, indices, models));
    cache.markRendered(0, light, faces, indices, models);
    CHECK(cache.reusable(0, light, faces, indices, models));
    models[0].tile.animationTimeSeconds = 0.25f;
    CHECK(!cache.reusable(0, light, faces, indices, models));
    models[0].tile.animationTimeSeconds = 0.0f;
    models[0].tile.groundSideMask = 3;
    CHECK_MESSAGE(!cache.reusable(0, light, faces, indices, models),
        "a changed ground index selection invalidates cached point shadows");
    models[0].tile.groundSideMask = groundAllSides;
    CHECK(cache.reusable(0, light, faces, indices, models));
    models[0].ready = false;
    CHECK(!cache.reusable(0, light, faces, indices, models));

    // Profile changes affect the shared rock vertices even when the selected
    // index range and the body's transform remain identical. The exact model
    // key must invalidate all six point-shadow faces for each such change.
    models[0].ready = true;
    models[0].tile.model = { 1 };
    models[0].tile.groundTop = true;
    models[0].tile.groundRimWidth = 0.12f;
    models[0].tile.groundRimDepth = 0.10f;
    models[0].tile.groundRimSides = groundNorthSide | groundEastSide;
    cache.markRendered(0, light, faces, indices, models);
    CHECK(cache.reusable(0, light, faces, indices, models));
    const RenderFrameData::Tile rimBaseline = models[0].tile;
    const auto changedRim = [&](auto mutate, const char* reason) {
        mutate(models[0].tile);
        CHECK_MESSAGE(!cache.reusable(0, light, faces, indices, models), reason);
        models[0].tile = rimBaseline;
        CHECK(cache.reusable(0, light, faces, indices, models));
    };
    changedRim([](auto& tile) { tile.groundRimWidth += 0.01f; },
        "rim width changes invalidate cached body shadows");
    changedRim([](auto& tile) { tile.groundRimDepth += 0.01f; },
        "rim depth changes invalidate cached body shadows");
    changedRim([](auto& tile) { tile.groundRimSides = groundNorthSide; },
        "rim exposure changes invalidate cached body shadows");
    changedRim([](auto& tile) { tile.groundRimConcaveCorners = groundSouthWestCorner; },
        "concave rim patches invalidate cached body shadows");
    changedRim([](auto& tile) { tile.groundRimWidth = 0.0f; },
        "disabling the rim invalidates its deformed body shadows");
    models[0].tile.groundRimWidth = 0.0f;
    cache.markRendered(0, light, faces, indices, models);
    CHECK(cache.reusable(0, light, faces, indices, models));
    models[0].tile = rimBaseline;
    CHECK_MESSAGE(!cache.reusable(0, light, faces, indices, models),
        "enabling the rim invalidates cached flat body shadows");
    cache.markRendered(0, light, faces, indices, models);
    models[0].ready = false;
    CHECK_MESSAGE(!cache.reusable(0, light, faces, indices, models),
        "ground model eviction invalidates the active rim shadow cohort");
    models[0].ready = true;
    CHECK(cache.reusable(0, light, faces, indices, models));

    cache.markRendered(0, light, faces, indices, noModels);
    cache.invalidate(0);
    CHECK(!cache.reusable(0, light, faces, indices, noModels));
}

void testCameraLayoutUsesConfiguredAngles()
{
    using namespace sokoban;

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 4;
    frame.levelHeight = 2;
    frame.levelDepth = 1;

    const PreparedRenderScene scene =
        prepareScene(frame, { 1920.0f, 1080.0f });
    constexpr float radiansPerDegree =
        3.14159265358979323846f / 180.0f;
    const float pitch = config::cameraPitchDegrees * radiansPerDegree;
    const float yaw = config::cameraYawDegrees * radiansPerDegree;
    const float distance = 4.0f * config::cameraDistanceScale;
    const float horizontalDistance = std::sin(pitch) * distance;

    CHECK(near(
        scene.isoLayout.cameraPosition.x,
        2.0f + std::sin(yaw) * horizontalDistance));
    CHECK(near(
        scene.isoLayout.cameraPosition.y,
        1.0f + std::cos(yaw) * horizontalDistance));
    CHECK(near(
        scene.isoLayout.cameraPosition.z,
        std::cos(pitch) * distance));
    CHECK(near(
        scene.isoLayout.focalLength,
        1.0f / std::tan(
            config::cameraVerticalFovDegrees *
            radiansPerDegree * 0.5f)));

    frame.cameraPitchDegrees = 0.0f;
    const PreparedRenderScene overhead =
        prepareScene(frame, { 1920.0f, 1080.0f });
    CHECK(near(overhead.isoLayout.cameraPosition.x, 2.0f));
    CHECK(near(overhead.isoLayout.cameraPosition.y, 1.0f));
    CHECK(near(overhead.isoLayout.cameraPosition.z, distance));
    CHECK(near(overhead.isoLayout.cameraRight.x, std::cos(yaw)));
    CHECK(near(overhead.isoLayout.cameraRight.y, -std::sin(yaw)));
    CHECK(near(overhead.isoLayout.cameraUp.x, -std::sin(yaw)));
    CHECK(near(overhead.isoLayout.cameraUp.y, -std::cos(yaw)));

    // Each viewpoint must rotate both the perspective pose and the overhead
    // basis. This also covers the straight-down singularity at a custom yaw.
    for (float authoredYaw : { -180.0f, -90.0f, 45.0f, 180.0f }) {
        frame.cameraYawDegrees = authoredYaw;
        const float yawRadians = authoredYaw * radiansPerDegree;
        for (float authoredPitch : { 0.0f, 55.0f, 89.0f }) {
            frame.cameraPitchDegrees = authoredPitch;
            const auto rotated = prepareScene(frame, { 1920.0f, 1080.0f });
            const auto& layout = rotated.isoLayout;
            const float pitchRadians = authoredPitch * radiansPerDegree;
            CHECK(near(layout.cameraPosition.x, 2.0f + std::sin(yawRadians) * std::sin(pitchRadians) * distance));
            CHECK(near(layout.cameraPosition.y, 1.0f + std::cos(yawRadians) * std::sin(pitchRadians) * distance));
            CHECK(near(layout.cameraPosition.z, std::cos(pitchRadians) * distance));
            CHECK(near(layout.cameraRight.x, std::cos(yawRadians)));
            CHECK(near(layout.cameraRight.y, -std::sin(yawRadians)));
            CHECK(near(dot(layout.cameraRight, layout.cameraForward), 0.0f));
            CHECK(near(dot(layout.cameraUp, layout.cameraForward), 0.0f));
        }
    }
}

void testExplicitCameraPoseBypassesBoardFit()
{
    using namespace sokoban;

    RenderFrameData frame = sceneFrame();
    frame.cameraExtent = RenderFrameData::CameraExtent {
        .originX = -20,
        .originY = -20,
        .originZ = -5,
        .width = 40,
        .height = 40,
        .depth = 10,
    };
    frame.cameraOverride = RenderFrameData::CameraOverride {
        .position = { 1.5f, -4.0f, 2.25f },
        .forward = { 0.2f, 1.0f, -0.25f },
        .verticalFovDegrees = 75.0f,
    };

    const PreparedRenderScene scene =
        prepareScene(frame, { 1600.0f, 900.0f });
    const Vec3 expectedForward = normalize(frame.cameraOverride->forward);
    CHECK(scene.isoLayout.cameraPosition == frame.cameraOverride->position);
    CHECK(near(scene.isoLayout.cameraForward.x, expectedForward.x));
    CHECK(near(scene.isoLayout.cameraForward.y, expectedForward.y));
    CHECK(near(scene.isoLayout.cameraForward.z, expectedForward.z));
    CHECK(near(dot(
        scene.isoLayout.cameraForward,
        scene.isoLayout.cameraRight), 0.0f));
    CHECK(near(dot(
        scene.isoLayout.cameraForward,
        scene.isoLayout.cameraUp), 0.0f));
    CHECK(near(scene.isoLayout.projectedCenter.x, 0.0f));
    CHECK(near(scene.isoLayout.projectedCenter.y, 0.0f));
    CHECK(near(scene.isoLayout.fitScale, 1.0f));
    CHECK(near(scene.isoLayout.nearestDepth, 0.05f));
    CHECK(scene.isoLayout.farthestDepth > scene.isoLayout.nearestDepth);
    CHECK(near(
        scene.isoLayout.focalLength,
        1.0f / std::tan(degreesToRadians(75.0f) * 0.5f)));

    const Vec3 pointOnAim =
        frame.cameraOverride->position + expectedForward * 5.0f;
    const Vec3 projected = IsoScenePreparer::projectIsoPoint(
        scene.isoLayout,
        scene.renderExtent,
        pointOnAim);
    CHECK(near(projected.x, 0.0f));
    CHECK(near(projected.y, 0.0f));
}

void testDetachedCameraRetainsFloorMarkerDepthSeparation()
{
    using namespace sokoban;

    // D16 collapses these distinct surfaces with the detached camera's 0.05
    // near plane. Exercise the actual GPU projection, including world-space
    // translation, at both board-view and distant free-camera positions.
    for (float distance : { 10.0f, 30.0f, 60.0f }) {
        RenderFrameData frame = sceneFrame();
        frame.cameraOverride = RenderFrameData::CameraOverride {
            .position = { 1.5f, 1.5f, distance },
            .forward = { 0.0f, 0.0f, -1.0f },
            .verticalFovDegrees = 75.0f,
        };
        const PreparedRenderScene scene =
            prepareScene(frame, { 1600.0f, 900.0f });
        const Mat4 clipFromWorld =
            isoClipFromWorld(scene.isoLayout, scene.renderExtent);
        const Vec4 floorClip = transform(
            clipFromWorld, Vec4 { 1.5f, 1.5f, 0.0f, 1.0f });
        const Vec4 markerClip = transform(
            clipFromWorld, Vec4 { 1.5f, 1.5f, 0.01f, 1.0f });
        const float floorDepth = floorClip.z / floorClip.w;
        const float markerDepth = markerClip.z / markerClip.w;
        CHECK(markerDepth > 0.0f);
        CHECK(floorDepth < 1.0f);
        // D32 stores these floats directly; require more than one storage
        // step so rounding at a shared triangle edge cannot erase the gap.
        CHECK(std::nextafter(markerDepth, 1.0f) < floorDepth);
        CHECK(std::round(markerDepth * 65535.0f) ==
              std::round(floorDepth * 65535.0f));
    }
}

bool containsCell(
    const sokoban::PreparedRenderScene& scene,
    sokoban::GridPosition3 cell)
{
    return std::ranges::any_of(
        scene.pickFaceIndices,
        [&](std::size_t index) {
            return scene.isoFaces[index].cell == cell;
        });
}

void testPreparationCategorizesOneSharedFacePool()
{
    const sokoban::RenderFrameData frame = sceneFrame();
    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 1920.0f, 1080.0f });

    CHECK(scene.hasTranslucentContent);
    CHECK(!scene.opaqueFaceIndices.empty());
    CHECK(!scene.translucentFaceIndices.empty());
    CHECK(scene.opaqueModelIndices.size() == 1);
    CHECK(scene.opaqueModelIndices[0] == 2);
    CHECK(scene.translucentModelIndices.empty());
    CHECK(scene.shadowModelIndices.size() == 1);
    CHECK(scene.shadowModelIndices[0] == 2);
    CHECK(scene.shadowFaces.size() == 11);

    std::unordered_set<std::size_t> drawFaces;
    for (std::size_t index : scene.opaqueFaceIndices) {
        CHECK(index < scene.isoFaces.size());
        CHECK(!scene.isoFaces[index].blurBehind);
        CHECK(drawFaces.insert(index).second);
    }
    for (std::size_t index : scene.translucentFaceIndices) {
        CHECK(index < scene.isoFaces.size());
        CHECK(
            scene.isoFaces[index].blurBehind ||
            scene.isoFaces[index].material ==
                sokoban::PreparedSurfaceMaterial::Water);
        CHECK(drawFaces.insert(index).second);
    }
    const auto waterFace = std::ranges::find_if(
        scene.isoFaces,
        [](const sokoban::PreparedIsoFace& face) {
            return face.material ==
                sokoban::PreparedSurfaceMaterial::Water;
        });
    CHECK(waterFace != scene.isoFaces.end());
    if (waterFace != scene.isoFaces.end()) {
        CHECK(waterFace->worldOrigin.x == 1.0f);
        CHECK(waterFace->worldOrigin.y == 2.0f);
        CHECK(waterFace->gridSize.x == 1.0f);
        CHECK(waterFace->gridSize.y == 1.0f);
        CHECK(
            waterFace->shorelineMask ==
            (sokoban::waterShorelineBit(
                 sokoban::WaterShorelineEdge::NegativeY) |
                sokoban::waterShorelineBit(
                    sokoban::WaterShorelineEdge::PositiveX)));
    }

    CHECK(containsCell(scene, { 0, 0, 0 }));
    CHECK(containsCell(scene, { 1, 0, 0 }));
    CHECK(containsCell(scene, { 2, 0, 0 }));
    CHECK(containsCell(scene, { 3, 0, 0 }));
    CHECK(!containsCell(scene, { 0, 1, 0 }));
    CHECK(containsCell(scene, { 1, 2, 0 }));
}

void testPassListsAreDepthSorted()
{
    const sokoban::PreparedRenderScene scene =
        prepareScene(
            sceneFrame(), { 1280.0f, 720.0f });
    // The two lists sort in opposite directions, and each direction is load
    // bearing, so they are pinned separately rather than by one shared
    // predicate.
    //
    // Opaque draws nearest first so the depth test can reject occluded
    // fragments before they are shaded. Translucent draws farthest first
    // because alpha blending is order dependent; reversing it is a visible
    // correctness bug, not a performance regression.
    auto nearestFirst = [&](const std::vector<std::size_t>& indices) {
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (scene.isoFaces[indices[i - 1]].depth >
                scene.isoFaces[indices[i]].depth) {
                return false;
            }
        }
        return true;
    };
    auto farthestFirst = [&](const std::vector<std::size_t>& indices) {
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (scene.isoFaces[indices[i - 1]].depth <
                scene.isoFaces[indices[i]].depth) {
                return false;
            }
        }
        return true;
    };
    CHECK(nearestFirst(scene.opaqueFaceIndices));
    CHECK(farthestFirst(scene.translucentFaceIndices));
    // Every face in this scene is fully opaque, so the opaque list is one
    // nearest-first run with no blended tail. The predicate above only
    // covers the whole list while that stays true.
    CHECK(scene.opaqueBlendedFirst == scene.opaqueFaceIndices.size());
    // A list of one or zero satisfies both predicates, which would make the
    // checks above vacuous. This scene must exercise a real ordering.
    CHECK(scene.opaqueFaceIndices.size() > 1);
    CHECK(scene.translucentFaceIndices.size() > 1);
}

void testOpaqueListEndsWithABackToFrontBlendedTail()
{
    using namespace sokoban;

    // A face can sit in the opaque list and still need the blend unit; the
    // editor's ladder-rung preview is the shipped case. Front-to-back is
    // wrong for those, because they write depth: drawn early, such a face
    // occludes the geometry it was supposed to blend with and the result is
    // a hole rather than a tint. They are partitioned into a farthest-first
    // tail behind everything fully opaque, which is the order the whole list
    // used to be in.
    RenderFrameData frame = sceneFrame();
    RenderFrameData::Tile nearFaded = cube(0, 2);
    nearFaded.color.w = 0.4f;
    RenderFrameData::Tile farFaded = cube(3, 2);
    farFaded.color.w = 0.4f;
    frame.tiles.push_back(nearFaded);
    frame.tiles.push_back(farFaded);

    const PreparedRenderScene scene =
        prepareScene(frame, { 1280.0f, 720.0f });

    // Both halves must be non-empty or the split is not being exercised,
    // and the tail needs more than one face for its order to mean anything.
    CHECK(scene.opaqueBlendedFirst > 0);
    CHECK(scene.opaqueBlendedFirst < scene.opaqueFaceIndices.size());
    CHECK(scene.opaqueFaceIndices.size() - scene.opaqueBlendedFirst > 1);

    // The boundary is exactly the alpha test the recorder picks its pipeline
    // with, so the tail is also one uninterrupted run of blended draws.
    for (std::size_t i = 0; i < scene.opaqueFaceIndices.size(); ++i) {
        const float alpha =
            scene.isoFaces[scene.opaqueFaceIndices[i]].color.w;
        CHECK((i >= scene.opaqueBlendedFirst) == (alpha < 1.0f));
    }
    for (std::size_t i = 1; i < scene.opaqueBlendedFirst; ++i) {
        CHECK(
            scene.isoFaces[scene.opaqueFaceIndices[i - 1]].depth <=
            scene.isoFaces[scene.opaqueFaceIndices[i]].depth);
    }
    for (std::size_t i = scene.opaqueBlendedFirst + 1;
         i < scene.opaqueFaceIndices.size();
         ++i) {
        CHECK(
            scene.isoFaces[scene.opaqueFaceIndices[i - 1]].depth >=
            scene.isoFaces[scene.opaqueFaceIndices[i]].depth);
    }

    // With the developer toggle off the list returns to a single painter's
    // order run and the recorder is told there is no LESS prefix to apply.
    PreparedRenderScene legacy;
    IsoScenePreparer legacyPreparer;
    legacyPreparer.setOpaqueFrontToBackSort(false);
    legacyPreparer.prepare(frame, { 1280.0f, 720.0f }, legacy);
    CHECK(legacy.opaqueBlendedFirst == legacy.opaqueFaceIndices.size());
    CHECK(legacy.opaqueFaceIndices.size() > 1);
    for (std::size_t i = 1; i < legacy.opaqueFaceIndices.size(); ++i) {
        CHECK(
            legacy.isoFaces[legacy.opaqueFaceIndices[i - 1]].depth >=
            legacy.isoFaces[legacy.opaqueFaceIndices[i]].depth);
    }
}

void testDepthRangeCoversTilesOutsideTheAuthoredCameraFit()
{
    using namespace sokoban;

    // projectIsoPoint clamps z to [0, 1] rather than clipping, so a surface
    // past the far plane does not vanish - it lands on exactly 1.0 together
    // with every other such surface, and the depth test can no longer order
    // them against each other. Front-to-back then shows the farthest of them,
    // back-to-front the nearest, which is why flipping the opaque sort turned
    // the far row of a board inside out.
    //
    // An authored cameraExtent is what makes that reachable. It excludes
    // tiles from the *fit* on purpose, so a decoration cannot zoom the board
    // out - but the depth range must not inherit that exclusion, because
    // those tiles are still drawn. Camera yaw is zero and the camera sits on
    // +Y looking toward -Y, so the low-y rows here are the far ones: the top
    // of the screen.
    constexpr uint32_t rows = 6;
    constexpr uint32_t columns = 4;
    constexpr int firstFramedRow = 3;
    const auto sceneryRows = [&](RenderFrameData& frame) {
        for (uint32_t y = 0; y < rows; ++y) {
            for (uint32_t x = 0; x < columns; ++x) {
                RenderFrameData::Tile tile =
                    cube(static_cast<int>(x), static_cast<int>(y));
                tile.affectsCameraFit =
                    static_cast<int>(y) >= firstFramedRow;
                frame.tiles.push_back(tile);
            }
        }
    };

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = columns;
    frame.levelHeight = rows;
    frame.levelDepth = 1;
    frame.cameraExtent = RenderFrameData::CameraExtent {
        .originX = 0,
        .originY = firstFramedRow,
        .originZ = 0,
        .width = columns,
        .height = rows - firstFramedRow,
        .depth = 1,
    };
    sceneryRows(frame);

    const PreparedRenderScene scene =
        prepareScene(frame, { 1280.0f, 720.0f });

    CHECK(!scene.opaqueFaceIndices.empty());
    bool reachesPastTheAuthoredExtent = false;
    for (std::size_t index : scene.opaqueFaceIndices) {
        const PreparedIsoFace& face = scene.isoFaces[index];
        for (Vec3 vertex : face.vertices) {
            // Strict on both ends: a vertex sitting exactly on either plane
            // is a clamped vertex, and clamped vertices are what collapse
            // distinct surfaces onto one depth.
            CHECK(vertex.z < 1.0f);
            CHECK(vertex.z > 0.0f);
        }
        if (face.cell.y < firstFramedRow) {
            reachesPastTheAuthoredExtent = true;
        }
    }
    // Without a drawn face outside the authored extent the checks above are
    // vacuous, and that is precisely the case the clamp used to collapse.
    CHECK(reachesPastTheAuthoredExtent);

    // Covering those tiles for depth must not let them frame the camera.
    // The fit has to stay identical to a board that has only the framed
    // rows in it, while the depth range grows to reach the scenery.
    RenderFrameData framedOnly = frame;
    framedOnly.tiles.clear();
    for (uint32_t y = firstFramedRow; y < rows; ++y) {
        for (uint32_t x = 0; x < columns; ++x) {
            framedOnly.tiles.push_back(
                cube(static_cast<int>(x), static_cast<int>(y)));
        }
    }
    const PreparedRenderScene framed =
        prepareScene(framedOnly, { 1280.0f, 720.0f });
    CHECK(near(scene.isoLayout.fitScale, framed.isoLayout.fitScale));
    CHECK(near(
        scene.isoLayout.projectedCenter.x,
        framed.isoLayout.projectedCenter.x));
    CHECK(near(
        scene.isoLayout.projectedCenter.y,
        framed.isoLayout.projectedCenter.y));
    CHECK(scene.isoLayout.farthestDepth > framed.isoLayout.farthestDepth);
}

void testCameraMatrixReproducesTheScalarProjection()
{
    using namespace sokoban;

    // C1's premise: the GPU gets a camera. That is only safe if the matrix
    // handed to it is the transform the CPU has been applying all along, so
    // this walks a grid of points through both and requires them to agree.
    //
    // Points are placed on the camera basis rather than in world coordinates,
    // which is what guarantees they sit in front of the near plane. That is
    // the whole domain where the two forms are defined to agree:
    // projectIsoPointToClip clamps view-space z to a small positive value and
    // a matrix cannot, so behind-the-camera points are deliberately excluded.
    // Nothing drawn is ever back there.
    const std::array<Vec2, 3> extents {
        Vec2 { 1280.0f, 720.0f },
        Vec2 { 2560.0f, 1080.0f },
        Vec2 { 800.0f, 1200.0f },
    };
    const std::array<uint32_t, 3> widths { 4, 9, 2 };
    const std::array<uint32_t, 3> heights { 3, 2, 11 };

    std::size_t comparisons = 0;
    for (std::size_t variant = 0; variant < extents.size(); ++variant) {
        RenderFrameData frame = sceneFrame();
        frame.levelWidth = widths[variant];
        frame.levelHeight = heights[variant];
        frame.cameraPitchDegrees =
            20.0f + 20.0f * static_cast<float>(variant);

        const PreparedRenderScene scene =
            prepareScene(frame, extents[variant]);
        const IsoRenderLayout& layout = scene.isoLayout;
        const Mat4 clipFromWorld =
            isoClipFromWorld(layout, extents[variant]);

        const float nearDepth = std::max(layout.nearestDepth, 0.001f);
        const float farDepth =
            std::max(layout.farthestDepth, nearDepth + 0.001f);
        const std::array<float, 4> depths {
            nearDepth,
            nearDepth + (farDepth - nearDepth) * 0.25f,
            nearDepth + (farDepth - nearDepth) * 0.75f,
            farDepth,
        };
        for (float depth : depths) {
            for (float across : { -4.0f, 0.0f, 2.5f }) {
                for (float upward : { -3.0f, 0.0f, 1.5f }) {
                    const Vec3 point = layout.cameraPosition +
                        layout.cameraForward * depth +
                        layout.cameraRight * across +
                        layout.cameraUp * upward;

                    const Vec3 scalar = IsoScenePreparer::projectIsoPoint(
                        layout, extents[variant], point);
                    const Vec4 clip = transform(
                        clipFromWorld,
                        Vec4 { point.x, point.y, point.z, 1.0f });
                    CHECK(clip.w > 0.0f);
                    const Vec3 viaMatrix {
                        clip.x / clip.w,
                        clip.y / clip.w,
                        std::clamp(clip.z / clip.w, 0.0f, 1.0f),
                    };

                    CHECK(std::abs(scalar.x - viaMatrix.x) < 0.0005f);
                    CHECK(std::abs(scalar.y - viaMatrix.y) < 0.0005f);
                    CHECK(std::abs(scalar.z - viaMatrix.z) < 0.0005f);
                    ++comparisons;
                }
            }
        }

        // The depth row is the ordinary Vulkan one, and the planes are where
        // the layout says they are. A matrix that projected correctly in x
        // and y but mapped depth differently would pass everything above and
        // still ruin every depth test.
        const Vec4 onNear = transform(
            clipFromWorld,
            toVec4(
                layout.cameraPosition + layout.cameraForward * nearDepth,
                1.0f));
        const Vec4 onFar = transform(
            clipFromWorld,
            toVec4(
                layout.cameraPosition + layout.cameraForward * farDepth,
                1.0f));
        CHECK(near(onNear.z / onNear.w, 0.0f));
        CHECK(near(onFar.z / onFar.w, 1.0f));
    }
    CHECK(comparisons == 108);
}

void testShadowMatrixReproducesTheScalarProjection()
{
    using namespace sokoban;

    // Same contract as the camera matrix, for the sun. The one asymmetry is
    // the depth clamp: projectShadowPoint clamps z into [0, 1] and the matrix
    // does not, so the comparison clamps the matrix result the way the
    // shaders are required to.
    const std::array<Vec2, 2> extents {
        Vec2 { 1280.0f, 720.0f },
        Vec2 { 1920.0f, 1200.0f },
    };
    std::size_t comparisons = 0;
    for (Vec2 extent : extents) {
        const PreparedRenderScene scene = prepareScene(sceneFrame(), extent);
        const ShadowRenderLayout& layout = scene.shadowLayout;
        const Mat4 matrix = shadowClipFromWorld(layout);

        for (float x : { -2.0f, 0.5f, 3.0f, 9.0f }) {
            for (float y : { -1.5f, 0.0f, 4.0f }) {
                for (float z : { -1.0f, 0.0f, 2.5f }) {
                    const Vec3 point { x, y, z };
                    const Vec4 scalar =
                        IsoScenePreparer::projectShadowPoint(layout, point);
                    Vec4 viaMatrix =
                        transform(matrix, Vec4 { x, y, z, 1.0f });
                    viaMatrix.z = std::clamp(viaMatrix.z, 0.0f, 1.0f);

                    CHECK(std::abs(scalar.x - viaMatrix.x) < 0.0005f);
                    CHECK(std::abs(scalar.y - viaMatrix.y) < 0.0005f);
                    CHECK(std::abs(scalar.z - viaMatrix.z) < 0.0005f);
                    CHECK(near(viaMatrix.w, 1.0f));
                    ++comparisons;
                }
            }
        }
    }
    CHECK(comparisons == 72);
}

void testModelWorldTransformComposesToTheClipTransform()
{
    using namespace sokoban;

    // Models ship worldFromModel to the GPU and the vertex shader applies the
    // camera. That is only equivalent to the old baked clipFromModel if the
    // projection is affine in homogeneous coordinates, which it is - so this
    // pins the composition rather than trusting the argument.
    const Vec2 extent { 1280.0f, 720.0f };
    RenderFrameData frame = sceneFrame();
    const PreparedRenderScene scene = prepareScene(frame, extent);
    const Mat4 clipFromWorld = isoClipFromWorld(scene.isoLayout, extent);

    std::size_t tilesChecked = 0;
    for (const RenderFrameData::Tile& tile : frame.tiles) {
        const std::array<Vec4, 4> world =
            IsoScenePreparer::modelWorldTransform(tile);
        const std::array<Vec4, 4> clip = IsoScenePreparer::modelClipTransform(
            scene.isoLayout, extent, tile);
        for (std::size_t column = 0; column < 4; ++column) {
            const Vec4 composed = transform(clipFromWorld, world[column]);
            CHECK(std::abs(composed.x - clip[column].x) < 0.002f);
            CHECK(std::abs(composed.y - clip[column].y) < 0.002f);
            CHECK(std::abs(composed.z - clip[column].z) < 0.002f);
            CHECK(std::abs(composed.w - clip[column].w) < 0.002f);
        }
        ++tilesChecked;
    }
    CHECK(tilesChecked > 1);

    // The world form has to actually be a world transform: its last column is
    // the model's origin, and its axis columns are directions, not points.
    const std::array<Vec4, 4> firstTile =
        IsoScenePreparer::modelWorldTransform(frame.tiles.front());
    CHECK(near(firstTile[0].w, 0.0f));
    CHECK(near(firstTile[1].w, 0.0f));
    CHECK(near(firstTile[2].w, 0.0f));
    CHECK(near(firstTile[3].w, 1.0f));
}

void testPickingConsumesPreparedFaces()
{
    const sokoban::RenderFrameData frame = sceneFrame();
    const sokoban::Vec2 extent { 1600.0f, 900.0f };
    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, extent);

    const auto iterator = std::ranges::find_if(
        scene.pickFaceIndices,
        [&](std::size_t index) {
            const sokoban::PreparedIsoFace& face = scene.isoFaces[index];
            return face.cell == sokoban::GridPosition3 { 3, 0, 0 } &&
                face.normal.z > 0.5f;
        });
    CHECK(iterator != scene.pickFaceIndices.end());
    if (iterator == scene.pickFaceIndices.end()) {
        return;
    }

    const sokoban::PreparedIsoFace& face = scene.isoFaces[*iterator];
    sokoban::Vec2 center {};
    for (sokoban::Vec3 vertex : face.vertices) {
        center.x += (vertex.x + 1.0f) * 0.5f * extent.x;
        center.y += (1.0f - vertex.y) * 0.5f * extent.y;
    }
    center.x *= 0.25f;
    center.y *= 0.25f;

    const std::optional<sokoban::GridPosition3> picked =
        sokoban::IsoScenePreparer {}.pickGridCell(
            scene,
            center,
            extent,
            frame.levelWidth,
            frame.levelHeight);
    CHECK(picked.has_value());
    CHECK((picked == sokoban::GridPosition3 { 3, 0, 0 }));
}

void testModelBackedPickFacesUseLogicalBounds()
{
    using namespace sokoban;

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 2;
    frame.tiles.push_back(cube(1, 1));

    RenderFrameData::Tile flag = cube(1, 1);
    flag.cell = { 1, 1, 1 };
    flag.position = { 1.175f, 1.175f };
    flag.size = { 0.65f, 0.65f };
    flag.baseElevation = 1.0f;
    flag.height = 0.975f;
    flag.model = RenderModel { 1 };
    flag.modelTransform = RenderFrameData::ModelTransform {
        .translation = { 1.5f, 1.5f, 1.0f },
        .scale = { 0.65f, 0.65f, 0.65f },
        .pivot = { 0.0f, 0.0f, 0.0f },
    };
    frame.tiles.push_back(flag);

    constexpr Vec2 extent { 1600.0f, 900.0f };
    const PreparedRenderScene scene = prepareScene(frame, extent);
    const auto top = std::ranges::find_if(
        scene.pickFaceIndices,
        [&](std::size_t index) {
            const PreparedIsoFace& face = scene.isoFaces[index];
            return face.cell == flag.cell && face.normal.z > 0.5f;
        });
    CHECK(top != scene.pickFaceIndices.end());
    if (top == scene.pickFaceIndices.end()) {
        return;
    }

    const PreparedIsoFace& face = scene.isoFaces[*top];
    CHECK(near(face.worldOrigin.x, flag.position.x));
    CHECK(near(face.worldOrigin.y, flag.position.y));
    CHECK(near(face.worldHeight, flag.baseElevation + flag.height));
    CHECK(near(face.gridSize.x, flag.size.x));
    CHECK(near(face.gridSize.y, flag.size.y));

    Vec2 center {};
    for (Vec3 vertex : face.vertices) {
        center.x += (vertex.x + 1.0f) * 0.5f * extent.x;
        center.y += (1.0f - vertex.y) * 0.5f * extent.y;
    }
    center.x *= 0.25f;
    center.y *= 0.25f;
    CHECK((IsoScenePreparer {}.pickGridCell(
        scene,
        center,
        extent,
        frame.levelWidth,
        frame.levelHeight) == flag.cell));
}

void testPickingTracksAuthoredCameraAngles()
{
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 2;
    frame.levelDepth = 1;
    frame.tiles.push_back(cube(1, 0));
    const Vec2 extent { 1600.0f, 900.0f };
    for (float yaw : { -180.0f, -90.0f, 45.0f, 180.0f }) {
        frame.cameraYawDegrees = yaw;
        for (float pitch : { 0.0f, 55.0f }) {
            frame.cameraPitchDegrees = pitch;
            const auto scene = prepareScene(frame, extent);
            const Vec3 projected = IsoScenePreparer::projectIsoPoint(
                scene.isoLayout, extent, { 1.5f, 0.5f, 1.0f });
            const Vec2 pixel {
                (projected.x + 1.0f) * 0.5f * extent.x,
                (1.0f - projected.y) * 0.5f * extent.y,
            };
            CHECK((IsoScenePreparer {}.pickGridCell(
                scene, pixel, extent, frame.levelWidth, frame.levelHeight) ==
                GridPosition3 { 1, 0, 0 }));
        }
    }
}

void testPickingHonorsConfiguredGridBorder()
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = 2;
    frame.levelHeight = 2;
    frame.levelDepth = 1;
    sokoban::RenderFrameData::Tile borderCell = cube(-1, 0);
    borderCell.height = 0.0f;
    borderCell.pickOnly = true;
    borderCell.affectsCameraFit = false;
    frame.tiles.push_back(borderCell);

    const sokoban::Vec2 extent { 1600.0f, 900.0f };
    const sokoban::PreparedRenderScene scene = prepareScene(frame, extent);
    const auto iterator = std::ranges::find_if(
        scene.pickFaceIndices,
        [&](std::size_t index) {
            return scene.isoFaces[index].cell ==
                sokoban::GridPosition3 { -1, 0, 0 };
        });
    CHECK(iterator != scene.pickFaceIndices.end());
    if (iterator == scene.pickFaceIndices.end()) {
        return;
    }

    sokoban::Vec2 center {};
    for (sokoban::Vec3 vertex : scene.isoFaces[*iterator].vertices) {
        center.x += (vertex.x + 1.0f) * 0.5f * extent.x;
        center.y += (1.0f - vertex.y) * 0.5f * extent.y;
    }
    center.x *= 0.25f;
    center.y *= 0.25f;

    const sokoban::IsoScenePreparer preparer;
    CHECK(!preparer.pickGridCell(
        scene, center, extent, frame.levelWidth, frame.levelHeight));
    CHECK((preparer.pickGridCell(
        scene,
        center,
        extent,
        frame.levelWidth,
        frame.levelHeight,
        1) == sokoban::GridPosition3 { -1, 0, 0 }));
}

void testScaledBorderTileCannotLeakIntoBoardPicking()
{
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 2;
    frame.levelHeight = 2;
    frame.levelDepth = 1;
    auto borderCell = cube(2, 1);
    borderCell.position = { 1.9875f, 0.9875f };
    borderCell.size = { 1.025f, 1.025f };
    borderCell.height = 1.025f;
    frame.tiles.push_back(borderCell);

    constexpr Vec2 extent { 1600.0f, 900.0f };
    const PreparedRenderScene scene = prepareScene(frame, extent);
    const Vec3 clip = IsoScenePreparer::projectIsoPoint(
        scene.isoLayout, extent, { 2.5f, 1.5f, 1.025f });
    const Vec2 pixel {
        (clip.x + 1.0f) * 0.5f * extent.x,
        (1.0f - clip.y) * 0.5f * extent.y,
    };
    const IsoScenePreparer preparer;
    CHECK(!preparer.pickGridCell(
        scene, pixel, extent, frame.levelWidth, frame.levelHeight));
    CHECK((preparer.pickGridCell(
        scene, pixel, extent, frame.levelWidth, frame.levelHeight, 1) ==
        borderCell.cell));
}

void testVirtualPickPlaneMatchesPreviewTopUnderPerspective()
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = 8;
    frame.levelHeight = 8;
    frame.levelDepth = 1;

    sokoban::RenderFrameData::Tile pickPlane = cube(7, 7);
    pickPlane.baseElevation = 1.0f;
    pickPlane.height = 0.0f;
    pickPlane.pickOnly = true;
    sokoban::RenderFrameData::Tile preview = cube(7, 7);
    preview.isEditorPreview = true;
    frame.tiles = { pickPlane, preview };

    const sokoban::Vec2 extent { 1600.0f, 900.0f };
    const sokoban::PreparedRenderScene scene = prepareScene(frame, extent);
    const auto pickFace = std::ranges::find_if(
        scene.isoFaces,
        [](const sokoban::PreparedIsoFace& face) {
            return face.pickable && face.normal.z > 0.5f;
        });
    const auto previewTop = std::ranges::find_if(
        scene.isoFaces,
        [](const sokoban::PreparedIsoFace& face) {
            return face.isEditorPreview && face.normal.z > 0.5f;
        });
    CHECK(pickFace != scene.isoFaces.end());
    CHECK(previewTop != scene.isoFaces.end());
    if (pickFace == scene.isoFaces.end() ||
        previewTop == scene.isoFaces.end()) {
        return;
    }

    for (std::size_t i = 0; i < pickFace->vertices.size(); ++i) {
        CHECK(near(pickFace->vertices[i].x, previewTop->vertices[i].x));
        CHECK(near(pickFace->vertices[i].y, previewTop->vertices[i].y));
    }

    sokoban::Vec2 center {};
    for (sokoban::Vec3 vertex : pickFace->vertices) {
        center.x += (vertex.x + 1.0f) * 0.5f * extent.x;
        center.y += (1.0f - vertex.y) * 0.5f * extent.y;
    }
    center.x *= 0.25f;
    center.y *= 0.25f;
    CHECK((sokoban::IsoScenePreparer {}.pickGridCell(
        scene,
        center,
        extent,
        frame.levelWidth,
        frame.levelHeight) == sokoban::GridPosition3 { 7, 7, 0 }));
}

void testTopDownPreparationSkipsIsoWork()
{
    sokoban::RenderFrameData frame;
    frame.levelWidth = 2;
    frame.levelHeight = 2;
    frame.tiles.push_back(cube(0, 0, true));

    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 0.0f, 0.0f });
    CHECK(scene.renderExtent.x == 1.0f);
    CHECK(scene.renderExtent.y == 1.0f);
    CHECK(scene.tileLayout.tileSize.x > 0.0f);
    CHECK(scene.tileLayout.tileSize.y > 0.0f);
    CHECK(!scene.hasTranslucentContent);
    CHECK(scene.isoFaces.empty());
    CHECK(scene.opaqueFaceIndices.empty());
    CHECK(scene.translucentFaceIndices.empty());
    CHECK(scene.shadowFaces.size() == 5);
}

void testPreparationReusesOutputWithoutStaleLists()
{
    sokoban::IsoScenePreparer preparer;
    sokoban::PreparedRenderScene scene;
    preparer.prepare(
        sceneFrame(), { 1920.0f, 1080.0f }, scene);
    const std::size_t faceCapacity = scene.isoFaces.capacity();
    const std::size_t opaqueCapacity =
        scene.opaqueFaceIndices.capacity();
    CHECK(faceCapacity > 0);
    CHECK(opaqueCapacity > 0);

    sokoban::RenderFrameData topDown;
    topDown.levelWidth = 1;
    topDown.levelHeight = 1;
    topDown.tiles.push_back(cube(0, 0));
    preparer.prepare(topDown, { 800.0f, 600.0f }, scene);

    CHECK(scene.isoFaces.empty());
    CHECK(scene.opaqueFaceIndices.empty());
    CHECK(scene.translucentFaceIndices.empty());
    CHECK(scene.pickFaceIndices.empty());
    CHECK(scene.opaqueModelIndices.empty());
    CHECK(scene.translucentModelIndices.empty());
    CHECK(scene.shadowModelIndices.empty());
    CHECK(scene.shadowFaces.size() == 5);
    CHECK(scene.isoFaces.capacity() >= faceCapacity);
    CHECK(scene.opaqueFaceIndices.capacity() >= opaqueCapacity);
}

void testPersistentRenderablesReuseAndReviseStableBounds()
{
    using namespace sokoban;

    IsoScenePreparer preparer;
    RenderFrameData frame = sceneFrame();
    PreparedRenderScene scene;
    preparer.prepare(frame, { 1280.0f, 720.0f }, scene);

    CHECK(scene.renderables.size() ==
        frame.tiles.size() + frame.waterSurfaces.size() +
            frame.isoFaces.size());
    CHECK(scene.rebuiltRenderableBounds == scene.renderables.size());
    CHECK(scene.reusedRenderableBounds == 0);
    CHECK(scene.visibleRenderables + scene.culledRenderables ==
        scene.renderables.size());
    const PreparedRenderable firstTile = scene.renderables.front();
    CHECK(firstTile.kind == PreparedRenderable::Kind::Tile);
    CHECK(firstTile.sourceIndex == 0);
    CHECK(firstTile.identity != 0);
    CHECK(firstTile.boundsRevision == 1);
    CHECK(firstTile.worldBounds == aabbFromMinMax(
        Vec3 { 0.0f, 0.0f, 0.0f },
        Vec3 { 1.0f, 1.0f, 1.0f }));

    // Camera and material values are frame-local. They must not invalidate
    // retained world geometry or its identity.
    frame.cameraPitchDegrees = 35.0f;
    frame.tiles.front().color = { 0.2f, 0.4f, 0.6f, 1.0f };
    preparer.prepare(frame, { 1920.0f, 1080.0f }, scene);
    CHECK(scene.reusedRenderableBounds == scene.renderables.size());
    CHECK(scene.rebuiltRenderableBounds == 0);
    CHECK(scene.renderables.front().identity == firstTile.identity);
    CHECK(scene.renderables.front().boundsRevision ==
        firstTile.boundsRevision);

    // Moving the same semantic source keeps its identity but advances the
    // bounds revision. The earlier snapshot remains unchanged.
    frame.tiles.front().position.x = 0.5f;
    preparer.prepare(frame, { 1920.0f, 1080.0f }, scene);
    CHECK(scene.rebuiltRenderableBounds == 1);
    CHECK(scene.renderables.front().identity == firstTile.identity);
    CHECK(scene.renderables.front().boundsRevision == 2);
    CHECK(scene.renderables.front().worldBounds.minimum.x == 0.5f);
    CHECK(firstTile.worldBounds.minimum.x == 0.0f);

    // Replacing the source occupying a slot starts a new persistent identity.
    frame.tiles.front().cell = { 8, 8, 0 };
    frame.tiles.front().position = { 8.0f, 8.0f };
    preparer.prepare(frame, { 1920.0f, 1080.0f }, scene);
    CHECK(scene.renderables.front().identity != firstTile.identity);
    CHECK(scene.renderables.front().boundsRevision == 1);
}

void testFrustumCullingFiltersOnlyMainSceneDrawLists()
{
    using namespace sokoban;

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 4;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    frame.cameraExtent = RenderFrameData::CameraExtent {
        .width = 4,
        .height = 3,
        .depth = 1,
    };

    RenderFrameData::Tile visibleCube = cube(1, 1);
    RenderFrameData::Tile outsideCube = cube(100, 100);
    outsideCube.affectsCameraFit = false;
    RenderFrameData::Tile provisionalModel = cube(200, 200);
    provisionalModel.model = RenderModel { 1 };
    provisionalModel.affectsCameraFit = false;
    frame.tiles = { visibleCube, outsideCube, provisionalModel };

    frame.waterSurfaces = {
        RenderFrameData::WaterSurface {
            .cell = { 1, 2, 0 },
            .position = { 1.0f, 2.0f },
            .color = { 0.1f, 0.3f, 0.8f, 0.6f },
            .elevation = 0.5f,
        },
        RenderFrameData::WaterSurface {
            .cell = { 100, 100, 0 },
            .position = { 100.0f, 100.0f },
            .color = { 0.1f, 0.3f, 0.8f, 0.6f },
            .elevation = 0.5f,
        },
    };
    frame.isoFaces = {
        RenderFrameData::IsoFace {
            .vertices = {
                Vec3 { 0.0f, 2.0f, 0.0f },
                Vec3 { 1.0f, 2.0f, 0.0f },
                Vec3 { 1.0f, 3.0f, 0.0f },
                Vec3 { 0.0f, 3.0f, 0.0f },
            },
            .normal = { 0.0f, 0.0f, 1.0f },
            .color = { 0.5f, 0.5f, 0.5f, 1.0f },
        },
        RenderFrameData::IsoFace {
            .vertices = {
                Vec3 { 100.0f, 100.0f, 0.0f },
                Vec3 { 101.0f, 100.0f, 0.0f },
                Vec3 { 101.0f, 101.0f, 0.0f },
                Vec3 { 100.0f, 101.0f, 0.0f },
            },
            .normal = { 0.0f, 0.0f, 1.0f },
            .color = { 0.5f, 0.5f, 0.5f, 1.0f },
        },
    };

    const auto isOutsideFace = [](const PreparedIsoFace& face) {
        return std::ranges::any_of(
            face.worldVertices,
            [](Vec3 vertex) { return vertex.x > 50.0f; });
    };
    const auto drawsOutsideFace = [&](const PreparedRenderScene& scene) {
        return std::ranges::any_of(
                   scene.opaqueFaceIndices,
                   [&](std::size_t index) {
                       return isOutsideFace(scene.isoFaces[index]);
                   }) ||
            std::ranges::any_of(
                scene.translucentFaceIndices,
                [&](std::size_t index) {
                    return isOutsideFace(scene.isoFaces[index]);
                });
    };

    IsoScenePreparer preparer;
    PreparedRenderScene culled;
    preparer.prepare(frame, { 1280.0f, 720.0f }, culled);
    CHECK(culled.renderables.size() == 7);
    CHECK(culled.visibleRenderables == 4);
    CHECK(culled.culledRenderables == 3);
    CHECK(culled.renderables[0].mainSceneVisible);
    CHECK(!culled.renderables[1].mainSceneVisible);
    // Loaded mesh bounds are not retained yet, so model-backed tiles fail
    // open even when their provisional unit volume is outside the frustum.
    CHECK(culled.renderables[2].mainSceneVisible);
    CHECK(!drawsOutsideFace(culled));
    CHECK(culled.opaqueModelIndices.size() == 1);
    CHECK(culled.opaqueModelIndices.front() == 2);
    CHECK(containsCell(culled, { 100, 100, 0 }));

    const std::size_t pickFaceCount = culled.pickFaceIndices.size();
    const std::size_t shadowFaceCount = culled.shadowFaces.size();
    const std::size_t shadowModelCount = culled.shadowModelIndices.size();
    CHECK(shadowFaceCount == 12);
    CHECK(shadowModelCount == 1);

    preparer.setFrustumCulling(false);
    PreparedRenderScene unculled;
    preparer.prepare(frame, { 1280.0f, 720.0f }, unculled);
    CHECK(unculled.visibleRenderables == 7);
    CHECK(unculled.culledRenderables == 0);
    CHECK(drawsOutsideFace(unculled));
    CHECK(unculled.pickFaceIndices.size() == pickFaceCount);
    CHECK(unculled.shadowFaces.size() == shadowFaceCount);
    CHECK(unculled.shadowModelIndices.size() == shadowModelCount);
}

void testExteriorWaterDoesNotAffectCameraFitOrPicking()
{
    const sokoban::RenderFrameData baseFrame = sceneFrame();
    const sokoban::PreparedRenderScene baseScene =
        prepareScene(baseFrame, { 1920.0f, 1080.0f });

    sokoban::RenderFrameData exteriorFrame = baseFrame;
    exteriorFrame.waterSurfaces.push_back({
        .cell = { -64, -64, 0 },
        .position = { -64.0f, -64.0f },
        .size = { 63.0f, 131.0f },
        .color = { 0.05f, 0.38f, 0.72f, 0.64f },
        .elevation = 0.82f,
        .pickable = false,
    });
    const sokoban::PreparedRenderScene exteriorScene =
        prepareScene(exteriorFrame, { 1920.0f, 1080.0f });

    CHECK(exteriorScene.isoLayout.cameraPosition.x ==
        baseScene.isoLayout.cameraPosition.x);
    CHECK(exteriorScene.isoLayout.cameraPosition.y ==
        baseScene.isoLayout.cameraPosition.y);
    CHECK(exteriorScene.isoLayout.cameraPosition.z ==
        baseScene.isoLayout.cameraPosition.z);
    CHECK(exteriorScene.isoLayout.projectedCenter.x ==
        baseScene.isoLayout.projectedCenter.x);
    CHECK(exteriorScene.isoLayout.projectedCenter.y ==
        baseScene.isoLayout.projectedCenter.y);
    CHECK(exteriorScene.isoLayout.fitScale ==
        baseScene.isoLayout.fitScale);
    CHECK(exteriorScene.pickFaceIndices.size() ==
        baseScene.pickFaceIndices.size());
}

void testExteriorWaterReachesVisiblePlaneFootprint()
{
    sokoban::RenderFrameData frame = sceneFrame();
    constexpr sokoban::Vec4 waterColor {
        0.05f, 0.38f, 0.72f, 0.64f
    };
    constexpr float waterHeight = 0.82f;
    frame.waterSurfaces.push_back({
        .cell = { -2, -2, 0 },
        .position = { -2.0f, -2.0f },
        .size = { 1.0f, 7.0f },
        .color = waterColor,
        .elevation = waterHeight,
        .pickable = false,
    });
    frame.waterSurfaces.push_back({
        .cell = { 5, -2, 0 },
        .position = { 5.0f, -2.0f },
        .size = { 1.0f, 7.0f },
        .color = waterColor,
        .elevation = waterHeight,
        .pickable = false,
    });
    frame.waterSurfaces.push_back({
        .cell = { -1, -2, 0 },
        .position = { -1.0f, -2.0f },
        .size = { 6.0f, 1.0f },
        .color = waterColor,
        .elevation = waterHeight,
        .pickable = false,
    });
    frame.waterSurfaces.push_back({
        .cell = { -1, 4, 0 },
        .position = { -1.0f, 4.0f },
        .size = { 6.0f, 1.0f },
        .color = waterColor,
        .elevation = waterHeight,
        .pickable = false,
    });

    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 1998.0f, 1264.0f });
    float minimumX = std::numeric_limits<float>::max();
    float minimumY = std::numeric_limits<float>::max();
    float maximumX = std::numeric_limits<float>::lowest();
    float maximumY = std::numeric_limits<float>::lowest();
    for (const sokoban::PreparedIsoFace& face : scene.isoFaces) {
        if (face.material != sokoban::PreparedSurfaceMaterial::Water ||
            face.pickable ||
            (face.gridSize.x <= 1.0f && face.gridSize.y <= 1.0f)) {
            continue;
        }
        for (sokoban::Vec3 vertex : face.worldVertices) {
            minimumX = std::min(minimumX, vertex.x);
            minimumY = std::min(minimumY, vertex.y);
            maximumX = std::max(maximumX, vertex.x);
            maximumY = std::max(maximumY, vertex.y);
        }
    }

    CHECK(minimumX < -1.0f);
    CHECK(minimumY < -1.0f);
    CHECK(maximumX > 1.0f);
    CHECK(maximumY > 1.0f);

    // The four continuation strips must cover each exterior corner once.
    // Equal-height water cannot hide this overdraw with depth testing.
    std::vector<const sokoban::PreparedIsoFace*> strips;
    for (const auto& face : scene.isoFaces) {
        if (face.material == sokoban::PreparedSurfaceMaterial::Water &&
            !face.pickable) {
            strips.push_back(&face);
        }
    }
    CHECK(strips.size() == 4);
    for (std::size_t first = 0; first < strips.size(); ++first) {
        for (std::size_t second = first + 1; second < strips.size(); ++second) {
            const auto& a = *strips[first];
            const auto& b = *strips[second];
            const float overlapX = std::min(
                a.worldOrigin.x + a.gridSize.x,
                b.worldOrigin.x + b.gridSize.x) -
                std::max(a.worldOrigin.x, b.worldOrigin.x);
            const float overlapY = std::min(
                a.worldOrigin.y + a.gridSize.y,
                b.worldOrigin.y + b.gridSize.y) -
                std::max(a.worldOrigin.y, b.worldOrigin.y);
            CHECK(overlapX <= 0.0001f || overlapY <= 0.0001f);
        }
    }
    for (const float x : { minimumX + 0.1f, maximumX - 0.1f }) {
        for (const float y : { minimumY + 0.1f, maximumY - 0.1f }) {
            const auto coverage = std::ranges::count_if(strips,
                [&](const auto* face) {
                    return x > face->worldOrigin.x &&
                        x < face->worldOrigin.x + face->gridSize.x &&
                        y > face->worldOrigin.y &&
                        y < face->worldOrigin.y + face->gridSize.y;
                });
            CHECK(coverage == 1);
        }
    }
}

void testDecorativeTileDoesNotAffectCameraFit()
{
    const sokoban::RenderFrameData baseFrame = sceneFrame();
    const sokoban::PreparedRenderScene baseScene =
        prepareScene(baseFrame, { 1920.0f, 1080.0f });

    sokoban::RenderFrameData decorativeFrame = baseFrame;
    sokoban::RenderFrameData::Tile decoration = cube(1, 1);
    decoration.baseElevation = 12.0f;
    decoration.affectsCameraFit = false;
    decorativeFrame.tiles.push_back(decoration);
    const sokoban::PreparedRenderScene decorativeScene =
        prepareScene(decorativeFrame, { 1920.0f, 1080.0f });

    CHECK(decorativeScene.isoLayout.cameraPosition.x ==
        baseScene.isoLayout.cameraPosition.x);
    CHECK(decorativeScene.isoLayout.cameraPosition.y ==
        baseScene.isoLayout.cameraPosition.y);
    CHECK(decorativeScene.isoLayout.cameraPosition.z ==
        baseScene.isoLayout.cameraPosition.z);
    CHECK(decorativeScene.isoLayout.projectedCenter.x ==
        baseScene.isoLayout.projectedCenter.x);
    CHECK(decorativeScene.isoLayout.projectedCenter.y ==
        baseScene.isoLayout.projectedCenter.y);
    CHECK(decorativeScene.isoLayout.fitScale ==
        baseScene.isoLayout.fitScale);
    CHECK(decorativeScene.isoFaces.size() > baseScene.isoFaces.size());
}

void testExplicitCameraExtentOwnsEntireProjectedLayout()
{
    sokoban::RenderFrameData baseFrame = sceneFrame();
    baseFrame.cameraExtent = sokoban::RenderFrameData::CameraExtent {
        .originX = 0,
        .originY = 0,
        .originZ = 0,
        .width = 4,
        .height = 3,
        .depth = 1,
    };
    const sokoban::PreparedRenderScene baseScene =
        prepareScene(baseFrame, { 1920.0f, 1080.0f });

    sokoban::RenderFrameData transientFrame = baseFrame;
    transientFrame.tiles.front().position = { 50.0f, 30.0f };
    transientFrame.tiles.front().baseElevation = 12.0f;
    transientFrame.isoFaces.push_back({
        .vertices = {
            sokoban::Vec3 { 80.0f, 40.0f, 15.0f },
            sokoban::Vec3 { 81.0f, 40.0f, 15.0f },
            sokoban::Vec3 { 81.0f, 41.0f, 15.0f },
            sokoban::Vec3 { 80.0f, 41.0f, 15.0f },
        },
        .normal = { 0.0f, 0.0f, 1.0f },
    });
    const sokoban::PreparedRenderScene transientScene =
        prepareScene(transientFrame, { 1920.0f, 1080.0f });

    CHECK(near(transientScene.isoLayout.cameraPosition.x,
        baseScene.isoLayout.cameraPosition.x));
    CHECK(near(transientScene.isoLayout.cameraPosition.y,
        baseScene.isoLayout.cameraPosition.y));
    CHECK(near(transientScene.isoLayout.cameraPosition.z,
        baseScene.isoLayout.cameraPosition.z));
    CHECK(near(transientScene.isoLayout.projectedCenter.x,
        baseScene.isoLayout.projectedCenter.x));
    CHECK(near(transientScene.isoLayout.projectedCenter.y,
        baseScene.isoLayout.projectedCenter.y));
    CHECK(near(transientScene.isoLayout.fitScale,
        baseScene.isoLayout.fitScale));
}

void testExplicitCameraExtentLeavesDepthGuardBand()
{
    sokoban::RenderFrameData frame = sceneFrame();
    frame.cameraExtent = sokoban::RenderFrameData::CameraExtent {
        .originX = 2,
        .originY = 3,
        .originZ = 1,
        .width = 5,
        .height = 4,
        .depth = 2,
    };
    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 1920.0f, 1080.0f });

    const std::array<sokoban::Vec3, 8> extentCorners {
        sokoban::Vec3 { 2.0f, 3.0f, 1.0f },
        sokoban::Vec3 { 7.0f, 3.0f, 1.0f },
        sokoban::Vec3 { 7.0f, 7.0f, 1.0f },
        sokoban::Vec3 { 2.0f, 7.0f, 1.0f },
        sokoban::Vec3 { 2.0f, 3.0f, 3.0f },
        sokoban::Vec3 { 7.0f, 3.0f, 3.0f },
        sokoban::Vec3 { 7.0f, 7.0f, 3.0f },
        sokoban::Vec3 { 2.0f, 7.0f, 3.0f },
    };
    for (const sokoban::Vec3 corner : extentCorners) {
        const sokoban::Vec3 projected =
            sokoban::IsoScenePreparer::projectIsoPoint(
                scene.isoLayout,
                { 1920.0f, 1080.0f },
                corner);
        CHECK(projected.z > 0.0f);
        CHECK(projected.z < 1.0f);
    }
}

void testAdjacentWaterFacesSharePerspectiveCoordinates()
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = 4;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    frame.waterSurfaces = {
        {
            .cell = { 0, 0, 0 },
            .position = { 0.0f, 0.0f },
            .size = { 1.0f, 1.0f },
            .elevation = 0.82f,
        },
        {
            .cell = { 1, 0, 0 },
            .position = { 1.0f, 0.0f },
            .size = { 7.0f, 1.0f },
            .elevation = 0.82f,
            .pickable = false,
        },
    };

    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 1920.0f, 1080.0f });
    const auto first = std::ranges::find_if(
        scene.isoFaces,
        [](const sokoban::PreparedIsoFace& face) {
            return face.material ==
                    sokoban::PreparedSurfaceMaterial::Water &&
                face.worldOrigin.x == 0.0f;
        });
    const auto second = std::ranges::find_if(
        scene.isoFaces,
        [](const sokoban::PreparedIsoFace& face) {
            return face.material ==
                    sokoban::PreparedSurfaceMaterial::Water &&
                face.worldOrigin.x == 1.0f;
        });
    CHECK(first != scene.isoFaces.end());
    CHECK(second != scene.isoFaces.end());
    if (first != scene.isoFaces.end() &&
        second != scene.isoFaces.end()) {
        CHECK(first->clipW[1] == second->clipW[0]);
        CHECK(first->clipW[2] == second->clipW[3]);
        CHECK(first->clipW[0] != first->clipW[3]);
        CHECK(first->vertices[1].x == second->vertices[0].x);
        CHECK(first->vertices[1].y == second->vertices[0].y);
        CHECK(first->vertices[2].x == second->vertices[3].x);
        CHECK(first->vertices[2].y == second->vertices[3].y);
    }
}

void testAdjacentModelsShareProjectiveCoordinates()
{
    using namespace sokoban;

    constexpr Vec2 renderExtent { 1920.0f, 1080.0f };
    const PreparedRenderScene scene =
        prepareScene(sceneFrame(), renderExtent);

    const RenderFrameData::Tile left = cube(0, 0);
    const RenderFrameData::Tile right = cube(1, 0);
    const auto leftTransform = IsoScenePreparer::modelClipTransform(
        scene.isoLayout, renderExtent, left);
    const auto rightTransform = IsoScenePreparer::modelClipTransform(
        scene.isoLayout, renderExtent, right);

    const Vec4 leftSharedCorner =
        transformPoint(leftTransform, { 1.0f, 1.0f, 1.0f });
    const Vec4 rightSharedCorner =
        transformPoint(rightTransform, { 0.0f, 1.0f, 1.0f });
    checkNear(leftSharedCorner, rightSharedCorner);
    CHECK(!near(leftSharedCorner.w, 1.0f));

    RenderFrameData::Tile scaledLeft = left;
    scaledLeft.position = { -0.05f, -0.05f };
    scaledLeft.size = { 1.1f, 1.1f };
    scaledLeft.height = 1.1f;
    RenderFrameData::Tile scaledRight = scaledLeft;
    scaledRight.position.x = 0.95f;

    const auto scaledLeftTransform =
        IsoScenePreparer::modelClipTransform(
            scene.isoLayout, renderExtent, scaledLeft);
    const auto scaledRightTransform =
        IsoScenePreparer::modelClipTransform(
            scene.isoLayout, renderExtent, scaledRight);
    constexpr float leftLocalX = 1.05f / 1.1f;
    constexpr float rightLocalX = 0.05f / 1.1f;
    const Vec4 scaledLeftShared = transformPoint(
        scaledLeftTransform, { leftLocalX, 1.0f, 1.0f });
    const Vec4 scaledRightShared = transformPoint(
        scaledRightTransform, { rightLocalX, 1.0f, 1.0f });
    checkNear(scaledLeftShared, scaledRightShared);

    const Vec3 directlyProjected = IsoScenePreparer::projectIsoPoint(
        scene.isoLayout, renderExtent, { 1.0f, 1.05f, 1.1f });
    CHECK(near(
        scaledLeftShared.x / scaledLeftShared.w,
        directlyProjected.x));
    CHECK(near(
        scaledLeftShared.y / scaledLeftShared.w,
        directlyProjected.y));
    CHECK(near(
        scaledLeftShared.z / scaledLeftShared.w,
        directlyProjected.z));
}

void testMirrorEnergyIsTranslucentNonPickableAndShadowless()
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    frame.tiles.push_back({
        .cell = { 2, 2, 0 },
        .position = { 2.0f, 2.0f },
        .color = { 0.6f, 0.9f, 1.0f, 0.5f },
        .height = 1.0f,
        .model = { 1 },
        .effect = sokoban::RenderSurfaceEffect::MirrorEnergy,
    });
    frame.tiles.push_back({
        .cell = { 0, 0, 0 },
        .position = { 0.05f, 0.05f },
        .size = { 0.9f, 0.9f },
        .color = { 1.0f, 0.72f, 0.12f, 0.35f },
        .baseElevation = 0.05f,
        .height = 0.9f,
        .pickable = false,
        .showGrid = false,
        .effect = sokoban::RenderSurfaceEffect::GateEnergy,
    });
    frame.isoFaces.push_back({
        .vertices = {
            sokoban::Vec3 { 0.0f, 0.0f, 0.5f },
            sokoban::Vec3 { 1.0f, 0.0f, 0.5f },
            sokoban::Vec3 { 1.0f, 0.2f, 0.5f },
            sokoban::Vec3 { 0.0f, 0.2f, 0.5f },
        },
        .color = { 0.7f, 0.95f, 1.0f, 0.7f },
        .effect = sokoban::RenderSurfaceEffect::MirrorEnergy,
    });

    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 1280.0f, 720.0f });
    CHECK(scene.hasTranslucentContent);
    CHECK(scene.opaqueModelIndices.empty());
    CHECK(scene.translucentModelIndices.size() == 1);
    CHECK(scene.translucentModelIndices[0] == 0);
    CHECK(scene.shadowModelIndices.empty());
    CHECK(scene.shadowFaces.empty());
    CHECK(scene.pickFaceIndices.empty());

    const auto energyFaceIndex = std::ranges::find_if(
        scene.translucentFaceIndices,
        [&](std::size_t index) {
            return scene.isoFaces[index].material ==
                sokoban::PreparedSurfaceMaterial::MirrorEnergy;
        });
    CHECK(energyFaceIndex != scene.translucentFaceIndices.end());
    if (energyFaceIndex != scene.translucentFaceIndices.end()) {
        CHECK(std::ranges::find(
            scene.opaqueFaceIndices, *energyFaceIndex) ==
            scene.opaqueFaceIndices.end());
    }
    const auto gateFaceIndex = std::ranges::find_if(
        scene.translucentFaceIndices,
        [&](std::size_t index) {
            return scene.isoFaces[index].material ==
                sokoban::PreparedSurfaceMaterial::GateEnergy;
        });
    CHECK(gateFaceIndex != scene.translucentFaceIndices.end());
    if (gateFaceIndex != scene.translucentFaceIndices.end()) {
        CHECK(std::ranges::find(
            scene.opaqueFaceIndices, *gateFaceIndex) ==
            scene.opaqueFaceIndices.end());
    }
}

void testTranslucentIsoFaceDoesNotOccludeOrCastShadows()
{
    using namespace sokoban;

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 2;
    frame.isoFaces.push_back({
        .vertices = {
            Vec3 { 0.0f, 0.0f, 0.5f },
            Vec3 { 2.0f, 0.0f, 0.5f },
            Vec3 { 2.0f, 0.0f, 1.5f },
            Vec3 { 0.0f, 0.0f, 1.5f },
        },
        .color = { 0.7f, 0.3f, 0.9f, 0.08f },
        .translucent = true,
        .castsShadows = false,
    });

    const PreparedRenderScene scene =
        prepareScene(frame, { 1280.0f, 720.0f });
    CHECK(scene.hasTranslucentContent);
    CHECK(scene.opaqueFaceIndices.empty());
    CHECK(scene.translucentFaceIndices.size() == 1);
    CHECK(scene.shadowFaces.empty());
    if (!scene.translucentFaceIndices.empty()) {
        const PreparedIsoFace& face =
            scene.isoFaces[scene.translucentFaceIndices.front()];
        CHECK(face.material == PreparedSurfaceMaterial::Standard);
    }
}

void testLinkedObjectAuraIsTranslucentNonPickableAndShadowless()
{
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 2;
    frame.levelHeight = 2;
    frame.levelDepth = 1;
    frame.tiles.push_back({
        .cell = { 0, 0, 0 },
        .position = { -0.05f, -0.05f },
        .size = { 1.1f, 1.1f },
        .color = { 0.2f, 0.4f, 1.0f, 0.26f },
        .height = 1.1f,
        .pickable = false,
        .showGrid = false,
        .model = { 1 },
        .effect = RenderSurfaceEffect::LinkedObjectAura,
    });

    const PreparedRenderScene scene = prepareScene(frame, { 800.0f, 600.0f });
    CHECK(scene.hasTranslucentContent);
    CHECK(scene.opaqueModelIndices.empty());
    CHECK(scene.translucentModelIndices == (std::vector<std::size_t> { 0 }));
    CHECK(scene.shadowModelIndices.empty());
    CHECK(scene.shadowFaces.empty());
    CHECK(scene.pickFaceIndices.empty());
}

void testAlphaTintedModelUsesTheTranslucentPass()
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = 2;
    frame.levelHeight = 2;
    frame.levelDepth = 1;
    frame.tiles.push_back({
        .cell = { 1, 1, 0 },
        .position = { 1.0f, 1.0f },
        .color = { 0.8f, 0.7f, 0.6f, 0.4f },
        .height = 1.0f,
        .model = { 1 },
    });

    const sokoban::PreparedRenderScene scene =
        prepareScene(frame, { 1280.0f, 720.0f });
    CHECK(scene.hasTranslucentContent);
    CHECK(scene.opaqueModelIndices.empty());
    CHECK(scene.translucentModelIndices.size() == 1);
    CHECK(scene.translucentModelIndices[0] == 0);
}

void testParticlesBecomeSortedTranslucentBillboardsOnly()
{
    using namespace sokoban;

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    frame.particles = {
        RenderFrameData::Particle {
            .position = { 1.5f, 1.5f, 1.0f },
            .size = { 0.8f, 0.2f },
            .rotationRadians = 0.3f,
            .billboardAlignment = { 1.0f, 0.0f, 0.0f },
            .color = { 0.7f, 0.9f, 1.0f, 0.6f },
            .texture = RenderTexture { 5 },
            .drawOnTop = true,
            .drawOrder = 2,
        },
        RenderFrameData::Particle {
            .position = { 0.5f, 0.5f, 0.8f },
            .size = { 0.5f, 0.5f },
            .color = { 0.7f, 0.9f, 1.0f, 0.4f },
            .texture = RenderTexture { 6 },
        },
        RenderFrameData::Particle {
            .position = { 2.5f, 2.5f, 0.8f },
            .size = { 0.3f, 0.3f },
            .color = { 1.0f, 0.5f, 0.1f, 0.5f },
            .texture = RenderTexture { 7 },
            .drawOnTop = true,
            .drawOrder = 1,
        },
    };

    const PreparedRenderScene scene = prepareScene(frame, { 1280.0f, 720.0f });
    CHECK(scene.hasTranslucentContent);
    CHECK(scene.particles.size() == 3);
    CHECK(!scene.particles[0].drawOnTop);
    CHECK(scene.particles[1].drawOnTop);
    CHECK(scene.particles[1].drawOrder == 1);
    CHECK(scene.particles[2].drawOrder == 2);
    CHECK(scene.particles[0].vertices[0].x !=
        scene.particles[0].vertices[2].x);
    CHECK(scene.particles[0].vertices[0].y !=
        scene.particles[0].vertices[2].y);
    const PreparedParticle& aligned = scene.particles[2];
    const Vec3 longEdge = aligned.vertices[1] - aligned.vertices[0];
    const Vec3 projectedWorldX =
        scene.isoLayout.cameraRight *
            dot(Vec3 { 1.0f, 0.0f, 0.0f }, scene.isoLayout.cameraRight) +
        scene.isoLayout.cameraUp *
            dot(Vec3 { 1.0f, 0.0f, 0.0f }, scene.isoLayout.cameraUp);
    CHECK(near(length(longEdge), 0.8f));
    CHECK(dot(normalize(longEdge), normalize(projectedWorldX)) > 0.999f);
    CHECK(scene.pickFaceIndices.empty());
    CHECK(scene.shadowFaces.empty());
    CHECK(scene.shadowModelIndices.empty());
}

void testParticleRibbonFollowsTheProjectedWorldPath()
{
    using namespace sokoban;

    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    frame.particles.push_back({
        .position = { 1.5f, 1.5f, 1.0f },
        .size = { 0.2f, 2.0f },
        .billboardAlignment = { 1.0f, 0.0f, 0.0f },
        .billboardAlignmentUsesY = true,
        .flipTextureV = true,
        .color = { 1.0f, 0.8f, 0.2f, 1.0f },
        .textureNineSlice =
            NineSlice::symmetricFitTargetWidth({ 0.2f, 0.2f }),
        .texture = RenderTexture { 5 },
    });

    const PreparedRenderScene scene = prepareScene(frame, { 1280.0f, 720.0f });
    CHECK(scene.particles.size() == 1);
    const PreparedParticle& ribbon = scene.particles.front();
    CHECK(ribbon.flipTextureV);
    CHECK(near(ribbon.textureNineSlice.sourceBorders.x, 0.2f));
    CHECK(near(ribbon.textureNineSlice.sourceBorders.y, 0.2f));
    CHECK(ribbon.textureNineSlice.scaleMode ==
        NineSliceScaleMode::FitTargetWidth);
    const Vec3 longEdge = ribbon.vertices[3] - ribbon.vertices[0];
    const Vec3 projectedWorldPath =
        scene.isoLayout.cameraRight *
            dot(Vec3 { 2.0f, 0.0f, 0.0f }, scene.isoLayout.cameraRight) +
        scene.isoLayout.cameraUp *
            dot(Vec3 { 2.0f, 0.0f, 0.0f }, scene.isoLayout.cameraUp);
    CHECK(near(length(longEdge), length(projectedWorldPath)));
    CHECK(dot(normalize(longEdge), normalize(projectedWorldPath)) > 0.999f);
}

void testAuthoredModelTransformSupportsPivotRotationAndNonUniformScale()
{
    using namespace sokoban;
    constexpr float halfPi = 1.57079632679f;
    RenderFrameData::Tile decoration {
        .model = RenderModel { 1 },
        .modelTransform = RenderFrameData::ModelTransform {
            .translation = { 5.0f, 6.0f, 7.0f },
            .rotationRadians = { 0.0f, 0.0f, halfPi },
            .scale = { 2.0f, 3.0f, 4.0f },
        },
    };

    const ModelTransformPoints transform =
        IsoScenePreparer::modelTransformPoints(decoration);
    CHECK(near(transform.origin.x, 6.5f));
    CHECK(near(transform.origin.y, 5.0f));
    CHECK(near(transform.origin.z, 7.0f));
    CHECK(near(transform.xPoint.x, 6.5f));
    CHECK(near(transform.xPoint.y, 7.0f));
    CHECK(near(transform.yPoint.x, 3.5f));
    CHECK(near(transform.yPoint.y, 5.0f));
    CHECK(near(transform.zPoint.x, 6.5f));
    CHECK(near(transform.zPoint.y, 5.0f));
    CHECK(near(transform.zPoint.z, 11.0f));
}

} // namespace

void testRockGroundModelsRetainPaintableTops()
{
    TEST("rockGroundModelsRetainPaintableTops");
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 2;
    frame.levelHeight = 1;
    frame.levelDepth = 2;
    for (int x = 0; x < 2; ++x) {
        auto tile = cube(x, 0);
        tile.effect = RenderSurfaceEffect::GroundSplat;
        tile.model = RenderModel { static_cast<uint32_t>(x + 1) };
        frame.tiles.push_back(tile);
    }
    const Vec2 extent { 800, 600 };
    const auto scene = prepareScene(frame, extent);
    CHECK(std::ranges::count_if(scene.isoFaces, [](const auto& face) {
        return face.material == PreparedSurfaceMaterial::GroundSplat;
    }) == 2);
    CHECK(scene.opaqueModelIndices.size() == 2);
    CHECK(scene.shadowModelIndices.size() == 2);
    CHECK(scene.shadowFaces.size() == 2);
    // Only the paintable top is a drawn quad. Side picking retains square
    // logical bounds, while visible rock sides come from the glTF assets.
    for (const auto index : scene.opaqueFaceIndices) {
        CHECK(scene.isoFaces[index].material == PreparedSurfaceMaterial::GroundSplat);
    }
    for (int x = 0; x < 2; ++x) {
        const Vec3 world { static_cast<float>(x) + 0.5f, 0.5f, 1.0f };
        const Vec3 projected = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, world);
        const Vec2 pixel { (projected.x + 1.0f) * extent.x * 0.5f,
            (1.0f - projected.y) * extent.y * 0.5f };
        const auto picked = IsoScenePreparer {}.pickGridCell(scene, pixel, extent, 2, 1);
        CHECK(picked == GridPosition3({ x, 0, 0 }));
        const auto painted = IsoScenePreparer {}.pickGroundPoint(scene, pixel, extent);
        CHECK(painted.has_value());
        if (painted) {
            CHECK(near(painted->x, world.x));
            CHECK(near(painted->y, world.y));
            CHECK(near(painted->z, world.z));
        }
    }
    for (auto& tile : frame.tiles) {
        tile.groundTop = true;
        tile.effect = RenderSurfaceEffect::Standard;
        tile.color = { 1.0f, 0.0f, 0.0f, 1.0f };
    }
    const auto assigned = prepareScene(frame, extent);
    CHECK(assigned.opaqueFaceIndices.size() == 2);
    for (const auto index : assigned.opaqueFaceIndices) {
        const auto& face = assigned.isoFaces[index];
        CHECK(face.normal.z == 1.0f);
        CHECK(face.color.x == 1.0f && face.color.y == 0.0f);
    }
}

void testGroundRimCacheKeepsOwnedScenesAndCurrentFrameState()
{
    TEST("groundRimCacheKeepsOwnedScenesAndCurrentFrameState");
    using namespace sokoban;
    constexpr Vec2 extent { 1280, 720 };
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, 3, 3, 1 };
    frame.lighting.shadows.enabled = true;
    frame.lighting.pointLightCount = 1;
    frame.lighting.pointLights[0] = {
        .position = { 1.5f, 1.5f, 4 }, .intensity = 1, .range = 10,
    };
    auto tile = cube(1, 1);
    tile.model = { 1 };
    tile.effect = RenderSurfaceEffect::GroundSplat;
    tile.groundTop = true;
    tile.groundRimSides = groundAllSides;
    tile.groundRimWidth = 0.12f;
    tile.groundRimDepth = 0.10f;
    tile.groundSplat = GroundSplatTextures {
        .base = { 1 }, .detail = { 2 }, .splatMap = { 3 }, .rimWall = { 4 },
    };
    frame.tiles.push_back(tile);
    IsoScenePreparer preparer;
    preparer.setFrustumCulling(false);
    PreparedRenderScene retained;
    preparer.prepare(frame, extent, retained);
    CHECK(retained.generatedGroundRimSurfaces == 1);
    CHECK(retained.reusedGroundRimSurfaces == 0);
    CHECK(retained.groundRimSurfaceCacheBytes > 0);
    CHECK(retained.shadowFaces.size() > 1);
    CHECK(retained.pointShadowCasters[0].faceIndices.size() == retained.shadowFaces.size());
    const PreparedRenderScene retainedSnapshot = retained;
    const auto originalSurface = buildGroundRimSurface(frame.tiles[0]);
    CHECK(originalSurface.count > 0);
    const auto frontRimPoint = [&](const GroundRimSurface& surface) {
        for (std::size_t index = 0; index < surface.count; ++index) {
            const auto& patch = surface.patches[index];
            const Vec3 point = (patch.vertices[0] + patch.vertices[1] + patch.vertices[2]) / 3.0f;
            if (point.x > 1.5f && point.y > 1.5f && point.z < 1.0f) return point;
        }
        CHECK_MESSAGE(false, "front rim contains a sloped facet");
        return Vec3 { 1.5f, 1.5f, 1 };
    };
    const Vec3 originalRimPoint = frontRimPoint(originalSurface);
    const auto pixelFor = [&](const PreparedRenderScene& scene, Vec3 world) {
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, world);
        return Vec2 { (clip.x + 1) * extent.x * 0.5f, (1 - clip.y) * extent.y * 0.5f };
    };
    const auto checkPaintPoint = [&](const PreparedRenderScene& scene, Vec3 world) {
        const auto picked = preparer.pickGroundPoint(scene, pixelFor(scene, world), extent);
        CHECK(picked.has_value());
        if (picked) {
            CHECK(near(picked->x, world.x));
            CHECK(near(picked->y, world.y));
            CHECK(near(picked->z, world.z));
        }
    };
    checkPaintPoint(retained, originalRimPoint);

    // Projection and painted materials are frame state, not cached geometry.
    frame.cameraYawDegrees = 27.0f;
    frame.cameraOffset = { 0.3f, -0.2f };
    frame.tiles[0].color = { 0.2f, 0.6f, 0.4f, 1 };
    frame.tiles[0].groundSplat->splatMap = { 13 };
    frame.tiles[0].groundSplat->rimWall = { 14 };
    frame.tiles[0].groundSplatOrigin = { -7, 9 };
    PreparedRenderScene current;
    preparer.prepare(frame, extent, current);
    CHECK(current.generatedGroundRimSurfaces == 0);
    CHECK(current.reusedGroundRimSurfaces == 1);
    CHECK(current.groundRimSurfaceCacheHits > retained.groundRimSurfaceCacheHits);
    CHECK(current.groundRimSurfaceCacheRebuilds == retained.groundRimSurfaceCacheRebuilds);
    CHECK(current.shadowFaces == retained.shadowFaces);
    bool projectionChanged = false;
    for (const auto& face : current.isoFaces) {
        const auto previous = std::ranges::find_if(retained.isoFaces,
            [&](const auto& candidate) { return candidate.worldVertices == face.worldVertices; });
        // Camera movement can change which facets pass CPU back-face culling.
        if (previous != retained.isoFaces.end()) {
            projectionChanged |= face.vertices != previous->vertices;
        }
        if (face.groundRimSurface) {
            CHECK(std::find_if(originalSurface.patches.begin(),
                originalSurface.patches.begin() + originalSurface.count,
                [&](const auto& patch) { return patch.vertices == face.worldVertices; }) !=
                originalSurface.patches.begin() + originalSurface.count);
            CHECK(face.material == PreparedSurfaceMaterial::GroundSplat);
            CHECK(face.groundSplat == frame.tiles[0].groundSplat);
            CHECK(face.groundSplatOrigin == frame.tiles[0].groundSplatOrigin);
            CHECK(face.color == frame.tiles[0].color);
        }
    }
    CHECK(projectionChanged);
    checkPaintPoint(current, originalRimPoint);

    frame.tiles[0].effect = RenderSurfaceEffect::Standard;
    preparer.prepare(frame, extent, current);
    CHECK(current.generatedGroundRimSurfaces == 0);
    CHECK(current.reusedGroundRimSurfaces == 1);
    CHECK(!preparer.pickGroundPoint(current, pixelFor(current, originalRimPoint), extent));
    for (const auto& face : current.isoFaces) {
        if (face.groundRimSurface) CHECK(face.material == PreparedSurfaceMaterial::Standard);
    }

    // Readiness and draw-budget fallback both reprepare a resolved flat frame.
    // Cached rim caps must disappear from picking and every shadow list.
    frame.tiles[0].effect = RenderSurfaceEffect::GroundSplat;
    frame.tiles[0].groundRimWidth = 0;
    frame.tiles[0].groundRimDepth = 0;
    preparer.prepare(frame, extent, current);
    CHECK(current.generatedGroundRimSurfaces == 0);
    CHECK(current.reusedGroundRimSurfaces == 0);
    CHECK(current.shadowFaces.size() == 1);
    CHECK(current.pointShadowCasters[0].faceIndices.size() == 1);
    CHECK(std::ranges::none_of(current.isoFaces, [](const auto& face) { return face.groundRimSurface; }));
    checkPaintPoint(current, { originalRimPoint.x, originalRimPoint.y, 1 });

    frame.tiles[0].groundRimWidth = 0.19f;
    frame.tiles[0].groundRimDepth = 0.14f;
    preparer.prepare(frame, extent, current);
    CHECK(current.generatedGroundRimSurfaces == 1);
    CHECK(current.reusedGroundRimSurfaces == 0);
    const auto rebuiltSurface = buildGroundRimSurface(frame.tiles[0]);
    CHECK(current.shadowFaces.size() == rebuiltSurface.count);
    CHECK(current.pointShadowCasters[0].faceIndices.size() == rebuiltSurface.count);
    for (std::size_t index = 0; index < rebuiltSurface.count; ++index) {
        CHECK(current.shadowFaces[index] == rebuiltSurface.patches[index].vertices);
    }
    CHECK(current.shadowFaces != retained.shadowFaces);
    const Vec3 rebuiltRimPoint = frontRimPoint(rebuiltSurface);
    checkPaintPoint(current, rebuiltRimPoint);
    checkPaintPoint(retained, originalRimPoint);
    checkPreparationOutputsMatch(retainedSnapshot, retained);
}

void testGroundRimPatchesCoverTheSharedProfile()
{
    TEST("groundRimPatchesCoverTheSharedProfile");
    using namespace sokoban;
    RenderFrameData::Tile tile {
        .cell = { -2, 4, 2 }, .position = { -2, 4 },
        .color = { 1, 1, 1, 1 }, .baseElevation = 2, .height = 1,
        .model = { 1 }, .effect = RenderSurfaceEffect::GroundSplat,
        .groundTop = true,
    };
    tile.groundRimWidth = 0.12f;
    tile.groundRimDepth = 0.10f;
    // Exhaustive masks at positive and negative world origins include
    // straight edges, convex and concave corners, and opposing edges. Each
    // cap triangle must reproduce the actual field throughout its interior.
    for (Vec2 origin : { Vec2 { 0, 0 }, Vec2 { 17, 23 },
             Vec2 { -19, -7 }, Vec2 { 8, -11 } }) {
        tile.position = origin;
        CHECK(groundRimProfileForSurface(tile).origin == origin);
        for (uint8_t sides = 0; sides < 16; ++sides) {
            for (uint8_t corners = 0; corners < 16; ++corners) {
                tile.groundRimSides = sides;
                tile.groundRimConcaveCorners = corners;
                const auto surface = buildGroundRimSurface(tile);
                if (sides == 0 && corners == 0) {
                    CHECK(surface.count == 0);
                    continue;
                }
                CHECK(surface.count <= GroundRimSurface::capacity);
                const GroundRimProfile profile {
                    .exposedSides = sides, .concaveCorners = corners,
                    .width = tile.groundRimWidth, .depth = tile.groundRimDepth,
                    .origin = tile.position,
                };
                float area = 0;
                for (std::size_t i = 0; i < surface.count; ++i) {
                    const auto& patch = surface.patches[i];
                    CHECK(patch.normal.z > 0);
                    for (std::size_t vertex = 0; vertex < patch.vertices.size(); ++vertex) {
                        const float expectedCoverage =
                            patch.vertices[vertex].z < tile.baseElevation + tile.height
                            ? 1.0f : 0.0f;
                        CHECK(patch.wallCoverage[vertex] == expectedCoverage);
                    }
                    constexpr std::array<std::array<std::size_t, 3>, 2> triangles {{
                        { 0, 1, 2 }, { 0, 2, 3 },
                    }};
                    for (const auto& triangle : triangles) {
                        const Vec3 a = patch.vertices[triangle[0]];
                        const Vec3 b = patch.vertices[triangle[1]];
                        const Vec3 c = patch.vertices[triangle[2]];
                        const float triangleArea = cross(b - a, c - a).z * 0.5f;
                        area += triangleArea;
                        if (triangleArea == 0) continue;
                        CHECK(triangleArea > 0);
                        for (Vec3 point : { (a + b + c) / 3.0f,
                                 a * 0.2f + b * 0.3f + c * 0.5f }) {
                            const auto sample = sampleGroundRim(
                                { point.x - tile.position.x, point.y - tile.position.y }, profile);
                            const float expected = tile.baseElevation + tile.height - sample.drop;
                            CHECK(std::abs(point.z - expected) < 0.00001f);
                            CHECK(std::abs(patch.normal.x / patch.normal.z - sample.gradient.x) < 0.0001f);
                            CHECK(std::abs(patch.normal.y / patch.normal.z - sample.gradient.y) < 0.0001f);
                        }
                    }
                }
                CHECK(std::abs(area - 1.0f) < 0.00001f);
            }
        }
    }
    tile.groundRimSides = groundAllSides;
    tile.isEditorPreview = true;
    CHECK(buildGroundRimSurface(tile).count == 0);
    tile.isEditorPreview = false;
    tile.pickOnly = true;
    CHECK(buildGroundRimSurface(tile).count == 0);
}

void testGroundRimVisibleAndShadowCapsAgree()
{
    TEST("groundRimVisibleAndShadowCapsAgree");
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 1;
    auto tile = cube(1, 1);
    tile.model = { 1 };
    tile.effect = RenderSurfaceEffect::GroundSplat;
    tile.groundTop = true;
    tile.groundRimSides = groundAllSides;
    tile.groundRimWidth = 0.12f;
    tile.groundRimDepth = 0.10f;
    frame.tiles.push_back(tile);
    const auto expected = buildGroundRimSurface(tile);
    const auto scene = prepareScene(frame, { 1280, 720 });
    CHECK(scene.shadowFaces.size() == expected.count);
    for (std::size_t i = 0; i < expected.count; ++i) {
        CHECK(scene.shadowFaces[i] == expected.patches[i].vertices);
    }
    CHECK(!scene.opaqueFaceIndices.empty());
    for (const auto index : scene.opaqueFaceIndices) {
        const auto& face = scene.isoFaces[index];
        CHECK(face.material == PreparedSurfaceMaterial::GroundSplat);
        CHECK(face.gridSize == Vec2({ 1, 1 }));
        CHECK(std::ranges::find(scene.shadowFaces, face.worldVertices) !=
            scene.shadowFaces.end());
        const auto patchEnd = expected.patches.begin() + expected.count;
        const auto patch = std::find_if(expected.patches.begin(), patchEnd,
            [&](const auto& candidate) { return candidate.vertices == face.worldVertices; });
        CHECK(patch != patchEnd);
        if (patch != patchEnd) {
            CHECK(face.groundRimSurface);
            CHECK(face.groundRimWallCoverage == patch->wallCoverage);
        }
    }
    for (const auto& face : scene.isoFaces) {
        if (!face.groundRimSurface) {
            CHECK((face.groundRimWallCoverage == std::array<float, 4> {}));
        }
    }
    // Logical box remains available for selection without becoming a second,
    // flat paintable surface above the chamfer.
    CHECK(std::ranges::any_of(scene.isoFaces, [](const auto& face) {
        return face.material == PreparedSurfaceMaterial::Standard &&
            face.pickable && face.normal.z == 1;
    }));
    frame.tiles[0].groundRimWidth = 0;
    const auto disabled = prepareScene(frame, { 1280, 720 });
    CHECK(disabled.shadowFaces.size() == 1);
    CHECK(disabled.opaqueFaceIndices.size() == 1);
    const auto checkNoWallCoverage = [](const PreparedRenderScene& flatScene) {
        for (const auto& face : flatScene.isoFaces) {
            CHECK(!face.groundRimSurface);
            CHECK((face.groundRimWallCoverage == std::array<float, 4> {}));
        }
    };
    checkNoWallCoverage(disabled);
    frame.tiles[0].groundRimWidth = 0.12f;
    frame.tiles[0].groundRimSides = 0;
    frame.tiles[0].groundRimConcaveCorners = 0;
    checkNoWallCoverage(prepareScene(frame, { 1280, 720 }));
    frame.tiles[0].groundRimSides = groundAllSides;
    frame.tiles[0].isEditorPreview = true;
    checkNoWallCoverage(prepareScene(frame, { 1280, 720 }));
    frame.tiles[0].isEditorPreview = false;
    frame.tiles[0].pickOnly = true;
    checkNoWallCoverage(prepareScene(frame, { 1280, 720 }));
}

float interpolatedRimWallCoverage(
    const sokoban::GroundRimSurface& surface, sokoban::Vec2 point)
{
    if (surface.count == 0) return 0.0f;
    constexpr std::array<std::array<std::size_t, 3>, 2> triangles {{
        { 0, 1, 2 }, { 0, 2, 3 },
    }};
    std::optional<float> coverage;
    for (std::size_t index = 0; index < surface.count; ++index) {
        const auto& patch = surface.patches[index];
        for (const auto& triangle : triangles) {
            const auto a = patch.vertices[triangle[0]];
            const auto b = patch.vertices[triangle[1]];
            const auto c = patch.vertices[triangle[2]];
            const float determinant = (b.x - a.x) * (c.y - a.y) -
                (b.y - a.y) * (c.x - a.x);
            if (determinant == 0.0f) continue;
            const float u = ((point.x - a.x) * (c.y - a.y) -
                (point.y - a.y) * (c.x - a.x)) / determinant;
            const float v = ((b.x - a.x) * (point.y - a.y) -
                (b.y - a.y) * (point.x - a.x)) / determinant;
            if (u < -0.00001f || v < -0.00001f || u + v > 1.00001f) continue;
            const float interpolated = patch.wallCoverage[triangle[0]] * (1.0f - u - v) +
                patch.wallCoverage[triangle[1]] * u + patch.wallCoverage[triangle[2]] * v;
            CHECK(interpolated >= -0.0001f && interpolated <= 1.0001f);
            // A point on a patch boundary may belong to several triangles.
            // Their interpolated weights must form one continuous field.
            if (coverage) CHECK(near(*coverage, interpolated));
            else coverage = interpolated;
        }
    }
    CHECK(coverage.has_value());
    return coverage.value_or(0.0f);
}

void testGroundRimWallCoverageJoinsEveryNeighborhood()
{
    TEST("groundRimWallCoverageJoinsEveryNeighborhood");
    using namespace sokoban;
    constexpr std::array<GridPosition, 8> offsets {
        GridPosition { 0, -1 }, GridPosition { 1, 0 },
        GridPosition { 0, 1 }, GridPosition { -1, 0 },
        GridPosition { -1, -1 }, GridPosition { 1, -1 },
        GridPosition { 1, 1 }, GridPosition { -1, 1 },
    };
    constexpr std::array<std::array<std::size_t, 2>, 4> incidentSides {{
        { 0, 3 }, { 0, 1 }, { 2, 1 }, { 2, 3 },
    }};
    for (Vec2 origin : { Vec2 { 0, 0 }, Vec2 { 17, 23 }, Vec2 { -19, -7 } }) {
        for (uint32_t neighborhood = 0; neighborhood < 256; ++neighborhood) {
            const auto occupied = [&](GridPosition cell) {
                if (cell == GridPosition {}) return true;
                for (std::size_t index = 0; index < offsets.size(); ++index) {
                    if (cell == offsets[index]) return (neighborhood & (1U << index)) != 0;
                }
                return false;
            };
            const auto surfaceFor = [&](GridPosition cell) {
                auto tile = cube(cell.x, cell.y);
                tile.position = origin + tile.position;
                tile.model = { 1 };
                tile.groundTop = true;
                tile.groundRimSides = 0;
                tile.groundRimWidth = 0.12f;
                tile.groundRimDepth = 0.10f;
                std::array<bool, 4> cardinal {};
                for (std::size_t side = 0; side < cardinal.size(); ++side) {
                    cardinal[side] = occupied({ cell.x + offsets[side].x,
                        cell.y + offsets[side].y });
                    if (!cardinal[side]) tile.groundRimSides |= static_cast<uint8_t>(1U << side);
                }
                for (std::size_t corner = 0; corner < incidentSides.size(); ++corner) {
                    if (cardinal[incidentSides[corner][0]] && cardinal[incidentSides[corner][1]] &&
                        !occupied({ cell.x + offsets[4 + corner].x, cell.y + offsets[4 + corner].y })) {
                        tile.groundRimConcaveCorners |= static_cast<uint8_t>(1U << corner);
                    }
                }
                return buildGroundRimSurface(tile);
            };
            const auto center = surfaceFor({});
            for (std::size_t side = 0; side < 4; ++side) {
                if (!occupied(offsets[side])) continue;
                const auto neighbor = surfaceFor(offsets[side]);
                const bool horizontal = side == 0 || side == 2;
                const float border = horizontal ? origin.y + (side == 2 ? 1.0f : 0.0f)
                    : origin.x + (side == 1 ? 1.0f : 0.0f);
                const float start = horizontal ? origin.x : origin.y;
                std::vector<float> knots { start, start + 1.0f };
                for (const auto* surface : { &center, &neighbor }) {
                    for (std::size_t index = 0; index < surface->count; ++index) {
                        for (const auto vertex : surface->patches[index].vertices) {
                            if ((horizontal ? vertex.y : vertex.x) == border) {
                                knots.push_back(horizontal ? vertex.x : vertex.y);
                            }
                        }
                    }
                }
                std::ranges::sort(knots);
                knots.erase(std::unique(knots.begin(), knots.end()), knots.end());
                const auto checkAt = [&](float along) {
                    const Vec2 point = horizontal ? Vec2 { along, border } : Vec2 { border, along };
                    CHECK(near(interpolatedRimWallCoverage(center, point),
                        interpolatedRimWallCoverage(neighbor, point)));
                };
                for (std::size_t index = 0; index < knots.size(); ++index) {
                    checkAt(knots[index]);
                    if (index > 0) checkAt((knots[index - 1] + knots[index]) * 0.5f);
                }
            }
        }
    }
}

void testGroundRimConcaveCornerJoinsAdjacentEdge()
{
    TEST("groundRimConcaveCornerJoinsAdjacentEdge");
    using namespace sokoban;
    for (Vec2 origin : { Vec2 { 0, 0 }, Vec2 { 17, 23 },
             Vec2 { -19, -7 }, Vec2 { 8, -11 } }) {
        const GroundRimProfile concave {
            .exposedSides = 0, .concaveCorners = groundNorthEastCorner, .origin = origin,
        };
        const GroundRimProfile northEdge {
            .exposedSides = groundNorthSide, .origin = origin + Vec2 { 1, 0 },
        };
        for (int i = 0; i <= 100; ++i) {
            const float y = static_cast<float>(i) / 100.0f;
            const auto left = sampleGroundRim({ 1, y }, concave);
            const auto right = sampleGroundRim({ 0, y }, northEdge);
            CHECK(near(left.drop, right.drop));
            // Corner vertices belong to multiple facets with distinct
            // derivatives. Heights agree there; the shared edge's tangent
            // derivative agrees throughout its interior.
            if (i > 0 && i < 100) {
                CHECK(near(left.gradient.y, right.gradient.y));
            }
            // The body's upper edge and the cap use the identical drop.
            const Vec3 body = deformGroundRockPosition({ 1, y, 1 }, concave);
            CHECK(near(body.z, 1.0f - left.drop));
        }
    }
}

void testBakedGroundRimSurfacesRespectResolvedFrames()
{
    TEST("bakedGroundRimSurfacesRespectResolvedFrames");
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    auto tile = cube(1, 1);
    tile.model = { 1 };
    tile.effect = RenderSurfaceEffect::GroundSplat;
    tile.groundTop = true;
    tile.groundRimWidth = 0.12f;
    tile.groundRimDepth = 0.10f;
    frame.tiles.push_back(tile);
    frame.processedGroundArtifact = std::make_shared<const ProcessedGroundArtifact>(
        buildProcessedGroundArtifact(frame.tiles, 42));
    IsoScenePreparer preparer;
    PreparedRenderScene prepared;
    preparer.prepare(frame, { 1280, 720 }, prepared);
    CHECK(prepared.bakedGroundRimSurfaces == 1);
    CHECK(prepared.importedGroundRimSurfaces == 1);
    CHECK(prepared.generatedGroundRimSurfaces == 0);
    CHECK(prepared.groundRimArtifactImports == 1);
    const auto bakedShadowFaces = prepared.shadowFaces;
    const PreparedRenderScene retained = prepared;
    preparer.prepare(frame, { 1280, 720 }, prepared);
    CHECK(prepared.bakedGroundRimSurfaces == 1);
    CHECK(prepared.importedGroundRimSurfaces == 0);
    CHECK(prepared.reusedGroundRimSurfaces == 1);
    CHECK(prepared.shadowFaces == bakedShadowFaces);

    // Readiness/budget resolution stays authoritative over a matching artifact.
    frame.tiles[0].groundRimWidth = 0;
    preparer.prepare(frame, { 1280, 720 }, prepared);
    CHECK(prepared.bakedGroundRimSurfaces == 0);
    CHECK(prepared.importedGroundRimSurfaces == 0);
    CHECK(prepared.shadowFaces.size() == 1);
    CHECK(std::ranges::none_of(prepared.isoFaces,
        [](const auto& face) { return face.groundRimSurface; }));

    // A tuned profile misses the build output and compiles current geometry.
    frame.tiles[0].groundRimWidth = 0.17f;
    preparer.prepare(frame, { 1280, 720 }, prepared);
    CHECK(prepared.generatedGroundRimSurfaces == 1);
    CHECK(prepared.bakedGroundRimSurfaces == 0);
    CHECK(prepared.shadowFaces != bakedShadowFaces);
    frame.tiles[0].groundRimWidth = 0.12f;
    preparer.prepare(frame, { 1280, 720 }, prepared);
    CHECK(prepared.bakedGroundRimSurfaces == 1);
    CHECK(prepared.importedGroundRimSurfaces == 1);
    CHECK(prepared.shadowFaces == bakedShadowFaces);

    // A changed visible boundary cannot use a cap built for another mask.
    frame.tiles[0].groundRimSides = groundNorthSide;
    preparer.prepare(frame, { 1280, 720 }, prepared);
    CHECK(prepared.generatedGroundRimSurfaces == 1);
    CHECK(prepared.bakedGroundRimSurfaces == 0);
    CHECK(prepared.shadowFaces != bakedShadowFaces);
    CHECK(retained.shadowFaces == bakedShadowFaces);

    // Discarding the provider leaves previously prepared owning faces intact.
    frame.processedGroundArtifact.reset();
    frame.tiles[0].groundRimSides = groundAllSides;
    IsoScenePreparer livePreparer;
    PreparedRenderScene live;
    livePreparer.prepare(frame, { 1280, 720 }, live);
    CHECK(live.generatedGroundRimSurfaces == 1);
    CHECK(live.bakedGroundRimSurfaces == 0);
    CHECK(live.shadowFaces == bakedShadowFaces);
    CHECK(live.isoFaces.size() == retained.isoFaces.size());
    for (std::size_t index = 0; index < live.isoFaces.size(); ++index) {
        CHECK(live.isoFaces[index].worldVertices == retained.isoFaces[index].worldVertices);
        CHECK(live.isoFaces[index].groundRimWallCoverage == retained.isoFaces[index].groundRimWallCoverage);
    }
}

namespace {

sokoban::RenderFrameData chunkSceneFrame()
{
    using namespace sokoban;
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = 3;
    frame.levelHeight = 3;
    frame.levelDepth = 3;
    frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, 3, 3, 3 };
    frame.groundSplat = { .base = { 1 }, .detail = { 2 }, .splatMap = { 3 } };
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            auto tile = cube(x, y);
            tile.model = { 1 };
            tile.groundTop = true;
            tile.groundGeometryEligible = true;
            tile.effect = RenderSurfaceEffect::GroundSplat;
            tile.groundRimSides = (x == 1 && y == 1) ? 0 : groundAllSides;
            tile.groundRimWidth = 0.12f;
            tile.groundRimDepth = 0.10f;
            // A second height also exercises perspective-correct interpolation.
            if (x == 2) {
                tile.cell.z = 2;
                tile.baseElevation = 2.0f;
            }
            frame.tiles.push_back(tile);
        }
    }
    return frame;
}

using PositionTriangle = std::array<float, 9>;

PositionTriangle positionTriangle(sokoban::Vec3 a, sokoban::Vec3 b, sokoban::Vec3 c)
{
    const std::array<sokoban::Vec3, 3> vertices { a, b, c };
    PositionTriangle best {};
    for (std::size_t rotation = 0; rotation < 3; ++rotation) {
        PositionTriangle next {};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& vertex = vertices[(corner + rotation) % 3];
            next[corner * 3] = vertex.x;
            next[corner * 3 + 1] = vertex.y;
            next[corner * 3 + 2] = vertex.z;
        }
        if (rotation == 0 || next < best) best = next;
    }
    return best;
}

void appendQuadTriangles(std::vector<PositionTriangle>& triangles,
    const std::array<sokoban::Vec3, 4>& vertices)
{
    triangles.push_back(positionTriangle(vertices[0], vertices[1], vertices[2]));
    triangles.push_back(positionTriangle(vertices[0], vertices[2], vertices[3]));
}

std::vector<PositionTriangle> chunkTriangles(const sokoban::PreparedRenderScene& scene,
    bool visibleOnly)
{
    using namespace sokoban;
    std::vector<PositionTriangle> triangles;
    if (!scene.groundChunks) return triangles;
    for (const auto& draw : scene.groundChunkDraws) {
        const auto& chunk = scene.groundChunks->chunks[draw.chunkIndex];
        for (std::size_t first = 0; first < chunk.indices.size(); first += 3) {
            const auto& a = chunk.vertices[chunk.indices[first]];
            const auto& b = chunk.vertices[chunk.indices[first + 1]];
            const auto& c = chunk.vertices[chunk.indices[first + 2]];
            const Vec3 center = (a.position + b.position + c.position + c.position) * 0.25f;
            if (visibleOnly && (!draw.mainSceneVisible ||
                dot(a.normal, scene.isoLayout.cameraPosition - center) <= 0.0f)) continue;
            triangles.push_back(positionTriangle(a.position, b.position, c.position));
        }
    }
    std::sort(triangles.begin(), triangles.end());
    return triangles;
}

sokoban::Vec2 pixelAtWorld(const sokoban::PreparedRenderScene& scene,
    sokoban::Vec2 extent, sokoban::Vec3 world)
{
    const auto clip = sokoban::IsoScenePreparer::projectIsoPoint(scene.isoLayout,
        scene.renderExtent, world);
    return { (clip.x + 1.0f) * extent.x * 0.5f, (1.0f - clip.y) * extent.y * 0.5f };
}

void testReadyGroundChunksPreserveGeometryAndPicking()
{
    TEST("readyGroundChunksPreserveGeometryAndPicking");
    using namespace sokoban;
    constexpr Vec2 extent { 1280, 720 };
    RenderFrameData frame = chunkSceneFrame();
    IsoScenePreparer preparer;
    preparer.setFrustumCulling(false);
    PreparedRenderScene individual;
    preparer.prepare(frame, extent, individual);
    frame.groundChunksRequested = true;
    frame.groundChunksReady = true;
    frame.groundChunks = std::make_shared<const GroundChunkGeometry>(
        compileGroundChunkGeometry(frame.tiles));
    PreparedRenderScene chunked;
    preparer.prepare(frame, extent, chunked);
    CHECK(chunked.groundChunks == frame.groundChunks);
    CHECK(chunked.groundChunkDraws.size() == 2);
    CHECK(std::ranges::all_of(chunked.groundChunkTileMask, [](uint8_t value) { return value == 1; }));
    CHECK(chunked.opaqueModelIndices == individual.opaqueModelIndices);
    CHECK(chunked.shadowModelIndices == individual.shadowModelIndices);
    CHECK(chunked.shadowFaces.empty());
    CHECK(chunked.opaqueFaceIndices.empty());
    CHECK(std::ranges::none_of(chunked.isoFaces, [](const auto& face) {
        return face.groundRimSurface || face.material == PreparedSurfaceMaterial::GroundSplat;
    }));
    CHECK(!chunked.pickFaceIndices.empty());

    std::vector<PositionTriangle> visible;
    for (std::size_t index : individual.opaqueFaceIndices) {
        appendQuadTriangles(visible, individual.isoFaces[index].worldVertices);
    }
    std::sort(visible.begin(), visible.end());
    CHECK(visible == chunkTriangles(chunked, true));
    std::vector<PositionTriangle> shadows;
    for (const auto& face : individual.shadowFaces) appendQuadTriangles(shadows, face);
    std::sort(shadows.begin(), shadows.end());
    CHECK(shadows == chunkTriangles(chunked, false));

    // Paint and cell picking sample flat centres, sloped rims, both levels,
    // and projected overlaps. Compare to the established individual path.
    for (const auto& tile : frame.tiles) {
        for (float y : { 0.03f, 0.25f, 0.5f, 0.87f, 0.97f }) {
            for (float x : { 0.03f, 0.25f, 0.5f, 0.87f, 0.97f }) {
                const auto sample = sampleGroundRim({ x, y }, groundRimProfileForSurface(tile));
                const Vec3 world { tile.position.x + x, tile.position.y + y,
                    tile.baseElevation + tile.height - sample.drop };
                const Vec2 pixel = pixelAtWorld(individual, extent, world);
                const auto oldPoint = preparer.pickGroundPoint(individual, pixel, extent);
                const auto newPoint = preparer.pickGroundPoint(chunked, pixel, extent);
                CHECK(oldPoint.has_value() == newPoint.has_value());
                if (oldPoint && newPoint) {
                    CHECK(near(oldPoint->x, newPoint->x));
                    CHECK(near(oldPoint->y, newPoint->y));
                    CHECK(near(oldPoint->z, newPoint->z));
                }
                CHECK(preparer.pickGridCell(individual, pixel, extent, 3, 3) ==
                    preparer.pickGridCell(chunked, pixel, extent, 3, 3));
            }
        }
    }

    TaskSystem tasks(2);
    IsoScenePreparer serialPreparer;
    IsoScenePreparer parallelPreparer;
    PreparedRenderScene serial;
    PreparedRenderScene parallel;
    serialPreparer.prepare(frame, extent, serial);
    parallelPreparer.prepare(frame, extent, parallel, &tasks);
    checkPreparationOutputsMatch(serial, parallel);
}

void testGroundChunkPreparationFallsBackWholeChunks()
{
    TEST("groundChunkPreparationFallsBackWholeChunks");
    using namespace sokoban;
    constexpr Vec2 extent { 1280, 720 };
    RenderFrameData frame = chunkSceneFrame();
    frame.groundChunks = std::make_shared<const GroundChunkGeometry>(
        compileGroundChunkGeometry(frame.tiles));
    frame.groundChunksRequested = true;
    const auto notReady = prepareScene(frame, extent);
    CHECK(notReady.groundChunkDraws.empty());
    CHECK(!notReady.shadowFaces.empty());
    frame.groundChunksReady = true;
    const auto ready = prepareScene(frame, extent);
    CHECK(ready.groundChunkDraws.size() == 2);

    // A stale member invalidates its whole layer/chunk, while other complete
    // chunks can remain on the uploaded path. There is no partial cap removal.
    frame.tiles[0].groundRimWidth = 0.17f;
    const auto stale = prepareScene(frame, extent);
    CHECK(stale.groundChunkDraws.size() == 1);
    for (std::size_t index = 0; index < frame.tiles.size(); ++index) {
        CHECK(stale.groundChunkTileMask[index] == (frame.tiles[index].cell.z == 2 ? 1 : 0));
    }
    frame.tiles[0].groundRimWidth = 0.12f;
    frame.tiles[0].groundSplat = GroundSplatTextures {};
    const auto invalidTileMaterial = prepareScene(frame, extent);
    CHECK(invalidTileMaterial.groundChunkDraws.size() == 1);
    CHECK(invalidTileMaterial.groundChunkTileMask[0] == 0);
    frame.tiles[0].groundSplat.reset();
    frame.groundSplatRegionCount = 1;
    frame.groundSplatRegions[0] = { .origin = { 0, 0 }, .width = 1, .height = 1 };
    CHECK(prepareScene(frame, extent).groundChunkDraws.size() == 1);
    frame.groundSplatRegions[0].textures = frame.groundSplat;
    CHECK(prepareScene(frame, extent).groundChunkDraws.size() == 2);
    frame.groundSplatRegionCount = 0;
    frame.groundSplat = {};
    CHECK(prepareScene(frame, extent).groundChunkDraws.empty());
    frame.groundSplat = { .base = { 1 }, .detail = { 2 }, .splatMap = { 3 } };

    frame.tiles[0].isEditorPreview = true;
    CHECK(prepareScene(frame, extent).groundChunkDraws.size() == 1);
    frame.tiles[0].isEditorPreview = false;
    frame.tiles.push_back(frame.tiles[0]);
    CHECK(prepareScene(frame, extent).groundChunkDraws.size() == 1);
    frame.tiles.erase(frame.tiles.end() - 1);
    frame.lighting.pointLightCount = 1;
    frame.lighting.pointLights[0] = { .position = { 1, 1, 5 }, .intensity = 1, .range = 10 };
    const auto pointShadows = prepareScene(frame, extent);
    CHECK(pointShadows.groundChunkDraws.empty());
    CHECK(!pointShadows.shadowFaces.empty());
    frame.lighting.pointLights[0].castsShadows = false;
    CHECK(prepareScene(frame, extent).groundChunkDraws.size() == 2);
    frame.groundChunksRequested = false;
    CHECK(prepareScene(frame, extent).groundChunkDraws.empty());
}

void testGroundChunkFrustumCullingKeepsSunCastersAndPicking()
{
    TEST("groundChunkFrustumCullingKeepsSunCastersAndPicking");
    using namespace sokoban;
    constexpr Vec2 extent { 1280, 720 };
    RenderFrameData frame = chunkSceneFrame();
    const auto visibleTile = frame.tiles[0];
    auto outsideTile = visibleTile;
    outsideTile.cell = { 100, 0, 0 };
    outsideTile.position = { 100.0f, 0.0f };
    outsideTile.affectsCameraFit = false;
    frame.tiles = { visibleTile, outsideTile };
    frame.levelWidth = 101;
    frame.cameraOverride = RenderFrameData::CameraOverride {
        .position = { 0.5f, 0.5f, 8.0f },
        .forward = { 0.0f, 0.0f, -1.0f },
        .verticalFovDegrees = 45.0f,
    };
    IsoScenePreparer preparer;
    PreparedRenderScene individual;
    preparer.prepare(frame, extent, individual);
    std::vector<PositionTriangle> individualSunTriangles;
    for (const auto& face : individual.shadowFaces) {
        appendQuadTriangles(individualSunTriangles, face);
    }
    std::sort(individualSunTriangles.begin(), individualSunTriangles.end());

    frame.groundChunksRequested = true;
    frame.groundChunksReady = true;
    frame.groundChunks = std::make_shared<const GroundChunkGeometry>(
        compileGroundChunkGeometry(frame.tiles));
    const auto visibilityByTile = [](const PreparedRenderScene& scene) {
        std::array<bool, 2> visible {};
        for (const auto& draw : scene.groundChunkDraws) {
            for (std::size_t slot = 0; slot < draw.tileCount; ++slot) {
                CHECK(draw.tileIndices[slot] < visible.size());
                if (draw.tileIndices[slot] < visible.size()) {
                    visible[draw.tileIndices[slot]] = draw.mainSceneVisible;
                }
            }
        }
        return visible;
    };

    PreparedRenderScene culled;
    preparer.prepare(frame, extent, culled);
    CHECK(culled.groundChunks == frame.groundChunks);
    CHECK(culled.groundChunkDraws.size() == 2);
    const auto initialVisibility = visibilityByTile(culled);
    CHECK(initialVisibility[0] && !initialVisibility[1]);
    // The model-backed tiles deliberately fail open; the actual cap bounds
    // still cull the second main draw. Both cap caster records stay present.
    CHECK(culled.renderables[0].mainSceneVisible && culled.renderables[1].mainSceneVisible);
    CHECK(culled.shadowFaces.empty());
    CHECK(culled.shadowModelIndices == individual.shadowModelIndices);
    CHECK(chunkTriangles(culled, false) == individualSunTriangles);
    CHECK(std::ranges::all_of(culled.groundChunkTileMask,
        [](uint8_t value) { return value == 1; }));
    const Vec3 visibleWorld { 0.5f, 0.5f, 1.0f };
    const Vec2 visiblePixel = pixelAtWorld(culled, extent, visibleWorld);
    const auto visiblePaint = preparer.pickGroundPoint(culled, visiblePixel, extent);
    const auto visibleCell = preparer.pickGridCell(culled, visiblePixel, extent, 101, 3);
    CHECK(visiblePaint.has_value());
    CHECK(visibleCell.has_value());

    preparer.setFrustumCulling(false);
    PreparedRenderScene unculled;
    preparer.prepare(frame, extent, unculled);
    const auto unculledVisibility = visibilityByTile(unculled);
    CHECK(unculledVisibility[0] && unculledVisibility[1]);
    CHECK(unculled.groundChunkDraws.size() == culled.groundChunkDraws.size());
    CHECK(unculled.groundChunks == culled.groundChunks);
    CHECK(unculled.groundChunkTileMask == culled.groundChunkTileMask);
    CHECK(unculled.shadowFaces == culled.shadowFaces);
    CHECK(unculled.shadowModelIndices == culled.shadowModelIndices);
    CHECK(chunkTriangles(unculled, false) == individualSunTriangles);
    CHECK(unculled.pickFaceIndices == culled.pickFaceIndices);
    CHECK(preparer.pickGroundPoint(unculled, visiblePixel, extent) == visiblePaint);
    CHECK(preparer.pickGridCell(unculled, visiblePixel, extent, 101, 3) == visibleCell);

    preparer.setFrustumCulling(true);
    frame.cameraOverride->position.x = 100.5f;
    PreparedRenderScene movedCamera;
    preparer.prepare(frame, extent, movedCamera);
    const auto movedVisibility = visibilityByTile(movedCamera);
    CHECK(!movedVisibility[0] && movedVisibility[1]);
    CHECK(movedCamera.groundChunkDraws.size() == 2);
    CHECK(movedCamera.shadowFaces.empty());
    CHECK(chunkTriangles(movedCamera, false) == individualSunTriangles);
    const Vec2 outsidePixel = pixelAtWorld(movedCamera, extent, { 100.5f, 0.5f, 1.0f });
    CHECK(preparer.pickGroundPoint(movedCamera, outsidePixel, extent).has_value());
    const auto outsideCell = preparer.pickGridCell(movedCamera, outsidePixel, extent, 101, 3);
    CHECK(outsideCell.has_value());
    if (outsideCell) CHECK(outsideCell->x == 100);

    // Invalid bounds are never grounds for removing a valid participating cap.
    const auto validGeometry = frame.groundChunks;
    for (int invalidKind = 0; invalidKind < 3; ++invalidKind) {
        auto invalid = std::make_shared<GroundChunkGeometry>(*validGeometry);
        for (auto& chunk : invalid->chunks) {
            if (invalidKind == 0) chunk.bounds = {};
            if (invalidKind == 1) chunk.bounds.minimum.x = std::numeric_limits<float>::quiet_NaN();
            if (invalidKind == 2) chunk.bounds.maximum.x = std::numeric_limits<float>::infinity();
        }
        frame.groundChunks = invalid;
        PreparedRenderScene failOpen;
        preparer.prepare(frame, extent, failOpen);
        const auto failOpenVisibility = visibilityByTile(failOpen);
        CHECK(failOpenVisibility[0] && failOpenVisibility[1]);
        CHECK(failOpen.groundChunkDraws.size() == 2);
        CHECK(failOpen.shadowFaces.empty());
        CHECK(chunkTriangles(failOpen, false) == individualSunTriangles);
    }
}

void testGroundChunkPickingRetainsOwnedFramesAndPickFlags()
{
    TEST("groundChunkPickingRetainsOwnedFramesAndPickFlags");
    using namespace sokoban;
    constexpr Vec2 extent { 1280, 720 };
    RenderFrameData frame = chunkSceneFrame();
    frame.groundChunksRequested = true;
    frame.groundChunksReady = true;
    GroundChunkGeometryCache cache;
    frame.groundChunks = cache.update(frame.tiles);
    IsoScenePreparer preparer;
    PreparedRenderScene retained;
    preparer.prepare(frame, extent, retained);
    const auto oldGeometry = retained.groundChunks;
    const Vec3 oldWorld { 0.5f, 0.5f, 1.0f };
    const Vec2 oldPixel = pixelAtWorld(retained, extent, oldWorld);
    const auto oldPaint = preparer.pickGroundPoint(retained, oldPixel, extent);
    const auto oldCell = preparer.pickGridCell(retained, oldPixel, extent, 3, 3);
    CHECK(oldPaint.has_value());
    CHECK(oldCell.has_value());

    for (auto& tile : frame.tiles) tile.pickable = false;
    std::rotate(frame.tiles.begin(), frame.tiles.begin() + 1, frame.tiles.end());
    frame.cameraYawDegrees = 57.0f;
    PreparedRenderScene current;
    preparer.prepare(frame, extent, current);
    CHECK(current.groundChunks == oldGeometry);
    CHECK(!preparer.pickGridCell(current, pixelAtWorld(current, extent, oldWorld), extent, 3, 3));
    CHECK(preparer.pickGroundPoint(current, pixelAtWorld(current, extent, oldWorld), extent).has_value());
    CHECK(preparer.pickGridCell(retained, oldPixel, extent, 3, 3) == oldCell);
    CHECK(preparer.pickGroundPoint(retained, oldPixel, extent) == oldPaint);
    for (auto& tile : frame.tiles) tile.baseElevation += 1.0f;
    frame.groundChunks = cache.update(frame.tiles);
    preparer.prepare(frame, extent, current);
    CHECK(current.groundChunks != oldGeometry);
    frame.groundChunks.reset();
    cache.invalidate();
    CHECK(retained.groundChunks == oldGeometry);
    CHECK(preparer.pickGroundPoint(retained, oldPixel, extent) == oldPaint);
    CHECK(preparer.pickGridCell(retained, oldPixel, extent, 3, 3) == oldCell);
}

} // namespace

int main()
{
    testReadyGroundChunksPreserveGeometryAndPicking();
    testGroundChunkPreparationFallsBackWholeChunks();
    testGroundChunkFrustumCullingKeepsSunCastersAndPicking();
    testGroundChunkPickingRetainsOwnedFramesAndPickFlags();
    testBakedGroundRimSurfacesRespectResolvedFrames();
    testGroundRimCacheKeepsOwnedScenesAndCurrentFrameState();
    testGroundRimPatchesCoverTheSharedProfile();
    testGroundRimVisibleAndShadowCapsAgree();
    testGroundRimWallCoverageJoinsEveryNeighborhood();
    testGroundRimConcaveCornerJoinsAdjacentEdge();
    testRockGroundModelsRetainPaintableTops();
    testParallelAuxiliaryPreparationMatchesSerialOutput();
    testPointShadowCastersAreRangeCulledConservatively();
    testPointShadowFaceCacheRequiresExactStableGeometry();
    testCameraLayoutUsesConfiguredAngles();
    testExplicitCameraPoseBypassesBoardFit();
    testDetachedCameraRetainsFloorMarkerDepthSeparation();
    testPreparationCategorizesOneSharedFacePool();
    testPassListsAreDepthSorted();
    testOpaqueListEndsWithABackToFrontBlendedTail();
    testDepthRangeCoversTilesOutsideTheAuthoredCameraFit();
    testCameraMatrixReproducesTheScalarProjection();
    testShadowMatrixReproducesTheScalarProjection();
    testModelWorldTransformComposesToTheClipTransform();
    testPickingConsumesPreparedFaces();
    testModelBackedPickFacesUseLogicalBounds();
    testPickingTracksAuthoredCameraAngles();
    testPickingHonorsConfiguredGridBorder();
    testScaledBorderTileCannotLeakIntoBoardPicking();
    testVirtualPickPlaneMatchesPreviewTopUnderPerspective();
    testTopDownPreparationSkipsIsoWork();
    testPreparationReusesOutputWithoutStaleLists();
    testPersistentRenderablesReuseAndReviseStableBounds();
    testFrustumCullingFiltersOnlyMainSceneDrawLists();
    testExteriorWaterDoesNotAffectCameraFitOrPicking();
    testExteriorWaterReachesVisiblePlaneFootprint();
    testDecorativeTileDoesNotAffectCameraFit();
    testExplicitCameraExtentOwnsEntireProjectedLayout();
    testExplicitCameraExtentLeavesDepthGuardBand();
    testAdjacentWaterFacesSharePerspectiveCoordinates();
    testAdjacentModelsShareProjectiveCoordinates();
    testMirrorEnergyIsTranslucentNonPickableAndShadowless();
    testLinkedObjectAuraIsTranslucentNonPickableAndShadowless();
    testTranslucentIsoFaceDoesNotOccludeOrCastShadows();
    testAlphaTintedModelUsesTheTranslucentPass();
    testParticlesBecomeSortedTranslucentBillboardsOnly();
    testParticleRibbonFollowsTheProjectedWorldPath();
    testAuthoredModelTransformSupportsPivotRotationAndNonUniformScale();

    if (failures == 0) {
        std::cout << "IsoScenePreparerTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "IsoScenePreparerTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
