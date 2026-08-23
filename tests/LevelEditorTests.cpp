// Headless tests for editor document commands and project filesystem behavior.
// No SDL, Vulkan, ImGui, rendering, or window dependencies.

#include "engine/LevelEditor.hpp"
#include "engine/OverworldMapEditor.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
        std::cerr << "FAIL [" << currentTest << "] line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

struct TemporaryProject {
    TemporaryProject()
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
            ("sokoban_level_editor_tests_" + std::to_string(unique));
        source = root / "source";
        runtime = root / "runtime";
        std::filesystem::create_directories(source);
        std::filesystem::create_directories(runtime);
    }

    ~TemporaryProject()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::filesystem::path source;
    std::filesystem::path runtime;
};

LevelEditor makeEditor(const TemporaryProject& project)
{
    LevelEditor editor;
    editor.initialize(project.source, project.runtime, 0, 0);
    return editor;
}

using TreeSnapshot =
    std::vector<std::pair<std::filesystem::path, std::string>>;

TreeSnapshot snapshotTree(const std::filesystem::path& root)
{
    TreeSnapshot snapshot;
    if (!std::filesystem::exists(root)) {
        return snapshot;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        const std::filesystem::path relative =
            entry.path().lexically_relative(root);
        if (entry.is_directory()) {
            snapshot.emplace_back(relative, "<directory>");
            continue;
        }

        std::ifstream file(entry.path(), std::ios::binary);
        snapshot.emplace_back(
            relative,
            std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>()));
    }
    std::ranges::sort(snapshot, {}, &TreeSnapshot::value_type::first);
    return snapshot;
}

void testDocumentCommandsAndUndo()
{
    TEST("documentCommandsAndUndo");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    editor.newDocument(4, 3, false);
    CHECK(editor.documentWidth() == 4);
    CHECK(editor.documentHeight() == 3);
    CHECK(editor.documentDepth() == 2);
    CHECK(editor.activeLayer() == 1);
    CHECK(editor.dirty());

    editor.setSelectedTile(TileType::Wall);
    editor.paintCell({ 2, 1, 1 });
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Wall));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Air));

    editor.setSelectedTile(TileType::Decorative);
    editor.paintCell({ 3, 1, 1 });
    CHECK(editor.documentLayers()[1][1][3] ==
        tileTypeToChar(TileType::Decorative));
    CHECK(editor.documentToLevel().tileAt(3, 1, 1) ==
        TileType::Decorative);

    const std::array mirrorTypes {
        TileType::MirrorNorthWest,
        TileType::MirrorNorthEast,
        TileType::MirrorSouthWest,
        TileType::MirrorSouthEast,
    };
    for (std::size_t x = 0; x < mirrorTypes.size(); ++x) {
        editor.setSelectedTile(mirrorTypes[x]);
        editor.paintCell({ static_cast<int>(x), 0, 1 });
        CHECK(editor.documentLayers()[1][0][x] ==
            tileTypeToChar(mirrorTypes[x]));
    }

    editor.setActiveLayer(100);
    CHECK(editor.activeLayer() == editor.documentDepth() - 1);
    editor.setLayerLocked(true);
    CHECK(editor.layerLocked());

    editor.addLayerAbove();
    CHECK(editor.documentDepth() == 3);
    CHECK(editor.activeLayer() == 2);
    editor.deleteActiveLayer();
    CHECK(editor.documentDepth() == 2);
}

void testTileValidationAndPlayerUniqueness()
{
    TEST("tileValidationAndPlayerUniqueness");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);

    editor.setCell({ 2, 1, 1 }, TileType::Ladder);
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Air));
    CHECK(editor.status().find("Ladders must") != std::string::npos);

    editor.setCell({ 1, 1, 1 }, TileType::Ground);
    editor.setCell({ 2, 1, 1 }, TileType::Ladder);
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Ladder));

    editor.setCell({ 3, 2, 1 }, TileType::Player);
    int playerCount = 0;
    for (const auto& layer : editor.documentLayers()) {
        for (const std::string& row : layer) {
            playerCount += static_cast<int>(std::ranges::count(row, tileTypeToChar(TileType::Player)));
        }
    }
    CHECK(playerCount == 1);
    CHECK(editor.documentLayers()[1][2][3] == tileTypeToChar(TileType::Player));
}

void testAddLayerBelowShiftsContentAndWaterAndIsUndoable()
{
    TEST("addLayerBelowShiftsContentAndWaterAndIsUndoable");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(2, 2, false);
    editor.setActiveLayer(0);
    editor.setWaterLayer(0);

    editor.addLayerBelow();
    CHECK(editor.documentDepth() == 3);
    CHECK(editor.activeLayer() == 0);
    CHECK(editor.waterLayer() == 1U);
    CHECK(editor.documentLayers()[0][0][0] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.documentLayers()[1][0][0] ==
        tileTypeToChar(TileType::Ground));
    CHECK(editor.documentLayers()[2][0][0] ==
        tileTypeToChar(TileType::Player));
    CHECK(editor.status() == "Added layer below.");

    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentDepth() == 2);
    CHECK(editor.activeLayer() == 0);
    CHECK(editor.waterLayer() == 0U);
    CHECK(editor.documentLayers()[0][0][0] ==
        tileTypeToChar(TileType::Ground));
    CHECK(editor.documentLayers()[1][0][0] ==
        tileTypeToChar(TileType::Player));
}

