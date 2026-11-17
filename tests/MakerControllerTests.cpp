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
    void frame(const InputRouter::Frame& routed, UiPointerInput pointer = {})
    {
        ui.beginFrame(viewportSize, pointer);
        maker.drawWorkspace(viewportSize, routed, std::nullopt);
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
    std::cout << "MakerControllerTests: " << checks - failures << " of "
              << checks << " checks passed\n";
    return failures != 0 ? 1 : 0;
}
