#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
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

} // namespace

int main()
{
    testPreparationCategorizesOneSharedFacePool();
    testPassListsAreDepthSorted();
    testPickingConsumesPreparedFaces();
    testTopDownPreparationSkipsIsoWork();
    testPreparationReusesOutputWithoutStaleLists();

    if (failures == 0) {
        std::cout << "IsoScenePreparerTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "IsoScenePreparerTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
