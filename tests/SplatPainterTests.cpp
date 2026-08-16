// Headless tests for the ground paint session: which screen a document maps
// to, loading and saving maps, stroke-level undo, and the failure modes the
// editor reports rather than silently swallowing.

#include "engine/AssetManifest.hpp"
#include "engine/SplatPainter.hpp"
#include "engine/render/ImageData.hpp"
#include "engine/render/PngWriter.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line "
                  << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path() /
              ("sokoban_painter_test_" +
                  std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count())))
    {
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

const AssetManifest& testManifest()
{
    static const AssetManifest manifest = AssetManifest::parse(R"json({
      "format": 1,
      "textures": [
        { "name": "GroundGrass", "path": "custom/textures/grass.png" },
        { "name": "GroundRock", "path": "custom/textures/rock.png" },
        { "name": "GroundSplatMap", "path": "custom/textures/splat.png" },
        { "name": "GroundSplatMap2_1",
          "path": "custom/textures/ground_splat_level2_screen1.png",
          "filter": "linear", "colorSpace": "linear" },
        { "name": "GroundSplatMapOverworld7",
          "path": "custom/textures/ground_splat_overworld_7.png",
          "filter": "linear", "colorSpace": "linear" }
      ],
      "models": [
        { "name": "Hero", "path": "h.glb", "geometry": "skinned", "role": "player" }
      ],
      "animations": [
        { "name": "Idle", "path": "a.glb", "role": "player-idle" },
        { "name": "Move", "path": "a.glb", "role": "player-move" },
        { "name": "Push", "path": "a.glb", "role": "player-push" },
        { "name": "Death", "path": "a.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" }
      ]
    })json");
    return manifest;
}

[[nodiscard]] SplatPainter::OpenRequest requestFor(
    const TemporaryDirectory& directory,
    const std::string& screenPath,
    uint32_t wide = 13,
    uint32_t high = 7)
{
    return {
        .documentPath = directory.path() / screenPath,
        .boardTilesWide = wide,
        .boardTilesHigh = high,
        .sourceAssetRoot = directory.path() / "assets",
        .runtimeAssetRoot = directory.path() / "staged",
    };
}

const SplatCanvas::Brush solidWhite {
    .radiusTiles = 1.0f,
    .hardness = 1.0f,
    .opacity = 1.0f,
    .color = SplatCanvas::BrushColor::White,
};

void testScreenPathParsing()
{
    TEST("screenPathParsing");
    const auto parse = [](const char* path) {
        return levelLocationFromScreenPath(std::filesystem::path(path));
    };

    const std::optional<LevelLocation> ok = parse("levels/level2/screen1.scr");
    CHECK(ok.has_value());
    CHECK(ok && ok->level == 2 && ok->screen == 1);

    const std::optional<LevelLocation> deep =
        parse("/a/b/c/levels/level12/screen3.scr");
    CHECK(deep && deep->level == 12 && deep->screen == 3);

    // Anything not following the convention is not a screen, and must be
    // rejected rather than guessed at.
    CHECK(!parse("levels/level2/draft.scr").has_value());
    CHECK(!parse("levels/scratch/screen1.scr").has_value());
    CHECK(!parse("levels/level2/screen1.txt").has_value());
    CHECK(!parse("levels/levelX/screen1.scr").has_value());
    CHECK(!parse("levels/level2/screen.scr").has_value());
    CHECK(!parse("screen1.scr").has_value());
    CHECK(!parse("").has_value());
    // A negative index would index a texture name that cannot exist.
    CHECK(!parse("levels/level-1/screen0.scr").has_value());
}

void testOpenRejectsDocumentsThatAreNotScreens()
{
    TEST("openRejectsDocumentsThatAreNotScreens");
    const TemporaryDirectory directory;
    SplatPainter painter;

    CHECK(!painter.open(requestFor(directory, "scratch.scr"), testManifest()));
    CHECK(!painter.active());
    CHECK(!painter.status().empty());

    // A real screen with no board is equally unpaintable.
    CHECK(!painter.open(
        requestFor(directory, "level2/screen1.scr", 0, 0), testManifest()));
    CHECK(!painter.active());
}

void testOpenReportsAMissingManifestEntry()
{
    TEST("openReportsAMissingManifestEntry");
    const TemporaryDirectory directory;
    SplatPainter painter;

    // level0/screen0 exists as a document but the fixture manifest only
    // declares a map for 2_1. Painting it would edit a file nothing loads, so
    // this must fail loudly - it is exactly the "added a screen, forgot the
    // manifest entry" mistake.
    CHECK(!painter.open(
        requestFor(directory, "level0/screen0.scr"), testManifest()));
    CHECK(!painter.active());
    CHECK(painter.status().find("GroundSplatMap0_0") != std::string::npos);
}