void testSaveLoadAndRuntimeMirror()
{
    TEST("saveLoadAndRuntimeMirror");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(5, 4, false);
    editor.setCell({ 2, 2, 1 }, TileType::Wall);

    const std::filesystem::path sourcePath = project.source / "level0" / "screen0.scr";
    const std::filesystem::path runtimePath = project.runtime / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(sourcePath));
    CHECK(std::filesystem::exists(sourcePath));
    CHECK(std::filesystem::exists(runtimePath));
    CHECK(!editor.dirty());

    editor.eraseCell({ 2, 2, 1 });
    CHECK(editor.dirty());
    CHECK(editor.loadDocument(sourcePath));
    CHECK(editor.documentLayers()[1][2][2] == tileTypeToChar(TileType::Wall));
    CHECK(!editor.dirty());

    const std::optional<Level> draft = editor.beginDraftPlayback();
    CHECK(draft.has_value());
    CHECK(editor.playingDraft());
    CHECK(!editor.editingDocument());
}

void testSelectedPathIsSeparateFromTheLoadedDocument()
{
    TEST("selectedPathIsSeparateFromTheLoadedDocument");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    const std::filesystem::path first =
        project.source / "level0" / "screen0.scr";
    const std::filesystem::path second =
        project.source / "level0" / "screen1.scr";
    editor.newDocument(5, 4, false);
    editor.setCell({ 2, 2, 1 }, TileType::Wall);
    CHECK(editor.saveDocument(first));
    editor.newDocument(6, 5, false);
    CHECK(editor.saveDocument(second));
    CHECK(editor.loadDocument(first));

    // Baseline: a loaded document reports the file it came from.
    CHECK(editor.documentPath() == first);
    CHECK(editor.loadedDocumentPath() == first);

    // Clicking a different screen in the browser only *selects* it. The
    // document in memory is untouched, so anything derived from the document -
    // notably which screen's ground splat map belongs to it - must keep
    // pointing at the loaded file. Keying that off documentPath() made merely
    // browsing swap the rendered splat map onto the wrong screen.
    editor.selectDocument(second);
    CHECK(editor.documentPath() == second);
    CHECK(editor.loadedDocumentPath() == first);
    CHECK(editor.documentWidth() == 5);

    // Actually loading it moves both.
    CHECK(editor.loadDocument(second));
    CHECK(editor.documentPath() == second);
    CHECK(editor.loadedDocumentPath() == second);

    // A new document belongs to no file until saved, so it has no screen and
    // must not inherit the previous document's map.
    editor.newDocument(4, 4, false);
    CHECK(editor.loadedDocumentPath().empty());

    // Saving it into a screen path makes it that screen.
    CHECK(editor.saveDocument(first));
    CHECK(editor.loadedDocumentPath() == first);
}

void testOpeningScreensPreservesIndependentDraftsAndUndoHistory()
{
    TEST("openingScreensPreservesIndependentDraftsAndUndoHistory");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    const std::filesystem::path first =
        project.source / "level0" / "screen0.scr";
    const std::filesystem::path second =
        project.source / "level0" / "screen1.scr";
    editor.newDocument(5, 4, false);
    CHECK(editor.saveDocument(first));
    editor.newDocument(6, 5, false);
    CHECK(editor.saveDocument(second));

    CHECK(editor.openDocument(first));
    editor.setCell({ 2, 2, 1 }, TileType::Wall);
    CHECK(editor.dirty());
    CHECK(editor.hasInProgressDraft(first));

    CHECK(editor.openDocument(second));
    CHECK(editor.documentWidth() == 6);
    CHECK(editor.documentLayers()[1][2][2] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.hasInProgressDraft(first));
    editor.setCell({ 3, 3, 1 }, TileType::Decorative);
    CHECK(editor.hasInProgressDraft(second));

    CHECK(editor.openDocument(first));
    CHECK(editor.documentWidth() == 5);
    CHECK(editor.documentLayers()[1][2][2] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.hasInProgressDraft(second));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][2][2] ==
        tileTypeToChar(TileType::Air));

    CHECK(editor.openDocument(second));
    CHECK(editor.documentLayers()[1][3][3] ==
        tileTypeToChar(TileType::Decorative));
    CHECK(editor.saveDocument(second));
    CHECK(!editor.hasInProgressDraft(second));
}

void testUndoRestoresTheLoadedDocumentPath()
{
    TEST("undoRestoresTheLoadedDocumentPath");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    const std::filesystem::path first =
        project.source / "level0" / "screen0.scr";
    const std::filesystem::path second =
        project.source / "level0" / "screen1.scr";
    editor.newDocument(5, 4, false);
    CHECK(editor.saveDocument(first));
    editor.newDocument(6, 5, false);
    CHECK(editor.saveDocument(second));

    CHECK(editor.loadDocument(first));
    CHECK(editor.loadDocument(second));
    CHECK(editor.loadedDocumentPath() == second);

    // Undoing a load restores the previous contents, so it has to restore
    // where they came from too - otherwise the old board would be attributed
    // to the newer screen.
    CHECK(editor.tryUndoEdit());
    CHECK(editor.loadedDocumentPath() == first);
    CHECK(editor.documentWidth() == 5);
}

