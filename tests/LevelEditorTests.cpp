// Headless tests for editor document commands and project filesystem behavior.
// No SDL, Vulkan, ImGui, rendering, or window dependencies.

#include "TestHarness.hpp"

#include "engine/LevelEditor.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/OverworldMapEditor.hpp"
#include "engine/TileTypes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sokoban;

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

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
}

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

        snapshot.emplace_back(relative, readFile(entry.path()));
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

using Plates = std::vector<GridPosition3>;

void testColorGroupsBecomeExplicitLinks()
{
    TEST("colorGroupsBecomeExplicitLinks");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(7, 3, false);

    const Vec3 orange = editor.activeLinkColor();
    const Vec3 blue { 0.2f, 0.4f, 1.0f };
    const GridPosition3 plateA { 1, 1, 1 };
    const GridPosition3 plateB { 2, 1, 1 };
    const GridPosition3 gate { 3, 1, 1 };
    const GridPosition3 rotator { 4, 1, 1 };
    const GridPosition3 secondGate { 5, 1, 1 };

    // Plates and a device painted in one color are linked.
    CHECK(editor.setCell(plateA, TileType::PressurePlate));
    CHECK(editor.setCell(plateB, TileType::PressurePlate));
    CHECK(editor.setCell(gate, TileType::Gate));
    CHECK(editor.linkColorAt(gate) == std::optional<Vec3>(orange));
    // The editor records carry colors, never plate lists.
    CHECK(editor.gates()[0].pressurePlates.empty());
    CHECK(editor.documentToLevel().gateAt(gate)->pressurePlates ==
        (Plates { plateA, plateB }));

    // A rotator painted blue, and plate B repainted blue, form a second
    // group; a second blue gate shares plate B with the rotator.
    editor.setActiveLinkColor(blue);
    CHECK(editor.setCell(rotator, TileType::RotatorClockwise));
    CHECK(editor.setCell(plateB, TileType::PressurePlate));
    CHECK(editor.linkColorAt(plateB) == std::optional<Vec3>(blue));
    CHECK(!editor.setCell(plateB, TileType::PressurePlate));
    CHECK(editor.setCell(secondGate, TileType::Gate));
    {
        const Level level = editor.documentToLevel();
        CHECK(level.gateAt(gate)->pressurePlates == Plates { plateA });
        CHECK(level.rotatorAt(rotator)->pressurePlates == Plates { plateB });
        CHECK(level.gateAt(secondGate)->pressurePlates == Plates { plateB });
    }
    const std::vector<LevelEditor::LinkGroup> groups = editor.linkGroups();
    CHECK(groups.size() == 2);
    if (groups.size() == 2) {
        CHECK(LevelEditor::sameLinkColor(groups[0].color, orange));
        CHECK(groups[0].pressurePlates == Plates { plateA });
        CHECK(groups[0].gates == Plates { gate });
        CHECK(groups[1].pressurePlates == Plates { plateB });
        CHECK(groups[1].gates == Plates { secondGate });
        CHECK(groups[1].rotators == Plates { rotator });
    }
    // Colors that look the same in the picker are the same group.
    CHECK(LevelEditor::sameLinkColor(blue, { 0.2001f, 0.4f, 1.0f }));
    CHECK(editor.linkedPressurePlates({ 0.2001f, 0.4f, 1.0f }) ==
        Plates { plateB });

    // Recoloring one item is one undoable command.
    CHECK(editor.setLinkColor(secondGate, orange));
    CHECK(editor.documentToLevel().gateAt(secondGate)->pressurePlates ==
        Plates { plateA });
    CHECK(editor.tryUndoEdit());
    CHECK(editor.linkColorAt(secondGate) == std::optional<Vec3>(blue));
    CHECK(!editor.setLinkColor({ 0, 2, 1 }, orange));
    CHECK(!editor.setLinkColor(gate, { 2.0f, 0.0f, 0.0f }));

    // Recoloring a whole group into another merges them.
    CHECK(editor.recolorLinkGroup(blue, orange));
    CHECK(editor.linkGroups().size() == 1);
    CHECK(editor.documentToLevel().rotatorAt(rotator)->pressurePlates ==
        (Plates { plateA, plateB }));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.linkGroups().size() == 2);

    // A plate that drives nothing keeps its color through a save as an
    // editor-only record; linked plates take theirs from their device.
    const Vec3 green { 0.1f, 0.9f, 0.2f };
    const GridPosition3 loose { 6, 1, 1 };
    editor.setActiveLinkColor(green);
    CHECK(editor.setCell(loose, TileType::PressurePlate));
    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    std::ifstream saved(path);
    const std::string text(
        (std::istreambuf_iterator<char>(saved)),
        std::istreambuf_iterator<char>());
    CHECK(text.find("@linkcolor {\"cell\":[6,1,1]") != std::string::npos);
    CHECK(text.find("@linkcolor {\"cell\":[1,1,1]") == std::string::npos);
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(path, false));
    CHECK(loaded.pressurePlateColors() == editor.pressurePlateColors());
    CHECK(loaded.gates() == editor.gates());
    CHECK(loaded.rotators() == editor.rotators());
    CHECK(loaded.status().find("distinct colors") == std::string::npos);
    // Gameplay sees only the explicit links.
    const Level level = Level::loadFromFile(path);
    CHECK(level.gateAt(gate)->pressurePlates == Plates { plateA });
    CHECK(level.gateAt(secondGate)->pressurePlates == Plates { plateB });

    // A moved plate keeps its color; an erased one leaves its group.
    const GridPosition3 movedPlate { 1, 2, 1 };
    CHECK(loaded.beginMove(plateA));
    CHECK(loaded.moveObject(movedPlate));
    CHECK(loaded.linkColorAt(movedPlate) == std::optional<Vec3>(orange));
    CHECK(loaded.documentToLevel().gateAt(gate)->pressurePlates ==
        Plates { movedPlate });
    CHECK(loaded.setCell(movedPlate, TileType::Air));
    CHECK(loaded.linkedPressurePlates(orange).empty());
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.linkedPressurePlates(orange) == Plates { movedPlate });

    // The link-color brush recolors whatever linkable tile tops the column.
    // A movable object becomes its own linked member without changing the
    // pressure plate beneath it.
    loaded.setActiveLinkColor(green);
    CHECK(loaded.paintLinkColorAt({ secondGate.x, secondGate.y, 0 }));
    CHECK(loaded.linkColorAt(secondGate) == std::optional<Vec3>(green));
    CHECK(!loaded.paintLinkColorAt({ secondGate.x, secondGate.y, 0 }));
    CHECK(!loaded.paintLinkColorAt({ 0, 2, 0 }));
    CHECK(loaded.setCell({ loose.x, loose.y, loose.z }, TileType::Rock));
    loaded.setActiveLinkColor(blue);
    CHECK(loaded.paintLinkColorAt({ loose.x, loose.y, 0 }));
    CHECK(loaded.linkColorAt(loose) == std::optional<Vec3>(blue));
    CHECK(loaded.linkedPressurePlates(green) == Plates { loose });
    // Locked to the ground layer, only ground is in reach.
    loaded.setActiveLayer(0);
    loaded.setLayerLocked(true);
    CHECK(!loaded.paintLinkColorAt({ secondGate.x, secondGate.y, 0 }));
    loaded.setLayerLocked(false);
    // Strokes fold a drag into one undo step.
    CHECK(loaded.beginStroke());
    loaded.setActiveLinkColor(orange);
    CHECK(loaded.paintLinkColorAt({ secondGate.x, secondGate.y, 0 }));
    CHECK(loaded.paintLinkColorAt({ loose.x, loose.y, 0 }));
    CHECK(loaded.endStroke());
    CHECK(loaded.linkColorAt(loose) == std::optional<Vec3>(orange));
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.linkColorAt(secondGate) == std::optional<Vec3>(green));
    CHECK(loaded.linkColorAt(loose) == std::optional<Vec3>(blue));

    // Alt-click picking a linked tile picks up its color.
    loaded.setActiveLinkColor(green);
    CHECK(loaded.pickTile({ rotator.x, rotator.y, 0 }) ==
        TileType::RotatorClockwise);
    CHECK(LevelEditor::sameLinkColor(loaded.activeLinkColor(), blue));
}