void testOpenCreatesABlankMapWhenTheFileIsMissing()
{
    TEST("openCreatesABlankMapWhenTheFileIsMissing");
    const TemporaryDirectory directory;
    SplatPainter painter;

    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(painter.active());
    CHECK(painter.location().has_value());
    CHECK(painter.location()->level == 2);
    CHECK(painter.location()->screen == 1);
    CHECK(!painter.dirty());
    CHECK(painter.canvas().width() == 13 * SplatCanvas::texelsPerTile);
    CHECK(painter.canvas().height() == 7 * SplatCanvas::texelsPerTile);
    CHECK(painter.canvas().weightAt(0, 0) == 0);
}

void testOpenUsesStableOverworldTextureIdentity()
{
    TEST("openUsesStableOverworldTextureIdentity");
    const TemporaryDirectory directory;
    SplatPainter painter;
    SplatPainter::OpenRequest request =
        requestFor(directory, "overworld/screen7.scr", 9, 7);
    request.textureName = "GroundSplatMapOverworld7";

    CHECK(painter.open(request, testManifest()));
    CHECK(painter.active());
    CHECK(!painter.location().has_value());
    CHECK(painter.documentPath() == request.documentPath);
    CHECK(painter.canvas().width() == 9 * SplatCanvas::texelsPerTile);
    painter.brush() = solidWhite;
    CHECK(painter.beginStroke({ 4.0f, 3.0f }));
    painter.endStroke();
    CHECK(painter.save());
    CHECK(std::filesystem::exists(
        directory.path() / "assets/custom/textures/ground_splat_overworld_7.png"));
}

void testOpenLoadsAnExistingMap()
{
    TEST("openLoadsAnExistingMap");
    const TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / "assets" /
        "custom" / "textures" / "ground_splat_level2_screen1.png";
    std::filesystem::create_directories(file.parent_path());

    const uint32_t width = 13 * SplatCanvas::texelsPerTile;
    const uint32_t height = 7 * SplatCanvas::texelsPerTile;
    writeGrayscalePng(file, width, height,
        std::vector<uint8_t>(
            static_cast<std::size_t>(width) * height, 77));

    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(painter.canvas().weightAt(5, 5) == 77);
    CHECK(!painter.dirty());
}

void testStrokesPaintAndMarkDirty()
{
    TEST("strokesPaintAndMarkDirty");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;

    const uint64_t before = painter.revision();
    CHECK(painter.beginStroke({ 6.0f, 3.0f }));
    CHECK(painter.strokeInProgress());
    CHECK(painter.dirty());
    CHECK(painter.revision() > before);
    CHECK(painter.canvas().weightAt(
        6 * SplatCanvas::texelsPerTile, 3 * SplatCanvas::texelsPerTile) == 255);

    // Dragging fills the gap between samples.
    CHECK(painter.paintTo({ 9.0f, 3.0f }));
    CHECK(painter.canvas().weightAt(
        8 * SplatCanvas::texelsPerTile, 3 * SplatCanvas::texelsPerTile) == 255);

    painter.endStroke();
    CHECK(!painter.strokeInProgress());
    CHECK(painter.undoDepth() == 1);
}

