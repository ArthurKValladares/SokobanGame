#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/CameraConfig.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
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
    auto sorted = [&](const std::vector<std::size_t>& indices) {
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (scene.isoFaces[indices[i - 1]].depth <
                scene.isoFaces[indices[i]].depth) {
                return false;
            }
        }
        return true;
    };
    CHECK(sorted(scene.opaqueFaceIndices));
    CHECK(sorted(scene.translucentFaceIndices));
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

} // namespace

int main()
{
    testCameraLayoutUsesConfiguredAngles();
    testPreparationCategorizesOneSharedFacePool();
    testPassListsAreDepthSorted();
    testPickingConsumesPreparedFaces();
    testPickingHonorsConfiguredGridBorder();
    testVirtualPickPlaneMatchesPreviewTopUnderPerspective();
    testTopDownPreparationSkipsIsoWork();
    testPreparationReusesOutputWithoutStaleLists();
    testExteriorWaterDoesNotAffectCameraFitOrPicking();
    testDecorativeTileDoesNotAffectCameraFit();
    testAdjacentWaterFacesSharePerspectiveCoordinates();
    testMirrorEnergyIsTranslucentNonPickableAndShadowless();
    testParticlesBecomeSortedTranslucentBillboardsOnly();

    if (failures == 0) {
        std::cout << "IsoScenePreparerTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "IsoScenePreparerTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