void testExplicitLinksBecomeColorGroupsOnLoad()
{
    TEST("explicitLinksBecomeColorGroupsOnLoad");
    TemporaryProject project;
    std::filesystem::create_directories(project.source / "level0");
    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    const auto write = [&](const std::string& records) {
        std::ofstream file(path);
        file << records << "\n@layer 0\n.......\n\n@layer 1\nQPPGG..\n";
    };
    const GridPosition3 first { 1, 0, 1 };
    const GridPosition3 second { 2, 0, 1 };
    const GridPosition3 gateA { 3, 0, 1 };
    const GridPosition3 gateB { 4, 0, 1 };

    // Two gates authored with the same color but different plates get
    // distinct colors, so the links survive a save unchanged.
    write(
        "@gate {\"cell\":[3,0,1],\"color\":[1.0,0.72,0.12],\"plates\":[[1,0,1]]}\n"
        "@gate {\"cell\":[4,0,1],\"color\":[1.0,0.72,0.12],\"plates\":[[2,0,1]]}");
    LevelEditor editor = makeEditor(project);
    CHECK(editor.loadDocument(path, false));
    CHECK(editor.status().find("distinct colors") != std::string::npos);
    CHECK(!LevelEditor::sameLinkColor(
        editor.gates()[0].color, editor.gates()[1].color));
    CHECK(editor.linkColorAt(first) == editor.linkColorAt(gateA));
    CHECK(editor.linkColorAt(second) == editor.linkColorAt(gateB));
    {
        const Level level = editor.documentToLevel();
        CHECK(level.gateAt(gateA)->pressurePlates == Plates { first });
        CHECK(level.gateAt(gateB)->pressurePlates == Plates { second });
    }

    // Links colors cannot express are regrouped, and the editor says so.
    write(
        "@gate {\"cell\":[3,0,1],\"color\":[1.0,0.72,0.12],\"plates\":[[1,0,1],[2,0,1]]}\n"
        "@gate {\"cell\":[4,0,1],\"color\":[0.2,0.4,1.0],\"plates\":[[1,0,1]]}");
    LevelEditor regrouped = makeEditor(project);
    CHECK(regrouped.loadDocument(path, false));
    CHECK(regrouped.status().find("Check the links") != std::string::npos);

    // Links that already match their colors load untouched.
    write(
        "@gate {\"cell\":[3,0,1],\"color\":[1.0,0.72,0.12],\"plates\":[[1,0,1],[2,0,1]]}\n"
        "@gate {\"cell\":[4,0,1],\"color\":[1.0,0.72,0.12],\"plates\":[[1,0,1],[2,0,1]]}");
    LevelEditor shared = makeEditor(project);
    CHECK(shared.loadDocument(path, false));
    CHECK(shared.status().find("distinct colors") == std::string::npos);
    CHECK(shared.linkGroups().size() == 1);
    CHECK(shared.gates()[0].color == Vec3({ 1.0f, 0.72f, 0.12f }));
}

void testMovableObjectsJoinColorGroupsAndKeepLinksWhenMoved()
{
    TEST("movableObjectsJoinColorGroupsAndKeepLinksWhenMoved");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(5, 2, false);

    const Vec3 blue { 0.2f, 0.4f, 1.0f };
    const GridPosition3 first { 1, 0, 1 };
    const GridPosition3 second { 3, 0, 1 };
    const GridPosition3 movedFirst { 1, 1, 1 };
    CHECK(editor.setCell(first, TileType::Rock));
    CHECK(editor.setCell(second, TileType::Ice));
    CHECK(!editor.objectLinkColorAt(first));

    editor.setActiveLinkColor(blue);
    CHECK(editor.paintLinkColorAt({ first.x, first.y, 0 }));
    CHECK(editor.paintLinkColorAt({ second.x, second.y, 0 }));
    CHECK(editor.objectLinks().size() == 2);
    CHECK(editor.objectLinkColorAt(first) == std::optional<Vec3>(blue));
    CHECK(editor.objectLinkColorAt(second) == std::optional<Vec3>(blue));
    CHECK(editor.linkGroups().size() == 1);
    if (!editor.linkGroups().empty()) {
        CHECK(editor.linkGroups()[0].objects == (Plates { first, second }));
    }
    CHECK(editor.documentToLevel().movablesAreLinked(0, 1));

    CHECK(editor.beginMove(first));
    CHECK(editor.moveObject(movedFirst));
    CHECK(!editor.objectLinkColorAt(first));
    CHECK(editor.objectLinkColorAt(movedFirst) == std::optional<Vec3>(blue));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.objectLinkColorAt(first) == std::optional<Vec3>(blue));
    CHECK(!editor.objectLinkColorAt(movedFirst));

    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    CHECK(readFile(path).find("@objectlink ") != std::string::npos);
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(path, false));
    CHECK(loaded.objectLinks() == editor.objectLinks());
    CHECK(loaded.documentToLevel().movablesAreLinked(0, 1));

    CHECK(loaded.setCell(first, TileType::Air));
    CHECK(!loaded.objectLinkColorAt(first));
    CHECK(loaded.objectLinks().size() == 1);
}

void testUnitsAndMirrorsStackOnPlates()
{
    TEST("unitsAndMirrorsStackOnPlates");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(6, 3, false);
    const auto top = [&](GridPosition3 cell) {
        return charToTileType(editor.documentLayers()
            [static_cast<std::size_t>(cell.z)]
            [static_cast<std::size_t>(cell.y)]
            [static_cast<std::size_t>(cell.x)]).value_or(TileType::Air);
    };

    // Unit painted onto a rotator: the rotator (and its record) stay beneath.
    const GridPosition3 rotatorCell { 2, 1, 1 };
    const GridPosition3 plate { 1, 1, 1 };
    CHECK(editor.setCell(plate, TileType::PressurePlate));
    CHECK(editor.setCell(rotatorCell, TileType::RotatorClockwise));
    const Vec3 linkColor = editor.rotators()[0].color;
    CHECK(editor.linkedPressurePlates(linkColor) == Plates { plate });
    CHECK(editor.setCell(rotatorCell, TileType::MirrorNorthWest));
    CHECK(top(rotatorCell) == TileType::MirrorNorthWest);
    const std::vector<Level::Plate> expectedPlates {
        { .cell = rotatorCell, .tile = TileType::RotatorClockwise },
    };
    CHECK(editor.coveredPlates() == expectedPlates);
    CHECK(editor.documentPlateAt(rotatorCell) == TileType::RotatorClockwise);
    CHECK(editor.rotators().size() == 1);
    CHECK(editor.rotators()[0].color == linkColor);
    // Repainting a movable mirror gives it the active object link color.
    CHECK(editor.setCell(rotatorCell, TileType::MirrorNorthWest));
    // Once linked, painting the same stack again changes nothing.
    CHECK(!editor.setCell(rotatorCell, TileType::MirrorNorthWest));
    // Replacing the occupant keeps the plate.
    CHECK(editor.setCell(rotatorCell, TileType::Rock));
    CHECK(top(rotatorCell) == TileType::Rock);
    CHECK(editor.documentPlateAt(rotatorCell) == TileType::RotatorClockwise);
    // Swapping the plate beneath keeps the occupant and the rotator record.
    CHECK(editor.setCell(rotatorCell, TileType::RotatorCounterClockwise));
    CHECK(top(rotatorCell) == TileType::Rock);
    CHECK(editor.documentPlateAt(rotatorCell) ==
        TileType::RotatorCounterClockwise);
    CHECK(editor.rotators().size() == 1);

    // A plate painted under an existing unit slides beneath it, and a covered
    // pressure plate can still be linked.
    const GridPosition3 heroCell { 4, 1, 1 };
    CHECK(editor.setCell(heroCell, TileType::Rogue));
    CHECK(editor.setCell(heroCell, TileType::PressurePlate));
    CHECK(top(heroCell) == TileType::Rogue);
    CHECK(editor.documentPlateAt(heroCell) == TileType::PressurePlate);
    CHECK(editor.linkedPressurePlates(linkColor) == (Plates { plate, heroCell }));

    const Level level = editor.documentToLevel();
    CHECK(level.coveredPlates().size() == 2);
    CHECK(level.tileAt(4, 1, 1) == TileType::PressurePlate);
    CHECK(level.tileAt(2, 1, 1) == TileType::RotatorCounterClockwise);

    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(path, false));
    CHECK(loaded.coveredPlates() == editor.coveredPlates());
    CHECK(loaded.rotators() == editor.rotators());

    // Erasing lifts the unit off and leaves the plate; erasing again removes
    // the plate and its links.
    CHECK(loaded.setCell(heroCell, TileType::Air));
    CHECK(loaded.coveredPlates().size() == 1);
    CHECK(loaded.documentLayers()[1][1][4] ==
        tileTypeToChar(TileType::PressurePlate));
    CHECK(loaded.linkedPressurePlates(linkColor).size() == 2);
    CHECK(loaded.setCell(heroCell, TileType::Air));
    CHECK(loaded.linkedPressurePlates(linkColor) == Plates { plate });
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.coveredPlates() == editor.coveredPlates());

    // Painting a non-stacking tile replaces the whole stack.
    CHECK(loaded.setCell(rotatorCell, TileType::Wall));
    CHECK(loaded.coveredPlates().size() == 1);
    CHECK(loaded.rotators().empty());

    // Moving a unit onto a free plate stacks it; moving it off again leaves
    // the plate behind.
    LevelEditor mover = makeEditor(project);
    mover.newDocument(6, 3, false);
    CHECK(mover.setCell({ 1, 1, 1 }, TileType::Rock));
    CHECK(mover.setCell({ 3, 1, 1 }, TileType::End));
    CHECK(mover.beginMove({ 1, 1, 1 }));
    CHECK(mover.moveObject({ 3, 1, 1 }));
    CHECK(mover.documentPlateAt({ 3, 1, 1 }) == TileType::End);
    CHECK(mover.coveredPlates().size() == 1);
    CHECK(mover.beginMove({ 3, 1, 1 }));
    CHECK(mover.moveObject({ 4, 1, 1 }));
    CHECK(mover.coveredPlates().empty());
    CHECK(mover.documentLayers()[1][1][3] == tileTypeToChar(TileType::End));
    CHECK(mover.documentLayers()[1][1][4] == tileTypeToChar(TileType::Rock));
    // Plates cannot be moved on top of anything.
    CHECK(mover.beginMove({ 3, 1, 1 }));
    CHECK(!mover.moveObject({ 4, 1, 1 }));
}