void testWaterLayerEditingPersistenceAndLayerRenumbering()
{
    TEST("waterLayerEditingPersistenceAndLayerRenumbering");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);

    editor.setWaterLayer(0);
    CHECK(editor.waterLayer() == 0U);
    CHECK(editor.dirty());
    CHECK(editor.documentToLevel().tileAt(3, 2, 0) == TileType::Ground);

    editor.setCell({ 3, 2, 0 }, TileType::Air);
    CHECK(editor.documentToLevel().tileAt(3, 2, 0) == TileType::Water);

    editor.setActiveLayer(0);
    editor.addLayerAbove();
    CHECK(editor.waterLayer() == 0U);
    editor.setWaterLayer(1);
    editor.setActiveLayer(0);
    editor.addLayerAbove();
    CHECK(editor.waterLayer() == 2U);
    editor.deleteActiveLayer();
    CHECK(editor.waterLayer() == 1U);
    editor.setActiveLayer(1);
    editor.deleteActiveLayer();
    CHECK(!editor.waterLayer());
    CHECK(editor.tryUndoEdit());
    CHECK(editor.waterLayer() == 1U);

    const std::filesystem::path sourcePath =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(sourcePath));
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(sourcePath));
    CHECK(loaded.waterLayer() == 1U);
    CHECK(loaded.documentToLevel().waterLayer() == 1U);
}

void testProjectRenumberDeleteAndRestore()
{
    TEST("projectRenumberDeleteAndRestore");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.setRequestedSize(4, 3);

    editor.addLevelAt(0);
    editor.addLevelAt(0);
    std::vector<LevelEditor::LevelDirectory> levels = editor.collectLevelDirectories();
    CHECK(levels.size() == 2);
    CHECK(levels[0].index == 0);
    CHECK(levels[1].index == 1);
    CHECK(std::filesystem::exists(project.runtime / "level0" / "screen0.scr"));
    CHECK(std::filesystem::exists(project.runtime / "level1" / "screen0.scr"));

    editor.renameLevel(levels[0], "Clockwork Garden");
    editor.renameScreen(levels[0], 0, "First Steps");
    levels = editor.collectLevelDirectories();
    CHECK(levels[0].name == "Clockwork Garden");
    CHECK(levels[0].screens[0].name == "First Steps");
    CHECK(std::filesystem::exists(
        project.runtime / "level0" / "metadata.json"));

    editor.addScreenAt(levels[0], 1);
    levels = editor.collectLevelDirectories();
    CHECK(levels[0].screens.size() == 2);
    CHECK(levels[0].screens[0].index == 0);
    CHECK(levels[0].screens[1].index == 1);
    CHECK(levels[0].screens[0].name == "First Steps");
    CHECK(levels[0].screens[1].name.empty());
    editor.renameScreen(levels[0], 1, "The Long Hall");
    levels = editor.collectLevelDirectories();

    editor.deleteScreen(levels[0], 0);
    levels = editor.collectLevelDirectories();
    CHECK(levels[0].screens.size() == 1);
    CHECK(levels[0].screens[0].index == 0);
    CHECK(levels[0].screens[0].name == "The Long Hall");
    CHECK(!std::filesystem::exists(levels[0].path / "screen1.scr"));

    editor.deleteLevel(levels[0]);
    levels = editor.collectLevelDirectories();
    std::vector<LevelEditor::LevelDirectory> deleted = editor.collectDeletedLevels();
    CHECK(levels.size() == 1);
    CHECK(levels[0].index == 0);
    CHECK(deleted.size() == 1);
    CHECK(deleted[0].name == "Clockwork Garden");
    CHECK(deleted[0].screens[0].name == "The Long Hall");

    editor.restoreDeletedLevel(deleted[0].path);
    levels = editor.collectLevelDirectories();
    CHECK(levels.size() == 2);
    CHECK(levels[1].index == 1);
    CHECK(levels[1].name == "Clockwork Garden");
    CHECK(editor.collectDeletedLevels().empty());

    editor.deleteLevel(levels[1]);
    deleted = editor.collectDeletedLevels();
    CHECK(deleted.size() == 1);
    const std::filesystem::path unrelated = project.root / "unrelated";
    std::filesystem::create_directories(unrelated);
    editor.restoreDeletedLevel(unrelated);
    CHECK(std::filesystem::exists(unrelated));
    CHECK(editor.collectLevelDirectories().size() == 1);
    CHECK(!editor.canPermanentlyDelete(project.source));
    CHECK(!editor.permanentlyDelete(project.source));
    CHECK(std::filesystem::exists(project.source));
    CHECK(editor.canPermanentlyDelete(deleted[0].path));
    CHECK(editor.permanentlyDelete(deleted[0].path));
    CHECK(!std::filesystem::exists(deleted[0].path));
    CHECK(!editor.permanentlyDelete(deleted[0].path));
}

void testUndoAfterNewEditDoesNotReplayAbandonedBranch()
{
    TEST("undoAfterNewEditDoesNotReplayAbandonedBranch");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);

    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    editor.setCell({ 2, 1, 1 }, TileType::Wall);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Air));

    editor.setCell({ 3, 1, 1 }, TileType::Rock);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][3] == tileTypeToChar(TileType::Air));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][1] == tileTypeToChar(TileType::Air));
    CHECK(!editor.tryUndoEdit());
}

void testResizePreservesOverlapAndUsesLayerFill()
{
    TEST("resizePreservesOverlapAndUsesLayerFill");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(2, 2, false);
    editor.setCell({ 1, 1, 1 }, TileType::Wall);

    editor.resizeDocument(4, 3, false);
    CHECK(editor.documentWidth() == 4);
    CHECK(editor.documentHeight() == 3);
    CHECK(editor.documentLayers()[1][1][1] == tileTypeToChar(TileType::Wall));
    CHECK(editor.documentLayers()[0][2][3] == tileTypeToChar(TileType::Ground));
    CHECK(editor.documentLayers()[1][2][3] == tileTypeToChar(TileType::Air));

    editor.resizeDocument(1, 1, false);
    CHECK(editor.documentWidth() == 1);
    CHECK(editor.documentHeight() == 1);
    CHECK(editor.documentLayers()[1][0][0] == tileTypeToChar(TileType::Player));
}

