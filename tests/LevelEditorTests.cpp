// Headless tests for editor document commands and project filesystem behavior.
// No SDL, Vulkan, ImGui, rendering, or window dependencies.

#include "engine/LevelEditor.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
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

    editor.setActiveLayer(100);
    CHECK(editor.activeLayer() == editor.documentDepth() - 1);
    editor.setLayerLocked(true);
    CHECK(editor.layerLocked());

    editor.addLayer();
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

    editor.addScreenAt(levels[0], 1);
    levels = editor.collectLevelDirectories();
    CHECK(levels[0].screens.size() == 2);
    CHECK(levels[0].screens[0].index == 0);
    CHECK(levels[0].screens[1].index == 1);

    editor.deleteScreen(levels[0], 0);
    levels = editor.collectLevelDirectories();
    CHECK(levels[0].screens.size() == 1);
    CHECK(levels[0].screens[0].index == 0);
    CHECK(!std::filesystem::exists(levels[0].path / "screen1.scr"));

    editor.deleteLevel(levels[0]);
    levels = editor.collectLevelDirectories();
    std::vector<LevelEditor::LevelDirectory> deleted = editor.collectDeletedLevels();
    CHECK(levels.size() == 1);
    CHECK(levels[0].index == 0);
    CHECK(deleted.size() == 1);

    editor.restoreDeletedLevel(deleted[0].path);
    levels = editor.collectLevelDirectories();
    CHECK(levels.size() == 2);
    CHECK(levels[1].index == 1);
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

} // namespace

int main()
{
    testDocumentCommandsAndUndo();
    testTileValidationAndPlayerUniqueness();
    testSaveLoadAndRuntimeMirror();
    testProjectRenumberDeleteAndRestore();
    testUndoAfterNewEditDoesNotReplayAbandonedBranch();
    testResizePreservesOverlapAndUsesLayerFill();
    testInvalidLoadLeavesDocumentUntouched();
    testAlternateBrowserRootDoesNotMirrorRuntime();
    testBrowserFiltersJunkAndRejectsForeignDirectories();

    if (failures == 0) {
        std::cout << "LevelEditorTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "LevelEditorTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