void testGateStartOpenIsAnUndoableGateSetting()
{
    TEST("gateStartOpenIsAnUndoableGateSetting");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(6, 3, false);

    const GridPosition3 plate { 1, 1, 1 };
    const GridPosition3 gate { 3, 1, 1 };
    CHECK(editor.setCell(plate, TileType::PressurePlate));
    CHECK(editor.setCell(gate, TileType::Gate));
    CHECK(editor.gates().size() == 1);
    CHECK(!editor.gates()[0].startOpen);

    CHECK(editor.setGateStartOpen(0, true));
    CHECK(editor.gates()[0].startOpen);
    CHECK(!editor.setGateStartOpen(0, true));
    CHECK(!editor.setGateStartOpen(1, true));
    CHECK(editor.documentToLevel().gateAt(gate)->startOpen);

    CHECK(editor.tryUndoEdit());
    CHECK(!editor.gates()[0].startOpen);
    CHECK(editor.tryRedoEdit());
    CHECK(editor.gates()[0].startOpen);

    // Moving the gate keeps the setting.
    const GridPosition3 moved { 4, 1, 1 };
    CHECK(editor.beginMove(gate));
    CHECK(editor.moveObject(moved));
    CHECK(editor.gates().size() == 1);
    CHECK(editor.gates()[0].cell == moved);
    CHECK(editor.gates()[0].startOpen);
}

void testElevatorStopsPersistAndFollowEditorCommands()
{
    TEST("elevatorStopsPersistAndFollowEditorCommands");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(6, 3, false);

    const GridPosition3 plate { 1, 1, 1 };
    const GridPosition3 elevatorCell { 3, 1, 0 };
    CHECK(editor.setCell(plate, TileType::PressurePlate));
    CHECK(editor.setCell(elevatorCell, TileType::Elevator));
    CHECK(editor.elevators().size() == 1);
    CHECK(editor.elevators()[0].cell == elevatorCell);
    // A new elevator stops only where it was painted.
    CHECK(editor.elevators()[0].levels == std::vector<int> { 0 });
    // Painted in the plate's color, it is linked to it.
    const Vec3 color = editor.elevators()[0].color;
    CHECK(editor.linkedPressurePlates(color) == Plates { plate });
    // Two more layers for the platform to travel to.
    CHECK(editor.setCell({ 5, 2, 3 }, TileType::Wall));
    CHECK(editor.documentDepth() == 4);

    CHECK(editor.setElevatorLevels(0, { 0, 3, 2 }));
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 0, 3, 2 }));
    const Level level = editor.documentToLevel();
    CHECK(level.elevatorAt(elevatorCell) != nullptr &&
        level.elevatorAt(elevatorCell)->pressurePlates == Plates { plate });
    CHECK(level.pressurePlateLinkColor(plate) == std::optional<Vec3>(color));

    // Invalid stops are refused without touching the document.
    CHECK(!editor.setElevatorLevels(0, { 1, 3 }));
    CHECK(!editor.setElevatorLevels(0, { 0, 4 }));
    CHECK(!editor.setElevatorLevels(0, { 0, 2, 2 }));
    CHECK(!editor.setElevatorLevels(0, {}));
    CHECK(!editor.setElevatorLevels(0, { 0, 3, 2 }));
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 0, 3, 2 }));

    CHECK(editor.tryUndoEdit());
    CHECK(editor.elevators()[0].levels == std::vector<int> { 0 });
    CHECK(editor.tryRedoEdit());
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 0, 3, 2 }));

    // Inserting a layer underneath shifts the elevator, its stops and links.
    editor.setActiveLayer(0);
    editor.addLayerBelow();
    CHECK(editor.elevators()[0].cell == (GridPosition3 { 3, 1, 1 }));
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 1, 4, 3 }));
    CHECK(editor.linkedPressurePlates(color) ==
        (Plates { { 1, 1, 2 } }));
    // Deleting a stop's layer drops just that stop.
    editor.setActiveLayer(3);
    editor.deleteActiveLayer();
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 1, 3 }));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.tryUndoEdit());
    CHECK(editor.elevators()[0].cell == elevatorCell);
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 0, 3, 2 }));

    // Moving the tile to another layer moves its own stop with it.
    const GridPosition3 movedElevator { 4, 1, 1 };
    CHECK(editor.beginMove(elevatorCell));
    CHECK(editor.moveObject(movedElevator));
    CHECK(editor.elevators().size() == 1);
    CHECK(editor.elevators()[0].cell == movedElevator);
    CHECK(editor.elevators()[0].levels == (std::vector<int> { 1, 3, 2 }));
    CHECK(editor.elevators()[0].color == color);
    CHECK(editor.documentToLevel().elevatorAt(movedElevator) != nullptr);

    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(path, false));
    CHECK(loaded.elevators() == editor.elevators());
    CHECK(loaded.documentLayers()[1][1][4] ==
        tileTypeToChar(TileType::Elevator));

    // Erasing a linked plate unlinks it; painting over the elevator removes
    // its record, and undo brings both back.
    CHECK(loaded.setCell(plate, TileType::Air));
    CHECK(loaded.linkedPressurePlates(color).empty());
    CHECK(loaded.setCell(movedElevator, TileType::Wall));
    CHECK(loaded.elevators().empty());
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.elevators().size() == 1);
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.elevators() == editor.elevators());
}

void testMinecartRequiresStopAndPersistsRouteDirection()
{
    TEST("minecartRequiresStopAndPersistsRouteDirection");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(7, 3, false);

    const GridPosition3 plate { 1, 1, 1 };
    const GridPosition3 start { 3, 1, 1 };
    const GridPosition3 destination { 5, 1, 1 };
    CHECK(editor.setCell(plate, TileType::PressurePlate));
    CHECK(!editor.setCell(start, TileType::Minecart));
    CHECK(editor.setCell(start, TileType::RailStopEastWest));
    CHECK(editor.setCell(start, TileType::Minecart));
    CHECK(editor.minecarts().size() == 1);
    CHECK(editor.documentPlateAt(start) == TileType::RailStopEastWest);
    CHECK(editor.minecarts()[0].initialDirection == 1);
    CHECK(editor.setMinecartInitialDirection(0, 3));
    CHECK(!editor.setMinecartInitialDirection(0, 0));
    CHECK(editor.minecarts()[0].initialDirection == 3);
    const Vec3 color = editor.minecarts()[0].color;
    CHECK(editor.linkedPressurePlates(color) == Plates { plate });

    CHECK(editor.setCell({ 4, 1, 1 }, TileType::RailStraightEastWest));
    CHECK(editor.setCell(destination, TileType::RailStopEastWest));
    CHECK(editor.beginMove(start));
    CHECK(editor.moveObject(destination));
    CHECK(editor.minecarts()[0].cell == destination);
    CHECK(editor.minecarts()[0].initialDirection == 3);
    CHECK(editor.minecarts()[0].color == color);
    CHECK(editor.documentPlateAt(start) == TileType::RailStopEastWest);
    CHECK(editor.documentPlateAt(destination) == TileType::RailStopEastWest);

    const Level level = editor.documentToLevel();
    CHECK(level.minecartAt(destination) != nullptr);
    CHECK(level.minecartAt(destination)->pressurePlates == Plates { plate });

    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(path, false));
    CHECK(loaded.minecarts() == editor.minecarts());

    // Moving onto a differently oriented stop keeps the cart record but
    // chooses a valid exit for the new rail orientation.
    const GridPosition3 verticalDestination { 5, 2, 1 };
    CHECK(loaded.setCell(verticalDestination, TileType::RailStopNorthSouth));
    CHECK(loaded.beginMove(destination));
    CHECK(loaded.moveObject(verticalDestination));
    CHECK(loaded.minecarts()[0].cell == verticalDestination);
    CHECK(loaded.minecarts()[0].initialDirection == 0);
    CHECK(loaded.documentToLevel().minecartAt(verticalDestination) != nullptr);

    // Replacing the stop beneath the cart replaces the whole invalid stack,
    // so an editor document can never retain a cart without a stop.
    CHECK(loaded.setCell(verticalDestination, TileType::PressurePlate));
    CHECK(loaded.minecarts().empty());
    CHECK(loaded.documentPlateAt(verticalDestination) == TileType::PressurePlate);
}

