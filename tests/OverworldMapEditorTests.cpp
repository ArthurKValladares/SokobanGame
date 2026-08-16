#include "engine/OverworldMapEditor.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

struct TemporaryProject {
    TemporaryProject()
    {
        root = std::filesystem::temp_directory_path() /
            ("sokoban_overworld_map_editor_tests_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        source = root / "source";
        runtime = root / "runtime";
        std::filesystem::create_directories(source / "level0");
        std::filesystem::create_directories(source / "overworld");
        std::filesystem::create_directories(runtime);
        write(source / "level0/screen0.scr",
            "@layer 0\n"
            "...\n"
            "...\n"
            "...\n"
            "@layer 1\n"
            "   \n"
            " C \n"
            " E \n");
        write(source / "overworld/screen1.scr",
            "@layer 0\n"
            "...\n"
            "...\n"
            "...\n"
            "@layer 1\n"
            "   \n"
            "   \n"
            "   \n");
        write(source / "overworld/layout.json",
            "{\n"
            "  \"format\": 1,\n"
            "  \"screenSize\": [3, 3],\n"
            "  \"start\": { \"screen\": 1, \"cell\": [1, 1, 1] },\n"
            "  \"screens\": [\n"
            "    { \"id\": 1, \"file\": \"screen1.scr\", \"slot\": [0, 0] }\n"
            "  ],\n"
            "  \"connections\": []\n"
            "}\n");
    }

    ~TemporaryProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    static void write(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::trunc);
        file << text;
    }

    std::filesystem::path root;
    std::filesystem::path source;
    std::filesystem::path runtime;
};

void testConnectedScreenLifecycleAndHistory()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);

    CHECK(editor.loaded());
    CHECK(!editor.dirty());
    CHECK(editor.screens().size() == 1);
    CHECK(editor.selectedScreen() == 1U);
    CHECK(editor.addConnectedScreen(1, { 1, 0 }, { 2, 1, 1 }));
    CHECK(editor.dirty());
    CHECK(editor.screens().size() == 2);
    CHECK(editor.layout().connections.size() == 1);
    if (!editor.layout().connections.empty()) {
        CHECK(editor.layout().connections[0].b.screen == 2U);
        CHECK((editor.layout().connections[0].b.cell ==
            sokoban::GridPosition3 { 0, 1, 1 }));
    }
    CHECK(editor.canUndo());
    CHECK(editor.undo());
    CHECK(editor.screens().size() == 1);
    CHECK(editor.canRedo());
    CHECK(editor.redo());
    CHECK(editor.screens().size() == 2);
    CHECK(!editor.moveScreen(2, { 2, 0 }));

    CHECK(editor.save());
    CHECK(!editor.dirty());
    CHECK(std::filesystem::exists(project.source / "overworld/screen2.scr"));
    CHECK(std::filesystem::exists(project.runtime / "overworld/layout.json"));
    CHECK(std::filesystem::exists(project.runtime / "overworld/screen2.scr"));
    CHECK(!std::filesystem::exists(project.runtime / "overworld/Deleted"));

    const sokoban::OverworldMap composed = sokoban::OverworldMap::load(
        project.source / "overworld");
    CHECK(composed.screens().size() == 2);
    CHECK(composed.connections().size() == 1);
}

void testInvalidDraftSavePreservesProject()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);

    CHECK(editor.addScreen({ 2, 2 }));
    CHECK(!editor.save());
    CHECK(editor.status().find("preserved") != std::string::npos);
    CHECK(!std::filesystem::exists(project.source / "overworld/screen2.scr"));
    const sokoban::OverworldLayout original = sokoban::loadOverworldLayout(
        project.source / "overworld/layout.json");
    CHECK(original.screens.size() == 1);
    CHECK(editor.undo());
    CHECK(!editor.dirty());
}

void testSoftDeleteAndRestoreKeepStableIdentity()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);
    CHECK(editor.addConnectedScreen(1, { 1, 0 }, { 2, 1, 1 }));
    CHECK(editor.save());

    CHECK(editor.deleteScreen(2));
    CHECK(editor.save());
    CHECK(!std::filesystem::exists(project.source / "overworld/screen2.scr"));
    CHECK(std::filesystem::exists(
        project.source / "overworld/Deleted/screen2.scr"));
    CHECK(!std::filesystem::exists(project.runtime / "overworld/screen2.scr"));
    CHECK(!std::filesystem::exists(project.runtime / "overworld/Deleted"));
    CHECK(editor.deletedScreens() ==
        std::vector<sokoban::OverworldScreenId> { 2 });

    CHECK(editor.restoreDeletedScreen(2, { 1, 0 }));
    CHECK(editor.connect(1, 2, { 2, 1, 1 }));
    CHECK(editor.save());
    CHECK(std::filesystem::exists(project.source / "overworld/screen2.scr"));
    CHECK(!std::filesystem::exists(
        project.source / "overworld/Deleted/screen2.scr"));
    CHECK(editor.screen(2) != nullptr);
}

