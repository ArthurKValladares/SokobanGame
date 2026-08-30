#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/PointShadowFaceCache.hpp"
#include "engine/TaskSystem.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <ranges>
#include <unordered_set>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

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
    CHECK(expected.isoFaces.size() == actual.isoFaces.size());
    CHECK(expected.renderables.size() == actual.renderables.size());
    CHECK(expected.particles.size() == actual.particles.size());
    for (std::size_t index = 0; index < expected.isoFaces.size(); ++index) {
        CHECK(expected.isoFaces[index].worldVertices ==
              actual.isoFaces[index].worldVertices);
        CHECK(expected.isoFaces[index].vertices ==
              actual.isoFaces[index].vertices);
        CHECK(expected.isoFaces[index].depth == actual.isoFaces[index].depth);
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
        CHECK(expected.particles[index].texture ==
              actual.particles[index].texture);
        CHECK(expected.particles[index].depth == actual.particles[index].depth);
        CHECK(expected.particles[index].drawOnTop ==
              actual.particles[index].drawOnTop);
    }
}

void testParallelAuxiliaryPreparationMatchesSerialOutput()
{
    using namespace sokoban;

    RenderFrameData frame = sceneFrame();
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
    CHECK(preparationTasks.executedTaskCount() == 1);
    checkPreparationOutputsMatch(serialScene, parallelScene);

    // A second frame also exercises retained-bound reuse on the foreground
    // path while the auxiliary vectors are rebuilt by the worker.
    serialPreparer.prepare(frame, extent, serialScene);
    parallelPreparer.prepare(
        frame, extent, parallelScene, &preparationTasks);
    CHECK(preparationTasks.executedTaskCount() == 2);
    CHECK(serialScene.reusedRenderableBounds ==
          parallelScene.reusedRenderableBounds);
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
    models[0].ready = false;
    CHECK(!cache.reusable(0, light, faces, indices, models));

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
        for (sokoban::Vec3 vertex : face.vertices) {
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
            .size = { 0.8f, 0.8f },
            .rotationRadians = 0.3f,
            .color = { 0.7f, 0.9f, 1.0f, 0.6f },
            .texture = RenderTexture { 5 },
            .drawOnTop = true,
        },
        RenderFrameData::Particle {
            .position = { 0.5f, 0.5f, 0.8f },
            .size = { 0.5f, 0.5f },
            .color = { 0.7f, 0.9f, 1.0f, 0.4f },
            .texture = RenderTexture { 6 },
        },
    };

    const PreparedRenderScene scene = prepareScene(frame, { 1280.0f, 720.0f });
    CHECK(scene.hasTranslucentContent);
    CHECK(scene.particles.size() == 2);
    CHECK(!scene.particles[0].drawOnTop);
    CHECK(scene.particles[1].drawOnTop);
    CHECK(scene.particles[0].vertices[0].x !=
        scene.particles[0].vertices[2].x);
    CHECK(scene.particles[0].vertices[0].y !=
        scene.particles[0].vertices[2].y);
    CHECK(scene.pickFaceIndices.empty());
    CHECK(scene.shadowFaces.empty());
    CHECK(scene.shadowModelIndices.empty());
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

int main()
{
    testParallelAuxiliaryPreparationMatchesSerialOutput();
    testPointShadowCastersAreRangeCulledConservatively();
    testPointShadowFaceCacheRequiresExactStableGeometry();
    testCameraLayoutUsesConfiguredAngles();
    testPreparationCategorizesOneSharedFacePool();
    testPassListsAreDepthSorted();
    testOpaqueListEndsWithABackToFrontBlendedTail();
    testDepthRangeCoversTilesOutsideTheAuthoredCameraFit();
    testCameraMatrixReproducesTheScalarProjection();
    testShadowMatrixReproducesTheScalarProjection();
    testModelWorldTransformComposesToTheClipTransform();
    testPickingConsumesPreparedFaces();
    testModelBackedPickFacesUseLogicalBounds();
    testPickingHonorsConfiguredGridBorder();
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
    testAlphaTintedModelUsesTheTranslucentPass();
    testParticlesBecomeSortedTranslucentBillboardsOnly();
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