void testPaintingOutsideExpandsAndShiftsDocumentAtomically()
{
    TEST("paintingOutsideExpandsAndShiftsDocumentAtomically");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(2, 2, false);
    editor.setCell({ 1, 1, 1 }, TileType::Wall);

    CHECK((editor.resolveEditTarget({ -1, -1, 0 }, false, false) ==
        GridPosition3 { -1, -1, 0 }));
    CHECK((editor.resolveEditTarget({ -1, -1, 0 }, false, true) ==
        GridPosition3 { -1, -1, 0 }));
    CHECK((editor.resolveEditTarget({ -1, -1, 0 }, true, false) ==
        GridPosition3 { -1, -1, 0 }));
    CHECK((editor.resolveEditTarget({ 0, 0, 0 }, false, false) ==
        GridPosition3 { 0, 0, 2 }));
    editor.setLayerLocked(true);
    CHECK((editor.resolveEditTarget({ -1, 0, 0 }, false, false) ==
        GridPosition3 { -1, 0, 1 }));
    editor.setLayerLocked(false);

    editor.setCell({ -1, -1, 0 }, TileType::Decorative);
    CHECK(editor.documentWidth() == 3);
    CHECK(editor.documentHeight() == 3);
    CHECK(editor.requestedWidth() == 3);
    CHECK(editor.requestedHeight() == 3);
    CHECK(editor.documentLayers()[0][0][0] ==
        tileTypeToChar(TileType::Decorative));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Player));
    CHECK(editor.documentLayers()[1][2][2] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.documentLayers()[0][0][1] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.status().find("Expanded level to 3 x 3") !=
        std::string::npos);

    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentWidth() == 2);
    CHECK(editor.documentHeight() == 2);
    CHECK(editor.documentLayers()[1][0][0] ==
        tileTypeToChar(TileType::Player));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));

    editor.setCell({ 3, 2, 0 }, TileType::Decorative);
    CHECK(editor.documentWidth() == 4);
    CHECK(editor.documentHeight() == 3);
    CHECK(editor.documentLayers()[0][2][2] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.documentLayers()[1][0][0] ==
        tileTypeToChar(TileType::Player));
    CHECK(editor.documentLayers()[0][2][3] ==
        tileTypeToChar(TileType::Decorative));

    editor.eraseCell({ -1, 0, 1 });
    CHECK(editor.documentWidth() == 4);
    CHECK(editor.documentHeight() == 3);
}

void testInvalidLoadLeavesDocumentUntouched()
{
    TEST("invalidLoadLeavesDocumentUntouched");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);
    editor.setCell({ 2, 1, 1 }, TileType::Wall);
    const Level::LayerRows before = editor.documentLayers();
    const std::filesystem::path beforePath = editor.documentPath();
    const bool dirtyBefore = editor.dirty();

    const std::filesystem::path invalidPath = project.root / "invalid.scr";
    {
        std::ofstream file(invalidPath);
        file << "@layer 0\n....\n\n@layer 1\n????\n";
    }

    CHECK(!editor.loadDocument(invalidPath));
    CHECK(editor.documentLayers() == before);
    CHECK(editor.documentPath() == beforePath);
    CHECK(editor.dirty() == dirtyBefore);
    CHECK(editor.status().find("Unknown level tile") != std::string::npos);
}

void testAlternateBrowserRootDoesNotMirrorRuntime()
{
    TEST("alternateBrowserRootDoesNotMirrorRuntime");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    const std::filesystem::path alternate = project.root / "alternate";
    std::filesystem::create_directories(alternate);
    CHECK(editor.setBrowserRoot(alternate));

    editor.addLevelAt(0);
    CHECK(std::filesystem::exists(alternate / "level0" / "screen0.scr"));
    CHECK(!std::filesystem::exists(project.runtime / "level0"));

    const std::filesystem::path alternateSave = alternate / "manual.scr";
    CHECK(editor.saveDocument(alternateSave));
    CHECK(std::filesystem::exists(alternateSave));
    CHECK(!std::filesystem::exists(project.runtime / "manual.scr"));
}

void testBrowserFiltersJunkAndRejectsForeignDirectories()
{
    TEST("browserFiltersJunkAndRejectsForeignDirectories");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.addLevelAt(0);
    std::filesystem::create_directories(project.source / "levelx");
    std::filesystem::create_directories(project.source / "notes");
    {
        std::ofstream file(project.source / "level0" / "screenx.scr");
        file << "ignored";
    }

    const std::vector<LevelEditor::LevelDirectory> levels = editor.collectLevelDirectories();
    CHECK(levels.size() == 1);
    CHECK(levels[0].screens.size() == 1);

    const std::filesystem::path foreignPath = project.root / "foreign";
    std::filesystem::create_directories(foreignPath);
    const LevelEditor::LevelDirectory foreign {
        .index = 0,
        .path = foreignPath,
    };
    editor.addScreenAt(foreign, 0);
    CHECK(std::filesystem::is_empty(foreignPath));
    editor.deleteLevel(foreign);
    CHECK(std::filesystem::exists(foreignPath));

    editor.addLevelAt(-1);
    CHECK(editor.collectLevelDirectories().size() == 1);
    CHECK(editor.status().find("negative") != std::string::npos);
}