void testTileValidationAndMultipleHeroPlacement()
{
    TEST("tileValidationAndMultipleHeroPlacement");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);

    editor.setCell({ 2, 1, 1 }, TileType::Ladder);
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Air));
    CHECK(editor.status().find("Ladders must") != std::string::npos);

    editor.setCell({ 1, 1, 1 }, TileType::Ground);
    editor.setCell({ 2, 1, 1 }, TileType::Ladder);
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Ladder));

    editor.setCell({ 3, 2, 1 }, TileType::Rogue);
    editor.setCell({ 2, 2, 1 }, TileType::Knight);
    int heroCount = 0;
    for (const auto& layer : editor.documentLayers()) {
        for (const std::string& row : layer) {
            heroCount += static_cast<int>(std::ranges::count(
                row, tileTypeToChar(TileType::Rogue)));
            heroCount += static_cast<int>(std::ranges::count(
                row, tileTypeToChar(TileType::Knight)));
        }
    }
    CHECK(heroCount == 3);
    CHECK(editor.documentLayers()[1][2][3] == tileTypeToChar(TileType::Rogue));
    CHECK(editor.documentLayers()[1][2][2] == tileTypeToChar(TileType::Knight));
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
        tileTypeToChar(TileType::Rogue));
    CHECK(editor.status() == "Added layer below.");

    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentDepth() == 2);
    CHECK(editor.activeLayer() == 0);
    CHECK(editor.waterLayer() == 0U);
    CHECK(editor.documentLayers()[0][0][0] ==
        tileTypeToChar(TileType::Ground));
    CHECK(editor.documentLayers()[1][0][0] ==
        tileTypeToChar(TileType::Rogue));
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

void testCharacterSelectionPersistsAndIsUndoable()
{
    TEST("characterSelectionPersistsAndIsUndoable");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(3, 2, false);
    CHECK(editor.character() == CharacterType::Rogue);

    editor.setCharacter(CharacterType::Knight);
    CHECK(editor.character() == CharacterType::Knight);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.character() == CharacterType::Rogue);

    editor.setCharacter(CharacterType::Druid);
    CHECK(editor.character() == CharacterType::Druid);
    const std::filesystem::path sourcePath =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(sourcePath));
    CHECK(readFile(sourcePath).find("@character") == std::string::npos);
    CHECK(readFile(sourcePath).find(tileTypeToChar(TileType::Druid)) !=
        std::string::npos);

    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(sourcePath));
    CHECK(loaded.character() == CharacterType::Druid);
    CHECK(loaded.documentToLevel().character() == CharacterType::Druid);

    loaded.setCharacter(CharacterType::Witch);
    CHECK(loaded.character() == CharacterType::Witch);
    CHECK(loaded.saveDocument(sourcePath));
    LevelEditor witch = makeEditor(project);
    CHECK(witch.loadDocument(sourcePath));
    CHECK(witch.character() == CharacterType::Witch);
    CHECK(witch.documentToLevel().character() == CharacterType::Witch);
}

void testAtomicSaveFailuresPreserveCommittedFilesAndExposeMirrorStaleness()
{
    TEST("atomicSaveFailuresPreserveCommittedFilesAndExposeMirrorStaleness");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(5, 4, false);

    const std::filesystem::path sourcePath =
        project.source / "level0" / "screen0.scr";
    const std::filesystem::path runtimePath =
        project.runtime / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(sourcePath));
    const std::string originalSource = readFile(sourcePath);
    const std::string originalMirror = readFile(runtimePath);

    editor.setCell({ 2, 2, 1 }, TileType::Wall);
    const std::filesystem::path sourceTemporary =
        sourcePath.string() + ".tmp";
    std::filesystem::create_directories(sourceTemporary);
    std::ofstream(sourceTemporary / "blocker.txt") << "blocked";

    const LevelEditor::SaveResult sourceFailure =
        editor.saveDocument(sourcePath);
    CHECK(sourceFailure.outcome == LevelEditor::SaveResult::Outcome::Failed);
    CHECK(!sourceFailure.sourceSaved());
    CHECK(!sourceFailure.mirrorStale());
    CHECK(readFile(sourcePath) == originalSource);
    CHECK(readFile(runtimePath) == originalMirror);
    CHECK(editor.dirty());
    CHECK(editor.status().find("Failed to save") != std::string::npos);

    std::filesystem::remove_all(sourceTemporary);
    CHECK(editor.saveDocument(sourcePath));
    const std::string sourceBeforeMirrorFailure = readFile(sourcePath);
    const std::string mirrorBeforeMirrorFailure = readFile(runtimePath);

    editor.setCell({ 3, 2, 1 }, TileType::Decorative);
    const std::filesystem::path mirrorTemporary =
        runtimePath.string() + ".tmp";
    std::filesystem::create_directories(mirrorTemporary);
    std::ofstream(mirrorTemporary / "blocker.txt") << "blocked";

    const LevelEditor::SaveResult mirrorFailure =
        editor.saveDocument(sourcePath);
    CHECK(mirrorFailure.outcome ==
        LevelEditor::SaveResult::Outcome::SourceSavedMirrorStale);
    CHECK(!mirrorFailure.succeeded());
    CHECK(mirrorFailure.sourceSaved());
    CHECK(mirrorFailure.mirrorStale());
    CHECK(readFile(sourcePath) != sourceBeforeMirrorFailure);
    CHECK(readFile(runtimePath) == mirrorBeforeMirrorFailure);
    CHECK(editor.loadedDocumentPath() == sourcePath);
    CHECK(editor.dirty());
    CHECK(editor.status().find("Saved source") != std::string::npos);

    std::filesystem::remove_all(mirrorTemporary);
    const LevelEditor::SaveResult retry = editor.saveDocument(sourcePath);
    CHECK(retry.succeeded());
    CHECK(!retry.mirrorStale());
    CHECK(readFile(runtimePath) == readFile(sourcePath));
    CHECK(!editor.dirty());

    std::filesystem::create_directories(mirrorTemporary);
    std::ofstream(mirrorTemporary / "blocker.txt") << "blocked";
    const LevelEditor::SaveResult cleanMirrorFailure =
        editor.saveDocument(sourcePath);
    CHECK(cleanMirrorFailure.mirrorStale());
    CHECK(editor.dirty());
    std::filesystem::remove_all(mirrorTemporary);
    CHECK(editor.saveDocument(sourcePath));
    CHECK(!editor.dirty());
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

void testScreenRenumberingPreservesDraftIdentity()
{
    TEST("screenRenumberingPreservesDraftIdentity");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    editor.setRequestedSize(5, 4);
    editor.addLevelAt(0);
    std::vector<LevelEditor::LevelDirectory> levels =
        editor.collectLevelDirectories();
    editor.addScreenAt(levels[0], 1);
    levels = editor.collectLevelDirectories();

    const std::filesystem::path originalFirst = levels[0].screens[0].path;
    const std::filesystem::path originalSecond = levels[0].screens[1].path;
    CHECK(editor.openDocument(originalSecond));
    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    CHECK(editor.openDocument(originalFirst));
    CHECK(editor.hasInProgressDraft(originalSecond));

    editor.addScreenAt(levels[0], 0);
    levels = editor.collectLevelDirectories();
    const std::filesystem::path shiftedFirst = levels[0].screens[1].path;
    const std::filesystem::path shiftedSecond = levels[0].screens[2].path;

    CHECK(!editor.hasInProgressDraft(shiftedFirst));
    CHECK(editor.hasInProgressDraft(shiftedSecond));
    CHECK(editor.openDocument(shiftedFirst));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.openDocument(shiftedSecond));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.loadedDocumentPath() == shiftedSecond);
}

