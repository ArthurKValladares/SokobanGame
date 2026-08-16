#include "engine/OverworldMapEditor.hpp"

#include <algorithm>
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
            " C \n"
            "   \n");
        write(source / "overworld/layout.json",
            "{\n"
            "  \"format\": 3,\n"
            "  \"screenSize\": [3, 3],\n"
            "  \"screens\": [\n"
            "    { \"id\": 1, \"file\": \"screen1.scr\", \"slot\": [0, 0] }\n"
            "  ]\n"
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

void testScreenLifecycleAndHistory()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);

    CHECK(editor.loaded());
    CHECK(!editor.dirty());
    CHECK(editor.screens().size() == 1);
    CHECK(editor.selectedScreen() == 1U);
    CHECK(editor.addAdjacentScreen(1, { 1, 0 }));
    CHECK(editor.dirty());
    CHECK(editor.screens().size() == 2);
    CHECK(editor.canUndo());
    CHECK(editor.undo());
    CHECK(editor.screens().size() == 1);
    CHECK(editor.canRedo());
    CHECK(editor.redo());
    CHECK(editor.screens().size() == 2);
    CHECK(editor.moveScreen(2, { 2, 0 }));
    CHECK(editor.save());
    CHECK(!editor.dirty());
    CHECK(std::filesystem::exists(project.source / "overworld/screen2.scr"));
    CHECK(std::filesystem::exists(project.runtime / "overworld/layout.json"));
    CHECK(std::filesystem::exists(project.runtime / "overworld/screen2.scr"));
    CHECK(!std::filesystem::exists(project.runtime / "overworld/Deleted"));

    const sokoban::OverworldMap composed = sokoban::OverworldMap::load(
        project.source / "overworld");
    CHECK(composed.screens().size() == 2);
}

void testIndependentScreenCanBeSaved()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);

    CHECK(editor.addScreen({ 2, 2 }));
    CHECK(editor.save());
    CHECK(std::filesystem::exists(project.source / "overworld/screen2.scr"));
    const sokoban::OverworldLayout saved = sokoban::loadOverworldLayout(
        project.source / "overworld/layout.json");
    CHECK(saved.screens.size() == 2);
    CHECK(saved.screens[1].slot == sokoban::OverworldSlot({ 2, 2 }));
    CHECK(!editor.dirty());
}

void testSoftDeleteAndRestoreKeepStableIdentity()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, project.runtime);
    CHECK(editor.addAdjacentScreen(1, { 1, 0 }));
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
    CHECK(editor.save());
    CHECK(std::filesystem::exists(project.source / "overworld/screen2.scr"));
    CHECK(!std::filesystem::exists(
        project.source / "overworld/Deleted/screen2.scr"));
    CHECK(editor.screen(2) != nullptr);
}

void testPlayerTileValidation()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, std::nullopt);
    CHECK(editor.addAdjacentScreen(1, { 1, 0 }));
    CHECK(editor.deleteScreen(1));
    CHECK(!editor.save());
    CHECK(editor.status().find("exactly one Player tile") !=
        std::string::npos);
}

void testUnsavedComposition()
{
    TemporaryProject project;
    sokoban::OverworldMapEditor editor;
    editor.initialize(project.source, std::nullopt);

    CHECK(editor.addAdjacentScreen(1, { 1, 0 }));
    CHECK(editor.screens().size() == 2);
    CHECK(editor.selectedScreen() == 2U);
    const sokoban::Level::Definition* newScreen = editor.definition(2);
    CHECK(newScreen != nullptr);
    if (newScreen) {
        CHECK(std::ranges::all_of(
            newScreen->layers[0],
            [](const std::string& row) { return row == "..."; }));
        CHECK(std::ranges::all_of(
            newScreen->layers[1],
            [](const std::string& row) { return row == "   "; }));
    }
    CHECK(editor.screens().size() == 2);
    CHECK(!std::filesystem::exists(
        project.source / "overworld/screen2.scr"));

    const sokoban::OverworldMap draft = sokoban::OverworldMap::load(
        project.source / "overworld",
        editor.draftOverride());
    CHECK(draft.screens().size() == 2);
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
        " C \n"
        "  #\n");

    const sokoban::OverworldMap draft = sokoban::OverworldMap::load(
        project.source / "overworld",
        editor.draftOverride());
    CHECK(draft.screen(1) != nullptr);
    CHECK(draft.screen(1) &&
        draft.screen(1)->definition.layers[1][2][2] == '#');

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
    testScreenLifecycleAndHistory();
    testIndependentScreenCanBeSaved();
    testSoftDeleteAndRestoreKeepStableIdentity();
    testPlayerTileValidation();
    testUnsavedComposition();
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