void testFailedRenumberPreservesSourceAndRuntimeTrees()
{
    TEST("failedRenumberPreservesSourceAndRuntimeTrees");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.setRequestedSize(4, 3);
    editor.addLevelAt(0);

    const std::filesystem::path invalidLevel = project.source / "level1";
    std::filesystem::create_directories(invalidLevel);
    {
        std::ofstream file(invalidLevel / "screen0.scr");
        file << "@layer 0\n....\n\n@layer 1\n????\n";
    }

    const TreeSnapshot sourceBefore = snapshotTree(project.source);
    const TreeSnapshot runtimeBefore = snapshotTree(project.runtime);

    editor.addLevelAt(0);

    CHECK(snapshotTree(project.source) == sourceBefore);
    CHECK(snapshotTree(project.runtime) == runtimeBefore);
    CHECK(std::filesystem::exists(project.source / "level0" / "screen0.scr"));
    CHECK(std::filesystem::exists(project.source / "level1" / "screen0.scr"));
    CHECK(!std::filesystem::exists(project.root / "source.editor-stage"));
    CHECK(!std::filesystem::exists(project.root / "source.editor-backup"));
    CHECK(!std::filesystem::exists(project.root / "runtime.editor-stage"));
    CHECK(!std::filesystem::exists(project.root / "runtime.editor-backup"));
    CHECK(editor.status().find("original files were preserved") !=
        std::string::npos);
}

void testDecorationEditingPersistenceAndUndo()
{
    TEST("decorationEditingPersistenceAndUndo");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(3, 2, false);
    editor.setSelectedDecorationModel("Stone");
    CHECK(editor.tool() == LevelEditor::Tool::Decorations);
    CHECK(editor.placeDecoration({ 1, 1, 1 }));
    CHECK(editor.decorations().size() == 1);
    CHECK(editor.selectedDecorationIndex() == 0U);
    CHECK(editor.decorations()[0].model == "Stone");
    CHECK(editor.decorations()[0].position.x == 1.5f);
    CHECK(editor.decorations()[0].position.y == 1.5f);
    CHECK(editor.decorations()[0].position.z == 1.0f);

    editor.cancelDecorationPlacement();
    CHECK(editor.selectedDecorationModel().empty());
    CHECK(editor.selectedDecorationIndex() == 0U);

    Level::Decoration transformed = *editor.selectedDecoration();
    transformed.position = { 0.25f, 1.75f, 2.5f };
    transformed.rotationDegrees = { 10.0f, 20.0f, 30.0f };
    transformed.scale = { 0.5f, 1.5f, 2.0f };
    transformed.pointLight = Level::Decoration::PointLight {
        .offset = { 0.0f, 0.0f, 0.75f },
        .color = { 1.0f, 0.4f, 0.1f },
        .intensity = 4.0f,
        .range = 6.5f,
        .castsShadows = true,
        .shadowBias = 0.003f,
        .shadowOpacity = 0.8f,
    };
    CHECK(editor.updateSelectedDecoration(transformed));
    CHECK(*editor.selectedDecoration() == transformed);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.selectedDecoration()->position.x == 1.5f);
    CHECK(editor.selectedDecoration()->scale.x == 1.0f);

    Level::Decoration lit = *editor.selectedDecoration();
    lit.pointLight = transformed.pointLight;
    CHECK(editor.updateSelectedDecoration(lit));

    CHECK(editor.duplicateSelectedDecoration());
    CHECK(editor.decorations().size() == 2);
    CHECK(editor.deleteSelectedDecoration());
    CHECK(editor.decorations().size() == 1);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.decorations().size() == 2);

    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.decorations().size() == 2);
    CHECK(loaded.documentToLevel().decorations().size() == 2);
    CHECK(loaded.decorations()[1].pointLight.has_value());
    CHECK(loaded.decorations()[1].pointLight->color.y == 0.4f);
    CHECK(loaded.decorations()[1].pointLight->range == 6.5f);

    const float oldX = loaded.decorations()[0].position.x;
    loaded.setSelectedTile(TileType::Wall);
    loaded.paintCell({ -1, 0, 0 });
    CHECK(loaded.decorations()[0].position.x == oldX + 1.0f);
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.decorations()[0].position.x == oldX);
}

void testDecorationTransformSessionCoalescesUndoAndCanCancel()
{
    TEST("decorationTransformSessionCoalescesUndoAndCanCancel");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(3, 2, false);
    editor.setSelectedDecorationModel("Stone");
    CHECK(editor.placeDecoration({ 1, 1, 1 }));
    const Level::Decoration original = *editor.selectedDecoration();

    CHECK(editor.beginSelectedDecorationTransform());
    CHECK(editor.transformingSelectedDecoration());
    Level::Decoration transformed = original;
    transformed.position.x += 0.5f;
    CHECK(editor.previewSelectedDecorationTransform(transformed));
    transformed.position.x += 0.75f;
    CHECK(editor.previewSelectedDecorationTransform(transformed));
    CHECK(editor.endSelectedDecorationTransform());
    CHECK(!editor.transformingSelectedDecoration());
    CHECK(editor.selectedDecoration()->position.x == transformed.position.x);

    // Both preview updates are one committed edit.
    CHECK(editor.tryUndoEdit());
    CHECK(*editor.selectedDecoration() == original);
    // The next undo is the placement itself, proving no preview record leaked.
    CHECK(editor.tryUndoEdit());
    CHECK(editor.decorations().empty());

    CHECK(editor.placeDecoration({ 1, 1, 1 }));
    const Level::Decoration beforeCancel = *editor.selectedDecoration();
    CHECK(editor.beginSelectedDecorationTransform());
    transformed = beforeCancel;
    transformed.scale = { 2.0f, 3.0f, 4.0f };
    CHECK(editor.previewSelectedDecorationTransform(transformed));
    CHECK(editor.endSelectedDecorationTransform(false));
    CHECK(*editor.selectedDecoration() == beforeCancel);
}