void testUndoRevertsWholeStrokesNotSamples()
{
    TEST("undoRevertsWholeStrokesNotSamples");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;

    // One stroke made of many samples must undo as one step - that is the
    // whole point of tying undo to press/release rather than to stamps.
    painter.beginStroke({ 2.0f, 3.0f });
    painter.paintTo({ 4.0f, 3.0f });
    painter.paintTo({ 6.0f, 3.0f });
    painter.paintTo({ 8.0f, 3.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 1);

    painter.beginStroke({ 2.0f, 5.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 2);

    // Undo the second stroke: the first survives.
    CHECK(painter.undo());
    CHECK(painter.canvas().weightAt(
        2 * SplatCanvas::texelsPerTile, 5 * SplatCanvas::texelsPerTile) == 0);
    CHECK(painter.canvas().weightAt(
        6 * SplatCanvas::texelsPerTile, 3 * SplatCanvas::texelsPerTile) == 255);

    // Undo the first: back to blank.
    CHECK(painter.undo());
    CHECK(painter.canvas().weightAt(
        6 * SplatCanvas::texelsPerTile, 3 * SplatCanvas::texelsPerTile) == 0);
    CHECK(painter.undoDepth() == 0);
    CHECK(!painter.undo());
}

void testNoOpStrokesLeaveNoUndoStep()
{
    TEST("noOpStrokesLeaveNoUndoStep");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;

    // Painting well off the board changes nothing. Recording an undo step for
    // it would make Ctrl+Z appear broken.
    painter.beginStroke({ -80.0f, -80.0f });
    painter.paintTo({ -70.0f, -70.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 0);
    CHECK(!painter.dirty());

    // A stroke that does land records exactly one step.
    painter.beginStroke({ 6.0f, 3.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 1);

    // Repeating it leaves the fully covered interior alone - that part is
    // already saturated. The anti-aliased rim keeps creeping toward the target
    // across strokes, which is ordinary paint build-up rather than a bug, so
    // this is deliberately not asserted to be a whole-stamp no-op.
    const std::vector<uint8_t> before = painter.canvas().snapshot();
    painter.beginStroke({ 6.0f, 3.0f });
    painter.endStroke();
    const uint32_t centre = 6 * SplatCanvas::texelsPerTile;
    const uint32_t row = 3 * SplatCanvas::texelsPerTile;
    CHECK(painter.canvas().weightAt(centre, row) == 255);
    CHECK(before[static_cast<std::size_t>(row) * painter.canvas().width() +
              centre] == 255);
    // Ground well outside the brush is untouched by either stroke.
    CHECK(painter.canvas().weightAt(0, 0) == 0);
}

void testUndoHistoryIsCapped()
{
    TEST("undoHistoryIsCapped");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;
    painter.brush().radiusTiles = 0.2f;

    // Each entry is a full canvas copy, so history must not grow without
    // bound over a long editing session.
    for (int i = 0; i < static_cast<int>(SplatPainter::maxUndoSteps) + 10; ++i) {
        painter.beginStroke(
            { 1.0f + static_cast<float>(i) * 0.25f, 3.0f });
        painter.endStroke();
    }
    CHECK(painter.undoDepth() == SplatPainter::maxUndoSteps);
}

void testInterruptedStrokeStillRecordsUndo()
{
    TEST("interruptedStrokeStillRecordsUndo");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;

    // A second press without a release (focus lost mid-drag) must close the
    // first stroke rather than discard its undo entry.
    painter.beginStroke({ 3.0f, 3.0f });
    painter.beginStroke({ 8.0f, 5.0f });
    CHECK(painter.undoDepth() == 1);
    painter.endStroke();
    CHECK(painter.undoDepth() == 2);
}

void testPaintingRequiresAnOpenSessionAndAStroke()
{
    TEST("paintingRequiresAnOpenSessionAndAStroke");
    SplatPainter painter;
    CHECK(!painter.beginStroke({ 1.0f, 1.0f }));
    CHECK(!painter.paintTo({ 1.0f, 1.0f }));
    CHECK(!painter.undo());
    CHECK(!painter.save());
    painter.endStroke(); // must not crash

    const TemporaryDirectory directory;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    // Dragging without having pressed paints nothing.
    CHECK(!painter.paintTo({ 6.0f, 3.0f }));
    CHECK(!painter.dirty());
}

void testSaveWritesBothTrees()
{
    TEST("saveWritesBothTrees");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;
    painter.beginStroke({ 6.0f, 3.0f });
    painter.endStroke();
    CHECK(painter.dirty());

    CHECK(painter.save());
    CHECK(!painter.dirty());

    const std::filesystem::path relative =
        std::filesystem::path("custom") / "textures" /
        "ground_splat_level2_screen1.png";
    const std::filesystem::path source =
        directory.path() / "assets" / relative;
    const std::filesystem::path staged =
        directory.path() / "staged" / relative;
    CHECK(std::filesystem::exists(source));
    // The staged copy is what the running game reads, so a painted map has to
    // survive a restart without re-running the content pipeline.
    CHECK(std::filesystem::exists(staged));

    // Reopening reads back exactly what was painted.
    SplatPainter reopened;
    CHECK(reopened.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(reopened.canvas().weights() == painter.canvas().weights());
}

void testOpenResizesAMapThatNoLongerMatchesTheBoard()
{
    TEST("openResizesAMapThatNoLongerMatchesTheBoard");
    const TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / "assets" /
        "custom" / "textures" / "ground_splat_level2_screen1.png";
    std::filesystem::create_directories(file.parent_path());

    // A map sized for a 4x4 board, opened against a 13x7 one - exactly what a
    // board resize in the editor leaves behind. The shader derives coverage
    // from the texture's dimensions, so leaving it alone means the map covers
    // only the first 4x4 tiles and the rest repeats the clamped edge.
    const uint32_t width = 4 * SplatCanvas::texelsPerTile;
    const uint32_t height = 4 * SplatCanvas::texelsPerTile;
    writeGrayscalePng(file, width, height,
        std::vector<uint8_t>(static_cast<std::size_t>(width) * height, 200));

    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(painter.active());
    CHECK(painter.canvas().width() == 13 * SplatCanvas::texelsPerTile);
    CHECK(painter.canvas().height() == 7 * SplatCanvas::texelsPerTile);
    // Existing paint survives where the boards overlap...
    CHECK(painter.canvas().weightAt(0, 0) == 200);
    // ...and the newly exposed area is base material, not smeared edge.
    CHECK(painter.canvas().weightAt(
        12 * SplatCanvas::texelsPerTile, 6 * SplatCanvas::texelsPerTile) == 0);
    // Dirty, because the on-disk map still has the old size.
    CHECK(painter.dirty());
    CHECK(painter.status().find("4x4") != std::string::npos);
    CHECK(painter.status().find("13x7") != std::string::npos);

    // Saving writes the resized map, so reopening is a clean no-op.
    CHECK(painter.save());
    SplatPainter reopened;
    CHECK(reopened.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(!reopened.dirty());
    CHECK(reopened.canvas().width() == 13 * SplatCanvas::texelsPerTile);
}

void testFollowBoardResizeDuringASession()
{
    TEST("followBoardResizeDuringASession");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;
    painter.beginStroke({ 2.0f, 2.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 1);
    const uint64_t before = painter.revision();

    // Resizing the board while painting must take the map with it.
    CHECK(painter.followBoardResize(20, 12));
    CHECK(painter.canvas().width() == 20 * SplatCanvas::texelsPerTile);
    CHECK(painter.canvas().height() == 12 * SplatCanvas::texelsPerTile);
    CHECK(painter.revision() > before);
    CHECK(painter.dirty());
    // Paint inside the old extent survives.
    CHECK(painter.canvas().weightAt(
        2 * SplatCanvas::texelsPerTile, 2 * SplatCanvas::texelsPerTile) == 255);
    // Undo history was sized for the old canvas, so it is dropped rather than
    // left to fail silently on restore.
    CHECK(painter.undoDepth() == 0);
    CHECK(!painter.undo());

    // Painting the newly exposed area works.
    CHECK(painter.beginStroke({ 18.0f, 10.0f }));
    painter.endStroke();
    CHECK(painter.canvas().weightAt(
        18 * SplatCanvas::texelsPerTile,
        10 * SplatCanvas::texelsPerTile) == 255);

    // No-ops: same size, degenerate size, and no open session.
    CHECK(!painter.followBoardResize(20, 12));
    CHECK(!painter.followBoardResize(0, 5));
    SplatPainter closed;
    CHECK(!closed.followBoardResize(4, 4));
}

void testCreateBlankSplatMapWritesBothTrees()
{
    TEST("createBlankSplatMapWritesBothTrees");
    const TemporaryDirectory directory;
    const std::filesystem::path source = directory.path() / "assets";
    const std::filesystem::path staged = directory.path() / "staged";

    const CreatedSplatMap created = createBlankSplatMap(
        { .level = 3, .screen = 3 }, 10, 6, source, staged);
    CHECK(created.created);
    CHECK(created.relativePath ==
        "custom/textures/ground_splat_level3_screen3.png");
    CHECK(std::filesystem::exists(source / created.relativePath));
    // The staged copy is what the running game reads, so a map created in the
    // editor has to appear there too or it would not load until the content
    // pipeline reran.
    CHECK(std::filesystem::exists(staged / created.relativePath));

    // Board-sized at the shared density, and blank (all base material), which
    // is the predictable starting point for painting.
    const ImageData image = loadRgbaImage(source / created.relativePath);
    CHECK(image.width == 10 * SplatCanvas::texelsPerTile);
    CHECK(image.height == 6 * SplatCanvas::texelsPerTile);
    CHECK(image.rgba[0] == static_cast<std::byte>(0));
}

void testCreateBlankSplatMapNeverOverwrites()
{
    TEST("createBlankSplatMapNeverOverwrites");
    const TemporaryDirectory directory;
    const std::filesystem::path source = directory.path() / "assets";
    const std::filesystem::path relative =
        "custom/textures/ground_splat_level3_screen3.png";
    std::filesystem::create_directories((source / relative).parent_path());

    // Stand in for a painted map: distinctive content at a different size.
    const uint32_t width = 4 * SplatCanvas::texelsPerTile;
    const uint32_t height = 4 * SplatCanvas::texelsPerTile;
    writeGrayscalePng(source / relative, width, height,
        std::vector<uint8_t>(static_cast<std::size_t>(width) * height, 199));

    // Pressing the button on a screen that already has a map must report
    // success - the caller only needs the entry to exist - without touching a
    // single byte of what is there.
    const CreatedSplatMap created = createBlankSplatMap(
        { .level = 3, .screen = 3 }, 10, 6, source, {});
    CHECK(created.created);
    CHECK(created.relativePath == relative);
    const ImageData image = loadRgbaImage(source / relative);
    CHECK(image.width == width);
    CHECK(image.height == height);
    CHECK(image.rgba[0] == static_cast<std::byte>(199));
}

void testCreateBlankSplatMapRejectsAnEmptyBoard()
{
    TEST("createBlankSplatMapRejectsAnEmptyBoard");
    const TemporaryDirectory directory;
    const CreatedSplatMap created = createBlankSplatMap(
        { .level = 0, .screen = 0 }, 0, 0, directory.path() / "assets", {});
    CHECK(!created.created);
    CHECK(!created.message.empty());
    CHECK(!std::filesystem::exists(
        directory.path() / "assets" / created.relativePath));
}

void testCreatedMapIsImmediatelyPaintable()
{
    TEST("createdMapIsImmediatelyPaintable");
    // End to end: create the map, then open it exactly as the button does
    // once the manifest entry exists. This is the workflow that previously
    // required running the Python generator and restarting.
    const TemporaryDirectory directory;
    const CreatedSplatMap created = createBlankSplatMap(
        { .level = 2, .screen = 1 },
        13,
        7,
        directory.path() / "assets",
        directory.path() / "staged");
    CHECK(created.created);

    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(painter.active());
    CHECK(!painter.dirty());
    CHECK(painter.canvas().width() == 13 * SplatCanvas::texelsPerTile);
    painter.brush() = solidWhite;
    CHECK(painter.beginStroke({ 6.0f, 3.0f }));
    painter.endStroke();
    CHECK(painter.save());
}

void testCloseResetsEverything()
{
    TEST("closeResetsEverything");
    const TemporaryDirectory directory;
    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    painter.brush() = solidWhite;
    painter.beginStroke({ 6.0f, 3.0f });
    painter.endStroke();

    painter.close();
    CHECK(!painter.active());
    CHECK(!painter.dirty());
    CHECK(!painter.location().has_value());
    CHECK(painter.undoDepth() == 0);
    CHECK(painter.texture().isNone());
    // Re-opening after a close starts clean rather than resuming the old
    // canvas, which would leak one screen's paint onto another.
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(painter.canvas().weightAt(
        6 * SplatCanvas::texelsPerTile, 3 * SplatCanvas::texelsPerTile) == 0);
}

} // namespace

int main()
{
    testScreenPathParsing();
    testOpenRejectsDocumentsThatAreNotScreens();
    testOpenReportsAMissingManifestEntry();
    testOpenCreatesABlankMapWhenTheFileIsMissing();
    testOpenUsesStableOverworldTextureIdentity();
    testOpenLoadsAnExistingMap();
    testStrokesPaintAndMarkDirty();
    testUndoRevertsWholeStrokesNotSamples();
    testNoOpStrokesLeaveNoUndoStep();
    testUndoHistoryIsCapped();
    testInterruptedStrokeStillRecordsUndo();
    testPaintingRequiresAnOpenSessionAndAStroke();
    testSaveWritesBothTrees();
    testOpenResizesAMapThatNoLongerMatchesTheBoard();
    testFollowBoardResizeDuringASession();
    testCreateBlankSplatMapWritesBothTrees();
    testCreateBlankSplatMapNeverOverwrites();
    testCreateBlankSplatMapRejectsAnEmptyBoard();
    testCreatedMapIsImmediatelyPaintable();
    testCloseResetsEverything();

    if (failures == 0) {
        std::cout << "SplatPainterTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SplatPainterTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
