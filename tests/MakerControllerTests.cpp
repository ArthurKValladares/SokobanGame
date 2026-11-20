#include "TestHarness.hpp"

#include "engine/AssetManifest.hpp"
#include "engine/Input.hpp"
#include "engine/InputRouter.hpp"
#include "engine/MakerController.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/Ui.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>

namespace {

using namespace sokoban;
namespace fs = std::filesystem;

constexpr Vec2 viewportSize { 1280.0f, 720.0f };

struct TemporaryDirectory {
    fs::path root = fs::temp_directory_path() /
        ("sokoban_maker_controller_tests_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

TileType tileAt(const MakerSession& session, GridPosition3 cell)
{
    return charToTileType(session.editor()
                              .documentLayers()
                              .at(static_cast<std::size_t>(cell.z))
                              .at(static_cast<std::size_t>(cell.y))
                              .at(static_cast<std::size_t>(cell.x)))
        .value();
}

// A host with no window and no renderer. The scene is a flat grid of square
// cells whose origin is the top-left corner of the board viewport, which is
// enough for the controller to pick, project and paint.
struct FakeHost final : MakerHost {
    static constexpr float cellSize = 24.0f;

    MakerController* maker = nullptr;
    bool sceneReady = true;
    bool options = false;
    int backRequests = 0;
    int textBlurs = 0;
    int playtestsConfigured = 0;
    int assetRequests = 0;
    int atmospheresSet = 0;
    int atmospheresRestored = 0;

    [[nodiscard]] static Vec2 cellCenter(UiRect scene, GridPosition cell)
    {
        return scene.position +
            Vec2 {
                (static_cast<float>(cell.x) + 0.5f) * cellSize,
                (static_cast<float>(cell.y) + 0.5f) * cellSize,
            };
    }

    std::optional<MakerSceneCamera> sceneCamera() override
    {
        if (!sceneReady) return std::nullopt;
        return MakerSceneCamera { .forward = { 0.0f, 0.0f, -1.0f },
                                  .right = { 1.0f, 0.0f, 0.0f },
                                  .valid = true };
    }
    std::optional<GridPosition3> pickCell(Vec2 position, UiRect scene) override
    {
        const auto& editor = maker->session().editor();
        const int x = static_cast<int>(
            std::floor((position.x - scene.position.x) / cellSize));
        const int y = static_cast<int>(
            std::floor((position.y - scene.position.y) / cellSize));
        if (!sceneReady || x < 0 || y < 0 ||
            std::cmp_greater_equal(x, editor.documentWidth()) ||
            std::cmp_greater_equal(y, editor.documentHeight())) {
            return std::nullopt;
        }
        return GridPosition3 { x, y, static_cast<int>(editor.activeLayer()) };
    }
    std::optional<GridPosition3> pickPortalCell(Vec2, UiRect) override
    {
        return std::nullopt;
    }
    std::optional<GridPosition3> pickCellOnPlane(
        Vec2 position,
        UiRect scene,
        int layer) override
    {
        auto cell = pickCell(position, scene);
        if (cell) cell->z = layer;
        return cell;
    }
    std::optional<Vec2> projectToScene(Vec3 worldPoint, UiRect scene) override
    {
        if (!sceneReady) return std::nullopt;
        return scene.position +
            Vec2 { worldPoint.x * cellSize, worldPoint.y * cellSize };
    }
    std::optional<UiRect> primaryPlayerBounds() override
    {
        return std::nullopt;
    }
    bool boardAssetsLoading() override { return false; }
    RenderTexture tileThumbnail(TileType) override { return {}; }

    SDL_Window* window() override { return nullptr; }
    Vec2 windowSize() override { return viewportSize; }
    Vec2 windowPixelSize() override { return viewportSize; }
    float displayScale() override { return 1.0f; }

    bool optionsOpen() override { return options; }
    void requestBack() override { ++backRequests; }
    bool textInputFocused() override
    {
        return maker->editing() && maker->textFocused();
    }
    void blurTextInput() override
    {
        ++textBlurs;
        maker->blurTextFields();
    }
    void refreshTextInput() override { }

    void configurePlaytest(GameplaySession&, GameplayPresentation&) override
    {
        ++playtestsConfigured;
    }
    void ensureLevelAssets(const Level&) override { ++assetRequests; }
    void setAtmosphere(const Level&) override { ++atmospheresSet; }
    void restoreAtmosphere() override { ++atmospheresRestored; }
    void drawLectern(LecternDialog&, const Level&, GridPosition3, Vec2) override
    {
    }
};

// The pieces Application wires together, without the window and the GPU.
struct Fixture {
    explicit Fixture(const FontAtlas& font)
        : ui(font)
        , maker(host, ui, input, manifest, settings, directory.root)
    {
        host.maker = &maker;
        maker.open();
        // The first frame lays the workspace out; picking needs a scene
        // that was already drawn with the same layout.
        frame({});
    }

    // One UI frame around the workspace, as Application::drawUiFrame runs it.
    void frame(
        const InputRouter::Frame& routed,
        UiPointerInput pointer = {},
        std::optional<Vec2> wheelPosition = std::nullopt)
    {
        ui.beginFrame(viewportSize, pointer);
        maker.drawWorkspace(viewportSize, routed, wheelPosition);
        ui.endFrame();
    }

    // A press and a release on one board cell, two frames apart.
    void click(GridPosition cell)
    {
        const Vec2 position = FakeHost::cellCenter(scene(), cell);
        InputRouter::Frame routed;
        routed.makerEditor.primaryPressed = true;
        routed.makerEditor.primaryDown = true;
        frame(
            routed,
            { .position = position, .down = true, .pressedAt = position });
        frame({}, { .position = position, .releasedAt = position });
        frame({}, { .position = position });
    }

    [[nodiscard]] UiRect scene() const { return *ui.drawData().sceneViewport; }

    TemporaryDirectory directory;
    AssetManifest manifest;
    PresentationSettings settings;
    InputState input { false };
    FakeHost host;
    UiContext ui;
    MakerController maker;
};

void testOpeningCreatesAnEditableSession(const FontAtlas& font)
{
    TEST("openingCreatesAnEditableSession");
    Fixture fixture(font);
    auto& maker = fixture.maker;
    CHECK(maker.isOpen() && maker.editing() && maker.hasSession());
    CHECK(
        !maker.playing() && !maker.dialogOpen() && !maker.boardCursorFocused());
    CHECK(!maker.session().overworld());
    CHECK(fixture.ui.drawData().sceneViewport.has_value());
    CHECK(!fixture.ui.drawData().commands.empty());
}

void testPointerPaintsThePickedCell(const FontAtlas& font)
{
    TEST("pointerPaintsThePickedCell");
    Fixture fixture(font);
    auto& maker = fixture.maker;
    maker.session().selectTile(TileType::Wall);
    const GridPosition3 target {
        6, 5, static_cast<int>(maker.session().editor().activeLayer())
    };
    CHECK(tileAt(maker.session(), target) == TileType::Air);
    CHECK(!maker.session().hasUnsavedChanges());

    fixture.click({ target.x, target.y });
    CHECK(tileAt(maker.session(), target) == TileType::Wall);
    CHECK(maker.session().hasUnsavedChanges());
    CHECK(!maker.session().strokeActive());

    // No scene on screen means nothing can be picked, so a click is inert.
    fixture.host.sceneReady = false;
    fixture.click({ 7, 5 });
    CHECK(tileAt(maker.session(), { 7, 5, target.z }) == TileType::Air);
}

void testEditsReachTheRecoveryJournal(const FontAtlas& font)
{
    TEST("editsReachTheRecoveryJournal");
    Fixture fixture(font);
    auto& maker = fixture.maker;
    const fs::path journal =
        fixture.directory.root / "maker-recovery" / "puzzle.autosave.json";
    maker.updateAutosave();
    CHECK(!fs::exists(journal));

    maker.session().selectTile(TileType::Wall);
    fixture.click({ 6, 5 });
    // The write happens on the autosave writer's thread.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!fs::exists(journal) &&
           std::chrono::steady_clock::now() < deadline) {
        maker.updateAutosave();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(fs::exists(journal));
}

void testBackUnwindsOneLevelAtATime(const FontAtlas& font)
{
    TEST("backUnwindsOneLevelAtATime");
    Fixture fixture(font);
    auto& maker = fixture.maker;

    // Nothing to unwind: the shell gets the press.
    CHECK(!maker.handleBack());

    InputRouter::Frame save;
    save.makerEditor.savePressed = true;
    fixture.frame(save);
    CHECK(maker.dialogOpen());
    CHECK(maker.handleBack());
    // The dialog closes when it next draws.
    fixture.frame({});
    CHECK(!maker.dialogOpen());

    InputRouter::Frame focus;
    focus.makerBoard.togglePressed = true;
    fixture.frame(focus);
    CHECK(maker.boardCursorFocused());
    CHECK(maker.handleBack());
    CHECK(!maker.boardCursorFocused());

    fixture.frame({});
    maker.session().setTool(MakerSession::Tool::SelectArea);
    CHECK(maker.handleBack());
    CHECK(maker.session().tool() == MakerSession::Tool::Paint);
    CHECK(!maker.handleBack());

    // With the options menu open the press belongs to the menu.
    fixture.frame(focus);
    CHECK(maker.boardCursorFocused());
    fixture.host.options = true;
    CHECK(!maker.handleBack());
    CHECK(maker.boardCursorFocused());
    CHECK(fixture.host.backRequests == 0);
}

void testBoardCursorMovesAndUsesTheTool(const FontAtlas& font)
{
    TEST("boardCursorMovesAndUsesTheTool");
    Fixture fixture(font);
    auto& maker = fixture.maker;
    const auto& editor = maker.session().editor();
    maker.session().selectTile(TileType::Wall);
    const int layer = static_cast<int>(editor.activeLayer());
    const GridPosition center {
        static_cast<int>(editor.documentWidth() / 2),
        static_cast<int>(editor.documentHeight() / 2),
    };

    InputRouter::Frame focus;
    focus.makerBoard.togglePressed = true;
    fixture.frame(focus);
    CHECK(maker.boardCursorFocused());

    // The fake camera's right vector is +x, so Right moves one column over.
    InputRouter::Frame right;
    right.makerBoard.direction = { 1, 0 };
    fixture.frame(right);
    fixture.frame({});
    const GridPosition3 target { center.x + 1, center.y, layer };
    CHECK(tileAt(maker.session(), target) == TileType::Air);

    InputRouter::Frame activate;
    activate.makerBoard.activatePressed = true;
    fixture.frame(activate);
    CHECK(tileAt(maker.session(), target) == TileType::Wall);
    CHECK(
        tileAt(maker.session(), { center.x, center.y, layer }) ==
        TileType::Air);
    CHECK(maker.boardCursorFocused());
}

void testTestPlayBorrowsTheHostAndReturns(const FontAtlas& font)
{
    TEST("testPlayBorrowsTheHostAndReturns");
    Fixture fixture(font);
    auto& maker = fixture.maker;
    auto& host = fixture.host;

    InputRouter::Frame play;
    play.toggleMakerPlaytestPressed = true;
    fixture.frame(play);
    CHECK(maker.playing() && !maker.editing());
    CHECK(maker.playtest() != nullptr);
    CHECK(
        host.playtestsConfigured == 1 && host.assetRequests == 1 &&
        host.atmospheresSet == 1);
    if (maker.playtest()) {
        const auto& board = maker.playtest()->activeBoard();
        CHECK(board.level.width() == maker.session().editor().documentWidth());
        CHECK(!board.solved);
    }

    // Back leaves Test Play before it touches the editing state.
    CHECK(maker.handleBack());
    CHECK(!maker.playing() && maker.editing());
    CHECK(host.atmospheresRestored == 1);

    maker.close();
    CHECK(!maker.isOpen());
    CHECK(host.textBlurs > 0);
}

void testEditFrameShowsTheBoardBeingEdited(const FontAtlas& font)
{
    TEST("editFrameShowsTheBoardBeingEdited");
    Fixture fixture(font);
    auto& maker = fixture.maker;
    FrameArena arena("maker controller test frame", 32U * 1024U * 1024U);
    GroundGeometryCache groundGeometry;
    const MakerEditFrameEnvironment environment {
        .selectorState =
            [](LevelLocation) { return ScreenSelectorViewState {}; },
        .groundGeometryCache = &groundGeometry,
    };
    const RenderFrameData frame = maker.buildEditFrame({}, arena, environment);
    const auto& editor = maker.session().editor();
    CHECK(frame.levelWidth == editor.documentWidth());
    CHECK(frame.levelHeight == editor.documentHeight());
    // A puzzle is framed with a one-cell margin around the board.
    CHECK(frame.cameraExtent.has_value());
    if (frame.cameraExtent) {
        CHECK(
            frame.cameraExtent->originX == -1 &&
            frame.cameraExtent->originY == -1);
        CHECK(frame.cameraExtent->width == editor.documentWidth() + 2);
        CHECK(frame.cameraExtent->height == editor.documentHeight() + 2);
    }
}

void testViewportPanLimit()
{
    TEST("viewportPanLimit");
    using Footprint = MakerViewportCamera::Footprint;
    // The inner rectangle of this viewport runs from (220, 110) to (980, 490).
    const UiRect viewport { { 100.0f, 50.0f }, { 1000.0f, 500.0f } };
    const Footprint board { Vec2 { 300.0f, 200.0f },
                            Vec2 { 700.0f, 200.0f },
                            Vec2 { 700.0f, 400.0f },
                            Vec2 { 300.0f, 400.0f } };
    const auto limit = [&](Vec2 delta) {
        return MakerViewportCamera::limitPan(delta, board, viewport);
    };
    const auto same = [](Vec2 left, Vec2 right) {
        return std::abs(left.x - right.x) < 0.01f &&
            std::abs(left.y - right.y) < 0.01f;
    };
    // A pan that leaves the board reaching the inner rectangle is not
    // touched.
    CHECK(same(limit({ 150.0f, -80.0f }), { 150.0f, -80.0f }));
    CHECK(same(limit({}), {}));
    // Past that, the board stops with its edge on the inner rectangle.
    CHECK(same(limit({ -5000.0f, 0.0f }), { 220.0f - 700.0f, 0.0f }));
    CHECK(same(limit({ 5000.0f, 0.0f }), { 980.0f - 300.0f, 0.0f }));
    CHECK(same(limit({ 0.0f, -5000.0f }), { 0.0f, 110.0f - 400.0f }));
    CHECK(same(limit({ 0.0f, 5000.0f }), { 0.0f, 490.0f - 200.0f }));
    CHECK(same(
        limit({ -5000.0f, -5000.0f }), { 220.0f - 700.0f, 110.0f - 400.0f }));
    // A board that is already out of reach comes back on the next gesture.
    Footprint lost = board;
    for (Vec2& corner : lost)
        corner.x -= 1200.0f;
    CHECK(same(
        MakerViewportCamera::limitPan({ -10.0f, 0.0f }, lost, viewport),
        { 220.0f + 500.0f, 0.0f }));

    // A board seen at an angle is a diamond, and the corners of its bounding
    // box are empty. Dragged up and to the left, it must stop with its lower
    // right edge on the corner of the inner rectangle, not with only an empty
    // corner of the box in view.
    const Footprint diamond { Vec2 { 300.0f, 300.0f },
                              Vec2 { 500.0f, 100.0f },
                              Vec2 { 700.0f, 300.0f },
                              Vec2 { 500.0f, 500.0f } };
    const Vec2 limited = MakerViewportCamera::limitPan(
        { -5000.0f, -5000.0f }, diamond, viewport);
    // The edge from (700, 300) to (500, 500) is x + y = 1000, and the corner
    // it has to reach is (220, 110).
    CHECK(std::abs(1000.0f + limited.x + limited.y - 330.0f) < 0.01f);
    // The shortest way back to that corner is square to the edge.
    CHECK(std::abs(limited.x - limited.y) < 0.01f);
    // The bounding box alone would have stopped 100 px later on each axis.
    CHECK(same(limited, { 220.0f - 700.0f + 145.0f, 110.0f - 500.0f + 55.0f }));

    // A footprint that is not finite limits nothing.
    Footprint broken = board;
    broken[2].x = std::numeric_limits<float>::quiet_NaN();
    CHECK(
        MakerViewportCamera::limitPan({ -5000.0f, 0.0f }, broken, viewport).x ==
        -5000.0f);

    // zoom() reports what it did, so a caller can tell where things went.
    MakerViewportCamera camera;
    CHECK(
        std::abs(camera.zoom(1.0f, { 600.0f, 300.0f }, viewport) - 1.2f) <
        0.0001f);
    CHECK(camera.zoom(100.0f, { 600.0f, 300.0f }, viewport) > 1.0f);
    CHECK(camera.zoom(1.0f, { 600.0f, 300.0f }, viewport) == 1.0f);
}

void testPanningAndZoomingKeepTheBoardOnScreen(const FontAtlas& font)
{
    TEST("panningAndZoomingKeepTheBoardOnScreen");
    FrameArena arena("maker controller test frame", 32U * 1024U * 1024U);
    GroundGeometryCache groundGeometry;
    const MakerEditFrameEnvironment environment {
        .selectorState =
            [](LevelLocation) { return ScreenSelectorViewState {}; },
        .groundGeometryCache = &groundGeometry,
    };
    // FakeHost draws the board from the scene's top-left corner at 24 px a
    // cell, whatever the camera does, so each test makes one gesture and reads
    // where the camera would put the board's far corner.
    const auto farCorner = [](const Fixture& fixture) {
        const auto& editor = fixture.maker.session().editor();
        return fixture.scene().position +
            Vec2 { static_cast<float>(editor.documentWidth()),
                   static_cast<float>(editor.documentHeight()) } *
            FakeHost::cellSize;
    };
    const auto afterCamera =
        [](const RenderFrameData& frame, UiRect scene, Vec2 pixel) {
            const auto camera = frame.viewportCamera.value_or(
                RenderFrameData::ViewportCameraTransform {});
            const Vec2 clip {
                2.0f * (pixel.x - scene.position.x) / scene.size.x - 1.0f,
                1.0f - 2.0f * (pixel.y - scene.position.y) / scene.size.y
            };
            const Vec2 moved = clip * camera.zoom + camera.clipOffset;
            return scene.position +
                Vec2 { (moved.x + 1.0f) * 0.5f * scene.size.x,
                       (1.0f - moved.y) * 0.5f * scene.size.y };
        };
    const auto near = [](float left, float right) {
        return std::abs(left - right) < 0.05f;
    };

    {
        Fixture fixture(font);
        auto& maker = fixture.maker;
        (void)maker.buildEditFrame({}, arena, environment);
        const UiRect scene = fixture.scene();
        const Vec2 inset = scene.size * MakerViewportCamera::keepInViewFraction;
        // A middle-button drag from the bottom-right of the scene to its
        // top-left corner: far more than the board is wide or tall.
        const Vec2 start = scene.position + scene.size * 0.95f;
        const Vec2 end = scene.position + scene.size * 0.02f;
        InputRouter::Frame press;
        press.pointer.position = start;
        press.pointer.middleDown = true;
        press.pointer.middlePressedAt = start;
        fixture.frame(press, { .position = start });
        InputRouter::Frame drag;
        drag.pointer.position = end;
        drag.pointer.middleDown = true;
        fixture.frame(drag, { .position = end });
        InputRouter::Frame release;
        release.pointer.position = end;
        release.pointer.middleReleasedAt = end;
        fixture.frame(release, { .position = end });

        const RenderFrameData frame =
            maker.buildEditFrame({}, arena, environment);
        CHECK(frame.viewportCamera.has_value());
        const Vec2 corner = afterCamera(frame, scene, farCorner(fixture));
        // Unlimited, the corner would be far outside the top-left corner.
        CHECK(near(corner.x, scene.position.x + inset.x));
        CHECK(near(corner.y, scene.position.y + inset.y));
    }
    {
        Fixture fixture(font);
        auto& maker = fixture.maker;
        (void)maker.buildEditFrame({}, arena, environment);
        const UiRect scene = fixture.scene();
        const Vec2 inset = scene.size * MakerViewportCamera::keepInViewFraction;
        // Zooming all the way in on empty space at the far side of the scene
        // pushes the board away from the pointer and off the screen.
        const Vec2 pointer = scene.position + scene.size * 0.95f;
        InputRouter::Frame wheel;
        wheel.pointer.position = pointer;
        wheel.pointer.wheel = { 0.0f, 100.0f };
        wheel.pointer.wheelPosition = pointer;
        fixture.frame(wheel, { .position = pointer }, pointer);

        const RenderFrameData frame =
            maker.buildEditFrame({}, arena, environment);
        CHECK(frame.viewportCamera.has_value());
        CHECK(frame.viewportCamera && frame.viewportCamera->zoom == 8.0f);
        const Vec2 corner = afterCamera(frame, scene, farCorner(fixture));
        CHECK(near(corner.x, scene.position.x + inset.x));
        CHECK(near(corner.y, scene.position.y + inset.y));
    }
}

} // namespace

int main()
{
    const auto font = sokoban::FontAtlas::loadDefault(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR));
    testOpeningCreatesAnEditableSession(font);
    testPointerPaintsThePickedCell(font);
    testEditsReachTheRecoveryJournal(font);
    testBackUnwindsOneLevelAtATime(font);
    testBoardCursorMovesAndUsesTheTool(font);
    testTestPlayBorrowsTheHostAndReturns(font);
    testEditFrameShowsTheBoardBeingEdited(font);
    testViewportPanLimit();
    testPanningAndZoomingKeepTheBoardOnScreen(font);
    std::cout << "MakerControllerTests: " << checks - failures << " of "
              << checks << " checks passed\n";
    return failures != 0 ? 1 : 0;
}