void testSelectorEditingPersistenceUndoAndProjectRemapping()
{
    TEST("selectorEditingPersistenceUndoAndProjectRemapping");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.setRequestedSize(4, 3);
    editor.addLevelAt(0);

    editor.newDocument(4, 3, false);
    const std::filesystem::path overworld = project.source / "overworld.scr";
    CHECK(editor.saveDocument(overworld));
    CHECK(editor.editingOverworld());
    editor.setSelectedTile(TileType::End);
    editor.paintCell({ 2, 1, 1 });
    CHECK(editor.documentToLevel().tileAt(2, 1, 1) != TileType::End);
    CHECK(editor.status().find("not allowed") != std::string::npos);
    editor.setTool(LevelEditor::Tool::Selectors);
    editor.setCell({ 3, 0, 1 }, TileType::Wall);
    CHECK(editor.placeSelector({ 1, 1, 1 }));
    CHECK(editor.selectors().size() == 1);
    CHECK(editor.selectors()[0].id == 1);
    CHECK(!editor.selectors()[0].target);
    editor.setActiveLayer(0);
    editor.setLayerLocked(true);
    CHECK((editor.resolveSelectorTarget({ 1, 1, 0 }) ==
        GridPosition3 { 1, 1, 0 }));
    CHECK((editor.resolveSelectorTarget({ 2, 1, 0 }) ==
        GridPosition3 { 2, 1, 0 }));
    editor.setActiveLayer(1);
    CHECK((editor.resolveSelectorTarget({ 1, 1, 0 }) ==
        GridPosition3 { 1, 1, 1 }));
    CHECK((editor.resolveSelectorTarget({ 2, 1, 0 }) ==
        GridPosition3 { 2, 1, 1 }));
    CHECK(editor.updateSelectedSelectorTarget(
        LevelLocation { .level = 0, .screen = 0 }));
    CHECK(editor.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 0 }));
    editor.setTool(LevelEditor::Tool::Tiles);
    CHECK(editor.beginMove({ 1, 1, 1 }));
    CHECK(editor.tool() == LevelEditor::Tool::Tiles);
    CHECK(editor.pendingMove().has_value());
    if (editor.pendingMove()) {
        CHECK(editor.pendingMove()->kind ==
            LevelEditor::MoveObject::Kind::ScreenSelector);
        CHECK(editor.pendingMove()->selectorId == 1U);
    }
    CHECK(!editor.moveObject({ 3, 0, 1 }));
    CHECK((editor.selectors()[0].cell == GridPosition3 { 1, 1, 1 }));
    CHECK(editor.beginMove({ 1, 1, 1 }));
    CHECK(editor.moveObject({ 2, 1, 1 }));
    CHECK(editor.tool() == LevelEditor::Tool::Tiles);
    CHECK(editor.selectors()[0].id == 1);
    CHECK((editor.selectors()[0].cell == GridPosition3 { 2, 1, 1 }));
    CHECK(editor.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 0 }));
    editor.setTool(LevelEditor::Tool::Selectors);
    CHECK(editor.beginMove({ 3, 0, 1 }));
    CHECK(editor.pendingMove().has_value());
    if (editor.pendingMove()) {
        CHECK(editor.pendingMove()->kind ==
            LevelEditor::MoveObject::Kind::Tile);
    }
    CHECK(!editor.moveObject({ 2, 1, 1 }));
    CHECK(editor.beginMove({ 3, 0, 1 }));
    CHECK(editor.moveObject({ 3, 1, 1 }));
    CHECK(editor.tool() == LevelEditor::Tool::Selectors);
    CHECK((editor.selectors()[0].cell == GridPosition3 { 2, 1, 1 }));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][0][3] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.tryUndoEdit());
    CHECK((editor.selectors()[0].cell == GridPosition3 { 1, 1, 1 }));
    std::vector<LevelEditor::LevelDirectory> labelLevels {
        {
            .index = 0,
            .name = "Easy Plains",
            .screens = { { .index = 0 } },
        },
    };
    CHECK(LevelEditor::selectorTargetLabel(
        editor.selectors()[0], labelLevels) ==
        "Easy Plains / Screen 1");
    labelLevels[0].screens[0].name = "First Push";
    CHECK(LevelEditor::selectorTargetLabel(
        editor.selectors()[0], labelLevels) ==
        "Easy Plains / First Push");
    CHECK(editor.saveDocument(overworld));
    CHECK(std::filesystem::exists(project.runtime / "overworld.scr"));

    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(overworld));
    CHECK(loaded.selectors().size() == 1);
    CHECK(loaded.documentToLevel().selectorAt({ 1, 1, 1 }) != nullptr);
    CHECK(loaded.selectSelector(0));
    CHECK(loaded.updateSelectedSelectorTarget(std::nullopt));
    CHECK(!loaded.selectors()[0].target);
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 0 }));

    std::vector<LevelEditor::LevelDirectory> levels =
        loaded.collectLevelDirectories();
    loaded.addScreenAt(levels[0], 0);
    const Level shifted = Level::loadFromFile(
        project.source / "overworld.scr");
    CHECK(shifted.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 1 }));
    CHECK(std::filesystem::exists(project.runtime / "overworld.scr"));

    levels = loaded.collectLevelDirectories();
    loaded.deleteScreen(levels[0], 0);
    const Level shiftedBack = Level::loadFromFile(
        project.source / "overworld.scr");
    CHECK(shiftedBack.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 0 }));

    loaded.addLevelAt(0);
    const Level levelShifted = Level::loadFromFile(
        project.source / "overworld.scr");
    CHECK(levelShifted.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 1, .screen = 0 }));

    levels = loaded.collectLevelDirectories();
    loaded.deleteLevel(levels[0]);
    const Level levelShiftedBack = Level::loadFromFile(
        project.source / "overworld.scr");
    CHECK(levelShiftedBack.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 0 }));

    CHECK(loaded.loadDocument(overworld));
    CHECK(loaded.selectSelector(0));
    CHECK(loaded.deleteSelectedSelector());
    CHECK(loaded.selectors().empty());
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.selectors().size() == 1);
}