void testActiveDraftFollowsLevelRenumbering()
{
    TEST("activeDraftFollowsLevelRenumbering");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    editor.setRequestedSize(5, 4);
    editor.addLevelAt(0);
    editor.addLevelAt(1);
    std::vector<LevelEditor::LevelDirectory> levels =
        editor.collectLevelDirectories();
    CHECK(editor.openDocument(levels[1].screens[0].path));
    editor.setCell({ 1, 1, 1 }, TileType::Wall);

    editor.addLevelAt(0);
    levels = editor.collectLevelDirectories();
    const std::filesystem::path shifted = levels[2].screens[0].path;
    CHECK(editor.hasInProgressDraft(shifted));
    CHECK(editor.openDocument(shifted));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.loadedDocumentPath() == shifted);
}

void testDeleteScreenRestoresShiftedDraft()
{
    TEST("deleteScreenRestoresShiftedDraft");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    editor.setRequestedSize(5, 4);
    editor.addLevelAt(0);
    std::vector<LevelEditor::LevelDirectory> levels =
        editor.collectLevelDirectories();
    editor.addScreenAt(levels[0], 1);
    levels = editor.collectLevelDirectories();
    editor.addScreenAt(levels[0], 2);
    levels = editor.collectLevelDirectories();

    CHECK(editor.openDocument(levels[0].screens[2].path));
    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    CHECK(editor.openDocument(levels[0].screens[0].path));
    CHECK(editor.hasInProgressDraft(levels[0].screens[2].path));

    editor.deleteScreen(levels[0], 0);
    levels = editor.collectLevelDirectories();
    CHECK(levels[0].screens.size() == 2);
    const std::filesystem::path shiftedDraft = levels[0].screens[1].path;
    CHECK(editor.hasInProgressDraft(shiftedDraft));
    CHECK(editor.openDocument(shiftedDraft));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));
}

void testStructuralChangesPublishSplatAndMusicAssociations()
{
    TEST("structuralChangesPublishSplatAndMusicAssociations");
    TemporaryProject project;
    const std::filesystem::path sourceAssets = project.root / "source-assets";
    const std::filesystem::path runtimeAssets = project.root / "runtime-assets";
    const std::filesystem::path sourceLevels = sourceAssets / "levels";
    const std::filesystem::path runtimeLevels = runtimeAssets / "levels";
    std::filesystem::create_directories(sourceLevels);
    std::filesystem::create_directories(runtimeLevels);

    LevelEditor bootstrap;
    bootstrap.initialize(sourceLevels, runtimeLevels, 0, 0);
    bootstrap.setRequestedSize(4, 3);
    bootstrap.addLevelAt(0);
    auto levels = bootstrap.collectLevelDirectories();
    bootstrap.addScreenAt(levels[0], 1);

    const std::string manifestText = R"json({
      "format": 1,
      "textures": [
        { "name": "GroundSplatMap0_0", "path": "maps/first.png", "colorSpace": "linear" },
        { "name": "GroundSplatMap0_1", "path": "maps/second.png", "colorSpace": "linear" }
      ],
      "models": [{ "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" }],
      "animations": [
        { "name": "Idle", "path": "hero.glb", "role": "player-idle" },
        { "name": "Move", "path": "hero.glb", "role": "player-move" },
        { "name": "Push", "path": "hero.glb", "role": "player-push" },
        { "name": "Death", "path": "hero.glb", "role": "player-death" },
        { "name": "DeadIdle", "path": "hero.glb", "role": "player-dead-idle" }
      ],
      "music": [{ "level": 0, "file": "music/first.ogg" }]
    })json";
    const std::filesystem::path sourceManifest = sourceAssets / "manifest.json";
    const std::filesystem::path runtimeManifest = runtimeAssets / "manifest.json";
    std::ofstream(sourceManifest, std::ios::binary) << manifestText;
    std::ofstream(runtimeManifest, std::ios::binary) << manifestText;
    std::ofstream(runtimeAssets / "content.index", std::ios::binary)
        << "format 1\ngame-version editor-test\n";

    LevelEditor editor;
    editor.initialize(sourceLevels, runtimeLevels, 0, 0,
        sourceManifest, runtimeManifest);
    levels = editor.collectLevelDirectories();
    editor.addScreenAt(levels[0], 0);
    AssetManifest manifest = AssetManifest::loadFromFile(sourceManifest);
    CHECK(manifest.findTextureIdByName("GroundSplatMap0_0").isNone());
    CHECK(manifest.textureIdByName("GroundSplatMap0_1").index() == 0);
    CHECK(manifest.textureIdByName("GroundSplatMap0_2").index() == 1);
    CHECK(manifest.musicForLevel(0) != nullptr);
    CHECK(readFile(sourceManifest) == readFile(runtimeManifest));

    editor.addLevelAt(0);
    manifest = AssetManifest::loadFromFile(sourceManifest);
    CHECK(manifest.textureIdByName("GroundSplatMap1_1").index() == 0);
    CHECK(manifest.textureIdByName("GroundSplatMap1_2").index() == 1);
    CHECK(manifest.musicForLevel(1) != nullptr);
    CHECK(manifest.musicForLevel(0) == nullptr);

    levels = editor.collectLevelDirectories();
    editor.deleteLevel(levels[0]);
    manifest = AssetManifest::loadFromFile(sourceManifest);
    CHECK(manifest.textureIdByName("GroundSplatMap0_1").index() == 0);
    CHECK(manifest.musicForLevel(0) != nullptr);

    levels = editor.collectLevelDirectories();
    editor.deleteLevel(levels[0]);
    manifest = AssetManifest::loadFromFile(sourceManifest);
    CHECK(manifest.findTextureIdByName("GroundSplatMap0_1").isNone());
    CHECK(manifest.musicForLevel(0) == nullptr);
    const auto deleted = editor.collectDeletedLevels();
    CHECK(!deleted.empty());
    editor.restoreDeletedLevel(deleted.back().path);
    manifest = AssetManifest::loadFromFile(sourceManifest);
    const RenderTexture restored =
        manifest.textureIdByName("GroundSplatMap0_1");
    CHECK(manifest.textures()[restored.index()].path == "maps/first.png");
    CHECK(manifest.musicForLevel(0) &&
        *manifest.musicForLevel(0) == "music/first.ogg");
    CHECK(readFile(sourceManifest) == readFile(runtimeManifest));
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

void testBrowserSnapshotsRefreshAfterProjectChanges()
{
    TEST("browserSnapshotsRefreshAfterProjectChanges");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    const auto now = std::chrono::steady_clock::now();
    CHECK(editor.levelBrowserSnapshot(now).empty());
    CHECK(editor.deletedLevelBrowserSnapshot(now).empty());

    editor.addLevelAt(0);
    CHECK(editor.levelBrowserSnapshot(now).size() == 1);
    auto level = editor.levelBrowserSnapshot(now).front();
    editor.renameLevel(level, "Cached garden");
    CHECK(editor.levelBrowserSnapshot(now).front().name == "Cached garden");
    editor.renameScreen(level, 0, "First steps");
    CHECK(editor.levelBrowserSnapshot(now).front().screens.front().name == "First steps");
    editor.addScreenAt(level, 1);
    CHECK(editor.levelBrowserSnapshot(now).front().screens.size() == 2);
    level = editor.levelBrowserSnapshot(now).front();
    editor.deleteScreen(level, 0);
    CHECK(editor.levelBrowserSnapshot(now).front().screens.size() == 1);
    editor.deleteLevel(level);
    CHECK(editor.levelBrowserSnapshot(now).empty());
    CHECK(editor.deletedLevelBrowserSnapshot(now).size() == 1);
    auto deleted = editor.deletedLevelBrowserSnapshot(now).front().path;
    editor.restoreDeletedLevel(deleted);
    CHECK(editor.levelBrowserSnapshot(now).size() == 1);
    CHECK(editor.deletedLevelBrowserSnapshot(now).empty());

    // Saving a new screen invalidates even without a project transaction.
    CHECK(editor.saveDocument(project.source / "level0/screen1.scr"));
    CHECK(editor.levelBrowserSnapshot(now).front().screens.size() == 2);
    // A partial save still publishes a source screen and must refresh the UI
    // even though SaveResult's boolean conversion reports failure.
    const auto mirrorBlocker = project.runtime / "level0/screen2.scr.tmp";
    std::filesystem::create_directories(mirrorBlocker);
    std::ofstream(mirrorBlocker / "blocker.txt") << "blocked";
    const auto partialSave = editor.saveDocument(project.source / "level0/screen2.scr");
    CHECK(partialSave.sourceSaved());
    CHECK(partialSave.mirrorStale());
    CHECK(editor.levelBrowserSnapshot(now).front().screens.size() == 3);
    std::filesystem::remove_all(mirrorBlocker);
    level = editor.levelBrowserSnapshot(now).front();
    editor.deleteLevel(level);
    deleted = editor.deletedLevelBrowserSnapshot(now).front().path;
    CHECK(editor.permanentlyDelete(deleted));
    CHECK(editor.deletedLevelBrowserSnapshot(now).empty());

    const auto alternate = project.root / "alternate";
    std::filesystem::create_directories(alternate / "level0");
    std::ofstream(alternate / "level0/screen0.scr") << "GG\nGG\n";
    CHECK(editor.setBrowserRoot(alternate));
    CHECK(editor.levelBrowserSnapshot(now).size() == 1);
    editor.initialize(project.source, project.runtime, 0, 0);
    CHECK(editor.levelBrowserSnapshot(now).empty());
}

