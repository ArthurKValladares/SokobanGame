// Headless tests for continuous ground picking, the mouse-to-world step that
// brush painting stands on.
//
// The strategy throughout is round-tripping: take a known world position,
// project it to a pixel with the same projection the renderer uses, pick that
// pixel, and require the original position back. That catches sign flips,
// swapped axes, and interpolation error without needing a GPU.

#include "TestHarness.hpp"

#include "engine/render/IsoScenePreparer.hpp"

#include <cmath>
#include <iostream>

namespace {

using namespace sokoban;

constexpr Vec2 outputExtent { 1280.0f, 720.0f };

// A flat board of ground tiles, which is what the editor paints on.
[[nodiscard]] RenderFrameData groundFrame(uint32_t width, uint32_t height)
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.levelWidth = width;
    frame.levelHeight = height;
    frame.levelDepth = 1;
    frame.cameraExtent = { 0, 0, 0, width, height, 1 };
    // Non-zero handles, so the recorder would take the splat path and the
    // preparer marks these tops as GroundSplat.
    frame.groundSplat = {
        .base = RenderTexture { 1 },
        .detail = RenderTexture { 2 },
        .splatMap = RenderTexture { 3 },
    };
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            frame.tiles.push_back({
                .cell = { static_cast<int>(x), static_cast<int>(y), 0 },
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .size = { 1.0f, 1.0f },
                .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                .effect = RenderSurfaceEffect::GroundSplat,
            });
        }
    }
    return frame;
}

// Pixel position of a world point, mirroring VulkanRenderer's clip-to-pixel
// conversion so the test drives picking exactly as the renderer does.
[[nodiscard]] Vec2 worldToPixel(
    const PreparedRenderScene& scene, Vec3 world)
{
    const Vec3 clip = IsoScenePreparer::projectIsoPoint(
        scene.isoLayout, scene.renderExtent, world);
    return {
        (clip.x + 1.0f) * 0.5f * outputExtent.x,
        (1.0f - clip.y) * 0.5f * outputExtent.y,
    };
}

void testPickReturnsTheProjectedWorldPosition()
{
    TEST("pickReturnsTheProjectedWorldPosition");
    const IsoScenePreparer preparer;
    PreparedRenderScene scene;
    // The largest board the campaign currently has: perspective error grows
    // with the distance a face spans, so this is the demanding case.
    preparer.prepare(groundFrame(21, 16), outputExtent, scene);

    // Sample a spread of positions, including sub-tile offsets: a brush must
    // land where the pointer is, not snapped to a tile centre.
    const Vec2 samples[] = {
        { 0.5f, 0.5f }, { 10.5f, 8.5f }, { 20.5f, 15.5f },
        { 2.25f, 1.75f }, { 15.52f, 15.52f }, { 7.0f, 3.0f },
    };
    for (const Vec2 expected : samples) {
        const Vec2 pixel =
            worldToPixel(scene, { expected.x, expected.y, 0.0f });
        const std::optional<Vec3> picked =
            preparer.pickGroundPoint(scene, pixel, outputExtent);
        CHECK(picked.has_value());
        if (!picked) {
            continue;
        }
        // 0.002 tiles is a sixteenth of a texel, and is deliberately tighter
        // than affine interpolation can manage (it peaks around 0.004 tiles
        // near the far corner of a board this size). The error is too small to
        // see when painting either way; the tight bound is here so that
        // dropping the perspective divide shows up as a test failure rather
        // than as silent drift.
        CHECK(std::abs(picked->x - expected.x) < 0.002f);
        CHECK(std::abs(picked->y - expected.y) < 0.002f);
    }
}

void testPickIsMonotonicAcrossTheBoard()
{
    TEST("pickIsMonotonicAcrossTheBoard");
    const IsoScenePreparer preparer;
    PreparedRenderScene scene;
    preparer.prepare(groundFrame(10, 10), outputExtent, scene);

    // Walking one axis in world space must move the picked position along the
    // same axis, and leave the other roughly alone. This is what catches an
    // axis swap or a mirrored face-coordinate table.
    std::optional<Vec3> previous;
    for (float x = 0.5f; x < 9.5f; x += 1.0f) {
        const Vec2 pixel = worldToPixel(scene, { x, 5.5f, 0.0f });
        const std::optional<Vec3> picked =
            preparer.pickGroundPoint(scene, pixel, outputExtent);
        CHECK(picked.has_value());
        if (picked && previous) {
            CHECK(picked->x > previous->x);
            CHECK(std::abs(picked->y - previous->y) < 0.05f);
        }
        previous = picked;
    }
}