void testMoveTileIsAtomicAndUndoable()
{
    TEST("moveTileIsAtomicAndUndoable");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);
    editor.setCell({ 1, 1, 1 }, TileType::Wall);

    CHECK(editor.beginMove({ 1, 1, 1 }));
    CHECK(editor.pendingMove().has_value());
    if (editor.pendingMove()) {
        CHECK((editor.pendingMove()->source == GridPosition3 { 1, 1, 1 }));
        CHECK(editor.pendingMove()->kind ==
            LevelEditor::MoveObject::Kind::Tile);
        CHECK(editor.pendingMove()->tile == TileType::Wall);
    }
    CHECK(editor.moveObject({ 2, 1, 1 }));
    CHECK(!editor.pendingMove());
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.documentLayers()[1][1][2] ==
        tileTypeToChar(TileType::Wall));

    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.documentLayers()[1][1][2] ==
        tileTypeToChar(TileType::Air));

    CHECK(editor.beginMove({ 1, 1, 1 }));
    CHECK(!editor.moveObject({ 0, 0, 0 }));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));
}

void testComposedOverworldDocumentsArePathAwareAndTransactional()
{
    TEST("composedOverworldDocumentsArePathAwareAndTransactional");
    TemporaryProject project;
    const auto write = [](const std::filesystem::path& path,
                           std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::trunc);
        file << text;
    };
    write(project.source / "level0/screen0.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n C \n E \n");
    write(project.source / "overworld/screen1.scr",
        "@selector {\"cell\":[0,0,1],\"id\":1,"
        "\"target\":{\"level\":0,\"screen\":0}}\n\n"
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n C \n   \n");
    write(project.source / "overworld/layout.json",
        "{\n"
        "  \"format\": 3,\n"
        "  \"screenSize\": [3, 3],\n"
        "  \"screens\": ["
        "{\"id\": 1, \"file\": \"screen1.scr\", \"slot\": [0, 0]}]\n"
        "}\n");

    LevelEditor editor = makeEditor(project);
    const std::filesystem::path screen =
        project.source / "overworld/screen1.scr";
    CHECK(editor.loadDocument(screen));
    CHECK(editor.editingOverworld());
    CHECK(editor.overworldScreenId() == 1U);
    CHECK(editor.sourceLevelRoot() == project.source);
    CHECK(editor.runtimeLevelRoot() == project.runtime);

    editor.setCell({ 2, 1, 1 }, TileType::Player);
    CHECK(editor.documentLayers()[1][1][1] == tileTypeToChar(TileType::Air));
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Player));
    editor.resizeDocument(4, 4);
    CHECK(editor.documentWidth() == 3);
    CHECK(editor.documentHeight() == 3);

    const TreeSnapshot before = snapshotTree(project.source);
    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    CHECK(editor.documentLayers()[1][1][1] == tileTypeToChar(TileType::Wall));
    CHECK(snapshotTree(project.source) == before);
    editor.setCell({ 2, 2, 1 }, TileType::Wall);
    CHECK(editor.saveDocument(screen));
    CHECK(std::filesystem::exists(
        project.runtime / "overworld/layout.json"));
    CHECK(std::filesystem::exists(
        project.runtime / "overworld/screen1.scr"));

    const std::optional<Level> overworldDraft = editor.beginDraftPlayback();
    CHECK(overworldDraft.has_value());
    CHECK(editor.draftOverworldMap() != nullptr);
    CHECK(editor.playingDraft());
    CHECK(overworldDraft &&
        overworldDraft->playerStart() == GridPosition3({ 2, 1, 1 }));
    editor.setPlayingDraft(false);
    CHECK(editor.draftOverworldMap() == nullptr);

    OverworldMapEditor topologyDraft;
    topologyDraft.initialize(project.source, std::nullopt);
    CHECK(topologyDraft.addAdjacentScreen(1, { 1, 0 }));
    CHECK(!std::filesystem::exists(
        project.source / "overworld/screen2.scr"));
    editor.setCell({ 0, 2, 1 }, TileType::Wall);
    CHECK(editor.dirty());
    const std::optional<Level> unsavedTopologyDraft =
        editor.beginDraftPlayback(&topologyDraft);
    CHECK(unsavedTopologyDraft.has_value());
    CHECK(editor.draftOverworldMap() != nullptr);
    if (editor.draftOverworldMap()) {
        CHECK(editor.draftOverworldMap()->screens().size() == 2);
        const OverworldScreenRuntime* activeDraft =
            editor.draftOverworldMap()->screen(1);
        CHECK(activeDraft != nullptr);
        CHECK(activeDraft && activeDraft->definition.layers[1][2][0] ==
            tileTypeToChar(TileType::Wall));
    }
    editor.setPlayingDraft(false);

    std::vector<LevelEditor::LevelDirectory> levels =
        editor.collectLevelDirectories();
    editor.addScreenAt(levels[0], 0);
    const Level::Definition remapped = Level::loadDefinitionFromFile(screen);
    CHECK(remapped.selectors[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 1 }));

    write(project.source / "overworld/scratch.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n C \n   \n");
    CHECK(editor.loadDocument(project.source / "overworld/scratch.scr"));
    CHECK(!editor.editingOverworld());
}