void testBrowserSnapshotsBoundExternalRefresh()
{
    TEST("browserSnapshotsBoundExternalRefresh");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.addLevelAt(0);
    const auto now = std::chrono::steady_clock::now();
    CHECK(editor.levelBrowserSnapshot(now).front().name.empty());
    const auto* retained = editor.levelBrowserSnapshot(now).data();

    std::ofstream(project.source / "level0/metadata.json") <<
        R"({"format":1,"name":"Outside rename","screens":["Outside screen"]})";
    std::ofstream(project.source / "level0/screen1.scr") << "GG\nGG\n";
    std::filesystem::create_directories(project.source / "level1");
    std::ofstream(project.source / "level1/screen0.scr") << "GG\nGG\n";
    const auto beforeRefresh = now + LevelEditor::browserRefreshInterval -
        std::chrono::milliseconds(1);
    CHECK(editor.levelBrowserSnapshot(beforeRefresh).data() == retained);
    CHECK(editor.levelBrowserSnapshot(beforeRefresh).front().name.empty());
    CHECK(editor.levelBrowserSnapshot(beforeRefresh).front().screens.size() == 1);
    CHECK(editor.levelBrowserSnapshot(beforeRefresh).size() == 1);
    // Fresh command reads do not inherit the presentation's refresh delay.
    CHECK(editor.collectLevelDirectories().front().screens.size() == 2);
    const auto refresh = now + LevelEditor::browserRefreshInterval;
    CHECK(editor.levelBrowserSnapshot(refresh).front().name == "Outside rename");
    CHECK(editor.levelBrowserSnapshot(refresh).front().screens.size() == 2);
    CHECK(editor.levelBrowserSnapshot(refresh).size() == 2);
    CHECK(editor.levelBrowserSnapshot(refresh).front().screens.front().name == "Outside screen");

    std::filesystem::remove(project.source / "level0/screen1.scr");
    std::filesystem::remove_all(project.source / "level1");
    CHECK(editor.levelBrowserSnapshot(refresh + LevelEditor::browserRefreshInterval)
        .front().screens.size() == 1);
    CHECK(editor.levelBrowserSnapshot(refresh + LevelEditor::browserRefreshInterval).size() == 1);
    editor.deleteLevel(editor.collectLevelDirectories().front());
    const auto deleted = editor.deletedLevelBrowserSnapshot(refresh).front().path;
    std::filesystem::remove_all(deleted);
    CHECK(editor.deletedLevelBrowserSnapshot(refresh + LevelEditor::browserRefreshInterval).empty());
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

    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    CHECK(editor.dirty());
    editor.deleteLevel(levels[0]);
    levels = editor.collectLevelDirectories();
    std::vector<LevelEditor::LevelDirectory> deleted = editor.collectDeletedLevels();
    CHECK(levels.size() == 1);
    CHECK(levels[0].index == 0);
    CHECK(deleted.size() == 1);
    CHECK(deleted[0].name == "Clockwork Garden");
    CHECK(deleted[0].screens[0].name == "The Long Hall");
    CHECK(editor.hasInProgressDraft(deleted[0].screens[0].path));
    CHECK(editor.openDocument(deleted[0].screens[0].path));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));

    editor.restoreDeletedLevel(deleted[0].path);
    levels = editor.collectLevelDirectories();
    CHECK(levels.size() == 2);
    CHECK(levels[1].index == 1);
    CHECK(levels[1].name == "Clockwork Garden");
    CHECK(editor.collectDeletedLevels().empty());
    CHECK(editor.loadedDocumentPath() == levels[1].screens[0].path);
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));

    editor.deleteLevel(levels[1]);
    deleted = editor.collectDeletedLevels();
    CHECK(deleted.size() == 1);
    const std::filesystem::path permanentlyDeletedScreen =
        deleted[0].screens[0].path;
    CHECK(editor.hasInProgressDraft(permanentlyDeletedScreen));
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
    CHECK(!editor.hasInProgressDraft(permanentlyDeletedScreen));
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
    CHECK(editor.documentLayers()[1][0][0] == tileTypeToChar(TileType::Rogue));
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
        tileTypeToChar(TileType::Rogue));
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
        tileTypeToChar(TileType::Rogue));
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));

    editor.setCell({ 3, 2, 0 }, TileType::Decorative);
    CHECK(editor.documentWidth() == 4);
    CHECK(editor.documentHeight() == 3);
    CHECK(editor.documentLayers()[0][2][2] ==
        tileTypeToChar(TileType::Air));
    CHECK(editor.documentLayers()[1][0][0] ==
        tileTypeToChar(TileType::Rogue));
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
    const std::filesystem::path activePath = editor.loadedDocumentPath();
    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    CHECK(editor.dirty());

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
    CHECK(editor.loadedDocumentPath() == activePath);
    CHECK(editor.documentLayers()[1][1][1] ==
        tileTypeToChar(TileType::Wall));
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
    loaded.setCell({ 0, 1, 1 }, TileType::Wall);
    CHECK(loaded.dirty());

    std::vector<LevelEditor::LevelDirectory> levels =
        loaded.collectLevelDirectories();
    loaded.addScreenAt(levels[0], 0);
    const Level shifted = Level::loadFromFile(
        project.source / "overworld.scr");
    CHECK(shifted.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 1 }));
    CHECK(std::filesystem::exists(project.runtime / "overworld.scr"));
    CHECK(loaded.openDocument(overworld));
    CHECK(loaded.selectors()[0].target ==
        std::optional<LevelLocation>({ .level = 0, .screen = 1 }));
    CHECK(loaded.documentLayers()[1][1][0] ==
        tileTypeToChar(TileType::Wall));

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

void testPortalColorGroups()
{
    TEST("portalColorGroupsSurviveEditingAndSave");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(6, 3, false);
    const Vec3 blue { 0.2f, 0.4f, 1.0f };
    const Vec3 green { 0.2f, 1.0f, 0.3f };
    const GridPosition3 first { 1, 0, 1 };
    const GridPosition3 second { 4, 1, 1 };
    const GridPosition3 moved { 4, 2, 1 };
    editor.setActiveLinkColor(blue);
    CHECK(editor.setCell(first, TileType::PortalNorth));
    CHECK(editor.setCell(second, TileType::PortalSouth));
    CHECK(editor.setCell(first, TileType::PortalEast));
    CHECK(editor.portals().size() == 2);
    CHECK(editor.documentToLevel().portalAt(first)->color == blue);
    CHECK(
        editor.documentToLevel().portalCrossing(first, { 1, 0 })->direction ==
        GridPosition({ 0, -1 }));
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentToLevel().plateAt(first) == TileType::PortalNorth);
    CHECK(editor.documentToLevel().portalAt(first)->color == blue);
    CHECK(editor.portals().size() == 2);
    CHECK(editor.documentToLevel().portalExit(first) == second);
    CHECK(editor.linkGroups()[0].portals.size() == 2);
    CHECK(editor.beginMove(second));
    CHECK(editor.moveObject(moved));
    CHECK(editor.documentToLevel().portalExit(first) == moved);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentToLevel().portalExit(first) == second);
    CHECK(editor.setLinkColor(first, green));
    CHECK(!editor.documentToLevel().portalExit(first));
    CHECK(editor.recolorLinkGroup(green, blue));
    CHECK(editor.documentToLevel().portalExit(first) == second);
    CHECK(editor.setCell(first, TileType::Rock));
    CHECK(editor.setLinkColor(first, green));
    const auto stacked = editor.documentToLevel();
    CHECK(stacked.portalAt(first)->color == blue);
    CHECK(stacked.movableLinkColor(0) == green);
    const auto path = project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    CHECK(readFile(path).find("@portal ") != std::string::npos);
    LevelEditor loaded = makeEditor(project);
    CHECK(loaded.loadDocument(path, false));
    CHECK(loaded.portals() == editor.portals());
    CHECK(loaded.documentToLevel().portalExit(first) == second);
    loaded.setActiveLayer(1);
    loaded.addLayerBelow();
    CHECK(
        loaded.documentToLevel().portalExit({ 1, 0, 2 }) ==
        GridPosition3({ 4, 1, 2 }));
    CHECK(loaded.tryUndoEdit());
    loaded.resizeDocument(3, 3);
    CHECK(loaded.portals().size() == 1);
    CHECK(loaded.tryUndoEdit());
    CHECK(loaded.portals().size() == 2);
    CHECK(loaded.setCell(second, TileType::Air));
    CHECK(loaded.portals().size() == 1);
    CHECK(!loaded.documentToLevel().portalExit(first));
}