void testPickMissesOutsideTheBoard()
{
    TEST("pickMissesOutsideTheBoard");
    const IsoScenePreparer preparer;
    PreparedRenderScene scene;
    preparer.prepare(groundFrame(4, 4), outputExtent, scene);

    // Corners of the viewport are off the board for a small centred board.
    CHECK(!preparer.pickGroundPoint(scene, { 1.0f, 1.0f }, outputExtent));
    CHECK(!preparer.pickGroundPoint(
        scene, { outputExtent.x - 1.0f, 1.0f }, outputExtent));
    // Well outside the viewport entirely.
    CHECK(!preparer.pickGroundPoint(scene, { -500.0f, -500.0f }, outputExtent));
    CHECK(!preparer.pickGroundPoint(scene, { 99999.0f, 99999.0f }, outputExtent));
    // A degenerate viewport must not divide by zero.
    CHECK(!preparer.pickGroundPoint(scene, { 10.0f, 10.0f }, { 0.0f, 0.0f }));
}

void testOnlySplattableGroundIsPaintable()
{
    TEST("onlySplattableGroundIsPaintable");
    const IsoScenePreparer preparer;

    // Same board, but the tiles are ordinary surfaces rather than splatted
    // ground. Picking a cell still works elsewhere in the editor; painting
    // must find nothing, so a stroke over a wall does not smear onto it.
    RenderFrameData frame = groundFrame(6, 6);
    for (RenderFrameData::Tile& tile : frame.tiles) {
        tile.effect = RenderSurfaceEffect::Standard;
    }
    PreparedRenderScene scene;
    preparer.prepare(frame, outputExtent, scene);

    const Vec2 pixel = worldToPixel(scene, { 3.5f, 3.5f, 0.0f });
    CHECK(!preparer.pickGroundPoint(scene, pixel, outputExtent));
    // The cell pick still succeeds there, proving the board really is under
    // the cursor and the miss above is about material, not geometry.
    CHECK(preparer.pickGridCell(scene, pixel, outputExtent, 6, 6).has_value());
}

void testNearestSurfaceWins()
{
    TEST("nearestSurfaceWins");
    const IsoScenePreparer preparer;
    RenderFrameData frame = groundFrame(6, 6);
    // Raise one tile into a block. Its top is splattable and closer to the
    // camera than the flat ground behind it, so a pointer over the overlap
    // must resolve to the raised top rather than punching through.
    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.cell.x == 3 && tile.cell.y == 3) {
            tile.height = 1.0f;
        }
    }
    PreparedRenderScene scene;
    preparer.prepare(frame, outputExtent, scene);

    const Vec2 pixel = worldToPixel(scene, { 3.5f, 3.5f, 1.0f });
    const std::optional<Vec3> picked =
        preparer.pickGroundPoint(scene, pixel, outputExtent);
    CHECK(picked.has_value());
    if (picked) {
        CHECK(std::abs(picked->x - 3.5f) < 0.05f);
        CHECK(std::abs(picked->y - 3.5f) < 0.05f);
    }
}

void testPickReportsTheSurfaceHeight()
{
    TEST("pickReportsTheSurfaceHeight");
    const IsoScenePreparer preparer;

    // Regression: the brush preview ring is drawn by projecting world points,
    // so it needs the height of the surface the pointer actually hit. Assuming
    // one puts the ring off the ground - visibly below the paint, and by more
    // the further the surface is from the camera.
    RenderFrameData frame = groundFrame(6, 6);
    for (RenderFrameData::Tile& tile : frame.tiles) {
        // Ground raised off z=0, as it is whenever it sits on a layer above
        // the bottom one.
        tile.baseElevation = 2.0f;
        if (tile.cell.x == 4 && tile.cell.y == 4) {
            tile.height = 1.0f; // a block: its top is a unit higher again
        }
    }
    PreparedRenderScene scene;
    preparer.prepare(frame, outputExtent, scene);

    const std::optional<Vec3> flat = preparer.pickGroundPoint(
        scene, worldToPixel(scene, { 1.5f, 1.5f, 2.0f }), outputExtent);
    CHECK(flat.has_value());
    CHECK(flat && std::abs(flat->z - 2.0f) < 0.001f);

    const std::optional<Vec3> raised = preparer.pickGroundPoint(
        scene, worldToPixel(scene, { 4.5f, 4.5f, 3.0f }), outputExtent);
    CHECK(raised.has_value());
    CHECK(raised && std::abs(raised->z - 3.0f) < 0.001f);

    // And the height must round-trip: projecting the reported point back must
    // land on the pixel that was picked, which is exactly what the ring does.
    if (flat) {
        const Vec2 target = worldToPixel(scene, { 1.5f, 1.5f, 2.0f });
        const Vec2 reprojected = worldToPixel(scene, *flat);
        CHECK(std::abs(reprojected.x - target.x) < 1.0f);
        CHECK(std::abs(reprojected.y - target.y) < 1.0f);
    }
}