void testComposedSelectorOwnershipIsEnforcedBeforeSave()
{
    TEST("composedSelectorOwnershipIsEnforcedBeforeSave");
    TemporaryProject project;
    const auto write = [](const std::filesystem::path& path,
                           std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::trunc);
        file << text;
    };
    write(project.source / "level0/screen0.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n C \n E \n");
    write(project.source / "overworld/screen1.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n  #\n C \n  #\n");
    write(project.source / "overworld/screen2.scr",
        "@selector {\"cell\":[1,1,1],\"id\":1,"
        "\"target\":{\"level\":0,\"screen\":0}}\n\n"
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n#  \n   \n#  \n");
    write(project.source / "overworld/layout.json",
        "{\n"
        "  \"format\": 3,\n"
        "  \"screenSize\": [3, 3],\n"
        "  \"screens\": [\n"
        "    {\"id\": 1, \"file\": \"screen1.scr\", \"slot\": [0, 0]},\n"
        "    {\"id\": 2, \"file\": \"screen2.scr\", \"slot\": [1, 0]}\n"
        "  ]\n"
        "}\n");

    LevelEditor editor = makeEditor(project);
    const std::filesystem::path screen1 =
        project.source / "overworld/screen1.scr";
    const std::filesystem::path screen2 =
        project.source / "overworld/screen2.scr";
    CHECK(editor.loadDocument(screen1));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Player));
    CHECK(editor.selectorLevelOwner(0) == 2U);
    CHECK(editor.placeSelector({ 0, 0, 1 }));
    CHECK(!editor.updateSelectedSelectorTarget(
        LevelLocation { .level = 0, .screen = 0 }));
    CHECK(!editor.selectors()[0].target);
    CHECK(editor.status().find("screen 2") != std::string::npos);

    CHECK(editor.openDocument(screen2));
    CHECK(editor.overworldScreenId() == 2U);
    CHECK(editor.hasInProgressDraft(screen1));
    CHECK(editor.openDocument(screen1));
    CHECK(editor.overworldScreenId() == 1U);
    CHECK(editor.selectors().size() == 1);
}

void testOverworldPlayerTileMovesAcrossComponents()
{
    TEST("overworldPlayerTileMovesAcrossComponents");
    TemporaryProject project;
    const auto write = [](const std::filesystem::path& path,
                           std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::trunc);
        file << text;
    };
    write(project.source / "level0/screen0.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n C \n E \n");
    write(project.source / "overworld/screen1.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n C \n   \n");
    write(project.source / "overworld/screen2.scr",
        "@layer 0\n...\n...\n...\n"
        "@layer 1\n   \n   \n   \n");
    write(project.source / "overworld/layout.json",
        "{\n"
        "  \"format\": 3,\n"
        "  \"screenSize\": [3, 3],\n"
        "  \"screens\": [\n"
        "    {\"id\": 1, \"file\": \"screen1.scr\", \"slot\": [0, 0]},\n"
        "    {\"id\": 2, \"file\": \"screen2.scr\", \"slot\": [1, 0]}\n"
        "  ]\n"
        "}\n");

    LevelEditor editor = makeEditor(project);
    const std::filesystem::path screen2 =
        project.source / "overworld/screen2.scr";
    CHECK(editor.loadDocument(screen2));
    editor.setCell({ 0, 1, 1 }, TileType::Player);
    CHECK(editor.saveDocument(screen2));

    const Level::Definition first = Level::loadDefinitionFromFile(
        project.source / "overworld/screen1.scr");
    const Level::Definition second = Level::loadDefinitionFromFile(screen2);
    CHECK(first.layers[1][1][1] == tileTypeToChar(TileType::Air));
    CHECK(second.layers[1][1][0] == tileTypeToChar(TileType::Player));
    const OverworldMap map = OverworldMap::load(
        project.source / "overworld");
    CHECK(map.startScreen() == 2U);
    CHECK(map.level().playerStart() == GridPosition3({ 3, 1, 1 }));
}

} // namespace

int main()
{
    testDocumentCommandsAndUndo();
    testTileValidationAndPlayerUniqueness();
    testAddLayerBelowShiftsContentAndWaterAndIsUndoable();
    testSaveLoadAndRuntimeMirror();
    testSelectedPathIsSeparateFromTheLoadedDocument();
    testOpeningScreensPreservesIndependentDraftsAndUndoHistory();
    testUndoRestoresTheLoadedDocumentPath();
    testWaterLayerEditingPersistenceAndLayerRenumbering();
    testProjectRenumberDeleteAndRestore();
    testUndoAfterNewEditDoesNotReplayAbandonedBranch();
    testResizePreservesOverlapAndUsesLayerFill();
    testPaintingOutsideExpandsAndShiftsDocumentAtomically();
    testInvalidLoadLeavesDocumentUntouched();
    testAlternateBrowserRootDoesNotMirrorRuntime();
    testBrowserFiltersJunkAndRejectsForeignDirectories();
    testFailedRenumberPreservesSourceAndRuntimeTrees();
    testDecorationEditingPersistenceAndUndo();
    testDecorationTransformSessionCoalescesUndoAndCanCancel();
    testSelectorEditingPersistenceUndoAndProjectRemapping();
    testMoveTileIsAtomicAndUndoable();
    testComposedOverworldDocumentsArePathAwareAndTransactional();
    testComposedSelectorOwnershipIsEnforcedBeforeSave();
    testOverworldPlayerTileMovesAcrossComponents();

    if (failures == 0) {
        std::cout << "LevelEditorTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "LevelEditorTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