void testLockPlateEditor()
{
    TEST("lockPlateEditor");
    TemporaryProject project;
    auto editor = makeEditor(project);
    editor.newDocument(6, 3, false);
    const GridPosition3 lock { 2, 1, 1 }, pressure { 1, 1, 1 };
    CHECK(editor.setCell(lock, TileType::LockPlate));
    CHECK(editor.setCell(pressure, TileType::PressurePlate));
    CHECK(editor.setCell(lock, TileType::Rock));
    CHECK(editor.documentPlateAt(lock) == TileType::LockPlate);
    CHECK(editor.setLockPlateStartEnabled(0, true));
    CHECK(editor.tryUndoEdit());
    CHECK(!editor.lockPlates()[0].startEnabled);
    CHECK(editor.tryRedoEdit());
    const auto definition = editor.documentDefinition();
    CHECK(definition.lockPlates[0].startEnabled);
    CHECK(definition.lockPlates[0].pressurePlates == std::vector<GridPosition3> { pressure });
    CHECK(editor.linkGroups()[0].lockPlates == std::vector<GridPosition3> { lock });
    CHECK(Level::parseDefinition(Level::serializeDefinition(definition), "editor lock").lockPlates == definition.lockPlates);
    CHECK(editor.setCell(lock, TileType::Wall));
    CHECK(editor.lockPlates().empty());
    CHECK(editor.tryUndoEdit());
    CHECK(editor.lockPlates()[0].startEnabled);
}

} // namespace

void testStrokeIsOneUndoStepAndRedoReplaysIt()
{
    TEST("strokeIsOneUndoStepAndRedoReplaysIt");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(6, 3, false);
    const char wall = tileTypeToChar(TileType::Wall);
    const char air = tileTypeToChar(TileType::Air);

    editor.setSelectedTile(TileType::Wall);
    CHECK(!editor.canRedo());
    CHECK(editor.beginStroke());
    CHECK(!editor.beginStroke());
    CHECK(editor.strokeActive());
    for (int x = 1; x < 5; ++x) {
        CHECK(editor.setCell({ x, 1, 1 }, TileType::Wall));
    }
    // Repainting a cell the stroke already covers changes nothing.
    CHECK(!editor.paintCell({ 2, 1, 1 }));
    CHECK(editor.canUndo());
    CHECK(editor.endStroke());
    CHECK(!editor.strokeActive());
    CHECK(editor.status().find("4 cells") != std::string::npos);

    CHECK(editor.tryUndoEdit());
    for (int x = 1; x < 5; ++x) {
        CHECK(editor.documentLayers()[1][1][static_cast<std::size_t>(x)] ==
            air);
    }
    CHECK(editor.canRedo());
    CHECK(editor.tryRedoEdit());
    for (int x = 1; x < 5; ++x) {
        CHECK(editor.documentLayers()[1][1][static_cast<std::size_t>(x)] ==
            wall);
    }
    CHECK(!editor.tryRedoEdit());

    // An empty stroke records nothing.
    CHECK(editor.beginStroke());
    CHECK(!editor.endStroke());
    CHECK(editor.tryUndoEdit());
    CHECK(editor.documentLayers()[1][1][1] == air);

    // Undo during an open stroke closes it first, then undoes all of it.
    CHECK(editor.tryRedoEdit());
    CHECK(editor.beginStroke());
    CHECK(editor.eraseCell({ 1, 1, 1 }));
    CHECK(editor.eraseCell({ 2, 1, 1 }));
    CHECK(editor.tryUndoEdit());
    CHECK(!editor.strokeActive());
    CHECK(editor.documentLayers()[1][1][1] == wall);
    CHECK(editor.documentLayers()[1][1][2] == wall);

    // A new edit abandons the redo branch.
    CHECK(editor.canRedo());
    CHECK(editor.setCell({ 5, 2, 1 }, TileType::Wall));
    CHECK(!editor.canRedo());
    CHECK(!editor.tryRedoEdit());
}

void testRedoSurvivesDraftSwitchingAndFailedMoves()
{
    TEST("redoSurvivesDraftSwitchingAndFailedMoves");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    const std::filesystem::path first =
        project.source / "level0" / "screen0.scr";
    const std::filesystem::path second =
        project.source / "level0" / "screen1.scr";
    editor.newDocument(5, 4, false);
    CHECK(editor.saveDocument(first));
    editor.newDocument(5, 4, false);
    CHECK(editor.saveDocument(second));

    CHECK(editor.openDocument(first));
    CHECK(editor.setCell({ 1, 1, 0 }, TileType::Ladder));
    editor.setCell({ 2, 2, 1 }, TileType::Wall);
    editor.setCell({ 3, 2, 1 }, TileType::Wall);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.canRedo());

    CHECK(editor.openDocument(second));
    CHECK(!editor.canRedo());
    CHECK(editor.openDocument(first));
    CHECK(editor.canRedo());
    CHECK(editor.tryRedoEdit());
    CHECK(editor.documentLayers()[1][2][3] ==
        tileTypeToChar(TileType::Wall));

    // A move whose placement fails (a ladder needs ground beside it) is
    // rolled back without touching undo or redo.
    CHECK(editor.tryUndoEdit());
    CHECK(editor.beginMove({ 1, 1, 0 }));
    CHECK(!editor.moveObject({ 4, 3, 1 }));
    CHECK(editor.documentLayers()[0][1][1] ==
        tileTypeToChar(TileType::Ladder));
    CHECK(editor.canRedo());
    CHECK(editor.tryRedoEdit());
    CHECK(editor.documentLayers()[1][2][3] ==
        tileTypeToChar(TileType::Wall));
}

void testRedoFollowsScreenRenumbering()
{
    TEST("redoFollowsScreenRenumbering");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);

    editor.setRequestedSize(5, 4);
    editor.addLevelAt(0);
    std::vector<LevelEditor::LevelDirectory> levels =
        editor.collectLevelDirectories();
    editor.addScreenAt(levels[0], 1);
    levels = editor.collectLevelDirectories();
    const std::filesystem::path originalFirst = levels[0].screens[0].path;
    const std::filesystem::path originalSecond = levels[0].screens[1].path;

    CHECK(editor.openDocument(originalSecond));
    editor.setCell({ 1, 1, 1 }, TileType::Wall);
    editor.setCell({ 2, 1, 1 }, TileType::Wall);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.openDocument(originalFirst));

    editor.addScreenAt(levels[0], 0);
    levels = editor.collectLevelDirectories();
    const std::filesystem::path shiftedSecond = levels[0].screens[2].path;
    CHECK(editor.openDocument(shiftedSecond));
    CHECK(editor.canRedo());
    CHECK(editor.tryRedoEdit());
    CHECK(editor.documentLayers()[1][1][2] ==
        tileTypeToChar(TileType::Wall));
    CHECK(editor.loadedDocumentPath() == shiftedSecond);
    // The redone snapshot carries the remapped identity too.
    CHECK(editor.tryUndoEdit());
    CHECK(editor.tryRedoEdit());
    CHECK(editor.loadedDocumentPath() == shiftedSecond);
}