void testStartValidation()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, std::nullopt);
    CHECK(!editor.setStart(1, { 1, 1, 0 }));
    CHECK(editor.setStart(1, { 0, 0, 1 }));
    CHECK((editor.layout().start.cell ==
        sokoban::GridPosition3 { 0, 0, 1 }));
    CHECK(editor.save());
}

void testCellPickingToolsAndUnsavedComposition()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, std::nullopt);

    CHECK(editor.beginSetStartCell(1));
    CHECK(editor.cellTool().has_value());
    CHECK(!editor.applyCellTool(2, { 0, 0, 1 }));
    CHECK(editor.cellTool().has_value());
    CHECK(!editor.applyCellTool(1, { 0, 0, 0 }));
    CHECK(editor.cellTool().has_value());
    CHECK(editor.applyCellTool(1, { 0, 0, 1 }));
    CHECK(!editor.cellTool().has_value());
    CHECK((editor.layout().start.cell ==
        sokoban::GridPosition3 { 0, 0, 1 }));

    CHECK(editor.beginAddConnectedScreenCell(1, { 1, 0 }));
    CHECK(editor.cellToolPrompt().find("boundary") != std::string::npos);
    CHECK(!editor.applyCellTool(1, { 1, 1, 1 }));
    CHECK(editor.cellTool().has_value());
    CHECK(editor.applyCellTool(1, { 2, 1, 1 }));
    CHECK(!editor.cellTool().has_value());
    CHECK(editor.screens().size() == 2);
    CHECK(!std::filesystem::exists(
        project.source / "overworld/screen2.scr"));

    const sokoban::OverworldMap draft = sokoban::OverworldMap::load(
        project.source / "overworld",
        editor.draftOverride());
    CHECK(draft.screens().size() == 2);
    CHECK(draft.connections().size() == 1);

    CHECK(editor.disconnect(0));
    CHECK(editor.beginConnectCell(1, 2));
    CHECK(editor.applyCellTool(1, { 2, 1, 1 }));
    CHECK(editor.layout().connections.size() == 1);
    CHECK(editor.beginSetStartCell(1));
    editor.cancelCellTool();
    CHECK(!editor.cellTool().has_value());
}

void testTopologyDraftPreservesNewerComponentSaves()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);

    TemporaryProject::write(project.source / "overworld/screen1.scr",
        "@layer 0\n"
        "...\n"
        "...\n"
        "...\n"
        "@layer 1\n"
        "   \n"
        "   \n"
        "  #\n");

    const sokoban::OverworldMap draft = sokoban::OverworldMap::load(
        project.source / "overworld",
        editor.draftOverride());
    CHECK(draft.screen(1) != nullptr);
    CHECK(draft.screen(1) &&
        draft.screen(1)->definition.layers[1][2][2] == '#');

    CHECK(editor.setStart(1, { 0, 0, 1 }));
    CHECK(editor.save());
    const sokoban::Level::Definition saved =
        sokoban::Level::loadDefinitionFromFile(
            project.source / "overworld/screen1.scr");
    CHECK(saved.layers[1][2][2] == '#');
    const sokoban::Level::Definition mirrored =
        sokoban::Level::loadDefinitionFromFile(
            project.runtime / "overworld/screen1.scr");
    CHECK(mirrored.layers[1][2][2] == '#');
}

} // namespace

int main()
{
    testConnectedScreenLifecycleAndHistory();
    testInvalidDraftSavePreservesProject();
    testSoftDeleteAndRestoreKeepStableIdentity();
    testStartValidation();
    testCellPickingToolsAndUnsavedComposition();
    testTopologyDraftPreservesNewerComponentSaves();

    if (failures != 0) {
        std::cerr << "OverworldMapEditorTests: " << failures
                  << " failure(s) of " << checks << " checks\n";
        return 1;
    }
    std::cout << "OverworldMapEditorTests: " << checks
              << " checks passed\n";
    return 0;
}
