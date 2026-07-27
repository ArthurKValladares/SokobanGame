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

void testOpenWarnsWhenTheMapDoesNotMatchTheBoard()
{
    TEST("openWarnsWhenTheMapDoesNotMatchTheBoard");
    const TemporaryDirectory directory;
    const std::filesystem::path file = directory.path() / "assets" /
        "custom" / "textures" / "ground_splat_level2_screen1.png";
    std::filesystem::create_directories(file.parent_path());

    // A map sized for a 4x4 board, opened against a 13x7 one - what happens
    // after a board is resized. The shader would map it across the whole
    // board, so strokes land scaled; the editor should say so and still let
    // you work.
    const uint32_t width = 4 * SplatCanvas::texelsPerTile;
    const uint32_t height = 4 * SplatCanvas::texelsPerTile;
    writeGrayscalePng(file, width, height,
        std::vector<uint8_t>(static_cast<std::size_t>(width) * height, 5));

    SplatPainter painter;
    CHECK(painter.open(
        requestFor(directory, "level2/screen1.scr"), testManifest()));
    CHECK(painter.active());
    CHECK(painter.status().find("4x4") != std::string::npos);
    CHECK(painter.status().find("13x7") != std::string::npos);
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

    // Painting white over already-white ground is likewise a no-op.
    painter.beginStroke({ 6.0f, 3.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 1);
    painter.beginStroke({ 6.0f, 3.0f });
    painter.endStroke();
    CHECK(painter.undoDepth() == 1);
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
    testOpenLoadsAnExistingMap();
    testOpenWarnsWhenTheMapDoesNotMatchTheBoard();
    testStrokesPaintAndMarkDirty();
    testUndoRevertsWholeStrokesNotSamples();
    testNoOpStrokesLeaveNoUndoStep();
    testUndoHistoryIsCapped();
    testInterruptedStrokeStillRecordsUndo();
    testPaintingRequiresAnOpenSessionAndAStroke();
    testSaveWritesBothTrees();
    testCloseResetsEverything();

    if (failures == 0) {
        std::cout << "SplatPainterTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SplatPainterTests: "
              << failures << " of " << checks << " checks failed\n";
    return 1;
}