void testEyedropperRecentTilesAndToolCycling()
{
    TEST("eyedropperRecentTilesAndToolCycling");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);

    editor.setSelectedTile(TileType::Wall);
    editor.setSelectedTile(TileType::Rock);
    editor.setSelectedTile(TileType::Wall);
    CHECK((editor.recentTiles() ==
        std::vector<TileType> { TileType::Rock, TileType::Wall }));
    CHECK(editor.selectRecentTile(1));
    CHECK(editor.selectedTile() == TileType::Wall);
    CHECK(!editor.selectRecentTile(2));

    // Eyedropper reads the top of the picked column.
    editor.setSelectedTile(TileType::Decorative);
    CHECK(editor.paintCell({ 2, 1, 1 }));
    editor.setSelectedTile(TileType::Ice);
    const std::optional<TileType> picked = editor.pickTile({ 2, 1, 0 });
    CHECK(picked == TileType::Decorative);
    CHECK(editor.selectedTile() == TileType::Decorative);
    CHECK(editor.pickTile({ 1, 1, 0 }) == TileType::Ground);

    editor.setLayerLocked(true);
    editor.setActiveLayer(1);
    CHECK(!editor.pickTile({ 1, 1, 0 }).has_value());
    CHECK(editor.selectedTile() == TileType::Ground);

    for (int index = 0; index < 20; ++index) {
        editor.setSelectedTile(
            index % 2 == 0 ? TileType::Wall : TileType::Rock);
    }
    CHECK(editor.recentTiles().size() <= LevelEditor::recentTileCapacity);

    editor.setTool(LevelEditor::Tool::Tiles);
    editor.cycleTool();
    CHECK(editor.tool() == LevelEditor::Tool::Decorations);
    // Selectors only exist on overworld screens.
    editor.cycleTool();
    CHECK(editor.tool() == LevelEditor::Tool::Tiles);
}

void testPlayFromCursorMovesTheFirstHeroWithoutEditing()
{
    TEST("playFromCursorMovesTheFirstHeroWithoutEditing");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(5, 4, false);
    const Level::LayerRows before = editor.documentLayers();

    const std::optional<Level> level =
        editor.beginDraftPlayback(nullptr, GridPosition3 { 3, 2, 0 });
    CHECK(level.has_value());
    if (level) {
        CHECK((level->playerStart() == GridPosition3 { 3, 2, 1 }));
        CHECK(level->playerStarts().size() == 1);
    }
    CHECK(editor.playingDraft());
    CHECK(editor.documentLayers() == before);
    editor.setPlayingDraft(false);

    // Pointing at the hero's own column keeps it on the same layer.
    const std::optional<Level> same =
        editor.beginDraftPlayback(nullptr, GridPosition3 { 0, 0, 1 });
    CHECK(same.has_value());
    if (same) {
        CHECK((same->playerStart() == GridPosition3 { 0, 0, 1 }));
    }
    editor.setPlayingDraft(false);

    // Stacked on a wall, the hero starts on top of it.
    editor.setCell({ 4, 3, 1 }, TileType::Wall);
    const std::optional<Level> onWall =
        editor.beginDraftPlayback(nullptr, GridPosition3 { 4, 3, 1 });
    CHECK(onWall.has_value());
    if (onWall) {
        CHECK((onWall->playerStart() == GridPosition3 { 4, 3, 2 }));
    }
    editor.setPlayingDraft(false);

    CHECK(!editor.beginDraftPlayback(nullptr, GridPosition3 { 9, 9, 0 }));
    CHECK(editor.status().find("board") != std::string::npos);
    CHECK(!editor.playingDraft());
    const std::optional<Level> plain = editor.beginDraftPlayback();
    CHECK(plain.has_value());
    if (plain) {
        CHECK((plain->playerStart() == GridPosition3 { 0, 0, 1 }));
    }
}

void testSaveShortcutAndLayerStepping()
{
    TEST("saveShortcutAndLayerStepping");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    editor.newDocument(4, 3, false);
    CHECK(!editor.saveLoadedDocument());
    CHECK(editor.status().find("never been saved") != std::string::npos);

    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    CHECK(editor.saveDocument(path));
    CHECK(editor.beginStroke());
    CHECK(editor.setCell({ 1, 1, 1 }, TileType::Wall));
    CHECK(editor.dirty());
    // Saving closes the open stroke so it still undoes as one step.
    CHECK(editor.saveLoadedDocument());
    CHECK(!editor.strokeActive());
    CHECK(!editor.dirty());
    CHECK(readFile(path).find(tileTypeToChar(TileType::Wall)) !=
        std::string::npos);
    CHECK(editor.canUndo());

    editor.setActiveLayer(0);
    editor.stepActiveLayer(1);
    CHECK(editor.activeLayer() == 1);
    CHECK(editor.status().find("Active layer 2 of 2") != std::string::npos);
    editor.stepActiveLayer(1);
    CHECK(editor.activeLayer() == 1);
    editor.stepActiveLayer(-5);
    CHECK(editor.activeLayer() == 0);

    editor.toggleLayerLock();
    CHECK(editor.layerLocked());
    CHECK(editor.status().find("layer 1") != std::string::npos);
    editor.toggleLayerLock();
    CHECK(!editor.layerLocked());
}

void testReloadFromDiskKeepsDraftsAndIgnoresOwnSaves()
{
    TEST("reloadFromDiskKeepsDraftsAndIgnoresOwnSaves");
    TemporaryProject project;
    LevelEditor editor = makeEditor(project);
    const std::filesystem::path path =
        project.source / "level0" / "screen0.scr";
    editor.newDocument(4, 3, false);
    CHECK(editor.setCell({ 1, 1, 1 }, TileType::Wall));
    CHECK(editor.saveDocument(path));
    CHECK(editor.canUndo());
    // The editor's own save is not an outside change.
    CHECK(!editor.reloadFromDisk());
    CHECK(editor.canUndo());

    // An outside edit replaces a clean document and its stale history.
    std::string text = readFile(path);
    const std::size_t wall = text.find(tileTypeToChar(TileType::Wall));
    CHECK(wall != std::string::npos);
    text[wall] = tileTypeToChar(TileType::Air);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    CHECK(editor.reloadFromDisk());
    CHECK(editor.documentLayers()[1][1][1] == tileTypeToChar(TileType::Air));
    CHECK(!editor.canUndo());
    CHECK(!editor.dirty());

    // Unsaved edits are never replaced.
    CHECK(editor.setCell({ 2, 1, 1 }, TileType::Wall));
    std::ofstream(path, std::ios::binary | std::ios::trunc) << readFile(path) << "\n";
    CHECK(!editor.reloadFromDisk());
    CHECK(editor.documentLayers()[1][1][2] == tileTypeToChar(TileType::Wall));
}

int main()
{
    testLockPlateEditor();
    testPortalColorGroups();
    testDocumentCommandsAndUndo();
    testColorGroupsBecomeExplicitLinks();
    testExplicitLinksBecomeColorGroupsOnLoad();
    testMovableObjectsJoinColorGroupsAndKeepLinksWhenMoved();
    testElevatorStopsPersistAndFollowEditorCommands();
    testGateStartOpenIsAnUndoableGateSetting();
    testMinecartRequiresStopAndPersistsRouteDirection();
    testUnitsAndMirrorsStackOnPlates();
    testTileValidationAndMultipleHeroPlacement();
    testAddLayerBelowShiftsContentAndWaterAndIsUndoable();
    testSaveLoadAndRuntimeMirror();
    testCharacterSelectionPersistsAndIsUndoable();
    testAtomicSaveFailuresPreserveCommittedFilesAndExposeMirrorStaleness();
    testSelectedPathIsSeparateFromTheLoadedDocument();
    testOpeningScreensPreservesIndependentDraftsAndUndoHistory();
    testScreenRenumberingPreservesDraftIdentity();
    testActiveDraftFollowsLevelRenumbering();
    testDeleteScreenRestoresShiftedDraft();
    testStructuralChangesPublishSplatAndMusicAssociations();
    testUndoRestoresTheLoadedDocumentPath();
    testWaterLayerEditingPersistenceAndLayerRenumbering();
    testProjectRenumberDeleteAndRestore();
    testBrowserSnapshotsRefreshAfterProjectChanges();
    testBrowserSnapshotsBoundExternalRefresh();
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
    testStrokeIsOneUndoStepAndRedoReplaysIt();
    testRedoSurvivesDraftSwitchingAndFailedMoves();
    testRedoFollowsScreenRenumbering();
    testEyedropperRecentTilesAndToolCycling();
    testPlayFromCursorMovesTheFirstHeroWithoutEditing();
    testSaveShortcutAndLayerStepping();
    testReloadFromDiskKeepsDraftsAndIgnoresOwnSaves();

    if (failures == 0) {
        std::cout << "LevelEditorTests: " << checks << " checks passed\n";
        return 0;
    }

    std::cerr << "LevelEditorTests: " << failures << " of " << checks << " checks failed\n";
    return 1;
}