void testEditorPreviewGroundIsStillPaintable()
{
    TEST("editorPreviewGroundIsStillPaintable");
    const IsoScenePreparer preparer;

    // Regression: the editor draws layers other than the active one as
    // dithered previews, and preview faces are deliberately excluded from the
    // pick list used for placing tiles. Ground almost always lives on layer 0
    // while the active layer is above it, so keying painting off that list
    // made the brush silently dead in the common case. Which tile layer is
    // being edited has nothing to do with painting the ground's texture.
    RenderFrameData frame = groundFrame(6, 6);
    for (RenderFrameData::Tile& tile : frame.tiles) {
        tile.isEditorPreview = true;
    }
    PreparedRenderScene scene;
    preparer.prepare(frame, outputExtent, scene);

    const Vec2 pixel = worldToPixel(scene, { 3.5f, 3.5f, 0.0f });
    const std::optional<Vec3> picked =
        preparer.pickGroundPoint(scene, pixel, outputExtent);
    CHECK(picked.has_value());
    if (picked) {
        CHECK(std::abs(picked->x - 3.5f) < 0.05f);
        CHECK(std::abs(picked->y - 3.5f) < 0.05f);
    }
    // Tile picking still excludes previews - that behaviour is unchanged.
    CHECK(!preparer.pickGridCell(scene, pixel, outputExtent, 6, 6).has_value());
}

void testPickOnlyPlanesAreNotPaintable()
{
    TEST("pickOnlyPlanesAreNotPaintable");
    const IsoScenePreparer preparer;

    // The editor floats invisible pick planes above the board for tile
    // placement. They sit nearer the camera than the ground, so painting must
    // ignore them rather than resolving strokes onto a plane one unit up.
    RenderFrameData frame = groundFrame(6, 6);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            frame.tiles.push_back({
                .cell = { x, y, 0 },
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .size = { 1.0f, 1.0f },
                .baseElevation = 1.0f,
                .pickOnly = true,
            });
        }
    }
    PreparedRenderScene scene;
    preparer.prepare(frame, outputExtent, scene);

    // Aim at where the ground is, not where the plane is.
    const Vec2 pixel = worldToPixel(scene, { 3.5f, 3.5f, 0.0f });
    const std::optional<Vec3> picked =
        preparer.pickGroundPoint(scene, pixel, outputExtent);
    CHECK(picked.has_value());
    if (picked) {
        CHECK(std::abs(picked->x - 3.5f) < 0.05f);
        CHECK(std::abs(picked->y - 3.5f) < 0.05f);
    }
}

void testEmptySceneIsSafe()
{
    TEST("emptySceneIsSafe");
    const IsoScenePreparer preparer;
    PreparedRenderScene scene;
    preparer.prepare(RenderFrameData {}, outputExtent, scene);
    CHECK(!preparer.pickGroundPoint(scene, { 640.0f, 360.0f }, outputExtent));
}

} // namespace

int main()
{
    testPickReturnsTheProjectedWorldPosition();
    testPickIsMonotonicAcrossTheBoard();
    testPickMissesOutsideTheBoard();
    testOnlySplattableGroundIsPaintable();
    testNearestSurfaceWins();
    testPickReportsTheSurfaceHeight();
    testEditorPreviewGroundIsStillPaintable();
    testPickOnlyPlanesAreNotPaintable();
    testEmptySceneIsSafe();

    if (failures == 0) {
        std::cout << "GroundPickTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "GroundPickTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
