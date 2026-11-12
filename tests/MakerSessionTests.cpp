#include "TestHarness.hpp"

#include "engine/GameplayLoop.hpp"
#include "engine/MakerDraftStore.hpp"
#include "engine/MakerSession.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>

namespace {

using namespace sokoban;

struct TemporaryDraftDirectory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("sokoban_maker_session_tests_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    ~TemporaryDraftDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

TileType tileAt(const MakerSession& session, GridPosition3 cell)
{
    return charToTileType(session.editor().documentLayers()
            .at(static_cast<std::size_t>(cell.z))
            .at(static_cast<std::size_t>(cell.y))
            .at(static_cast<std::size_t>(cell.x)))
        .value();
}

void testStarterIsIndependent()
{
    TEST("makerStarterIsIndependent");
    MakerSession session;
    MakerSession another;
    CHECK(session.editor().documentWidth() == 10);
    CHECK(session.editor().documentHeight() == 8);
    CHECK(session.editor().documentDepth() == 2);
    CHECK(session.editor().loadedDocumentPath().empty());
    CHECK(session.editor().sourceLevelRoot().empty());
    CHECK(session.editor().runtimeLevelRoot().empty());
    CHECK(!session.editor().canUndo());
    CHECK(tileAt(session, { 1, 3, 1 }) == TileType::Rogue);
    CHECK(tileAt(session, { 7, 3, 1 }) == TileType::End);
    CHECK(session.editor().documentToLevel().width() == 10);

    CHECK(session.beginStroke({ 0, 0 }));
    CHECK(session.endStroke());
    CHECK(tileAt(session, { 0, 0, 1 }) == TileType::Wall);
    CHECK(tileAt(another, { 0, 0, 1 }) == TileType::Air);
}

void testStrokeClipsAndGroups()
{
    TEST("makerStrokeClipsAndGroups");
    MakerSession session;
    CHECK(!session.beginStroke({ -1, 0 }));
    CHECK(!session.strokeActive());
    CHECK(!session.previewCell({ 10, 0 }));
    CHECK(!session.previewCell({ 0, 8 }));
    CHECK(session.beginStroke({ 0, 6 }));
    CHECK(session.continueStroke(GridPosition { 3, 6 }));
    CHECK(!session.continueStroke(std::nullopt));
    CHECK(session.continueStroke(GridPosition { 8, 6 }));
    CHECK(session.endStroke());
    CHECK(!session.strokeActive());
    for (int x = 0; x <= 3; ++x) {
        CHECK(tileAt(session, { x, 6, 1 }) == TileType::Wall);
    }
    CHECK(tileAt(session, { 5, 6, 1 }) == TileType::Air);
    CHECK(tileAt(session, { 8, 6, 1 }) == TileType::Wall);
    CHECK(session.undo());
    CHECK(!session.editor().canUndo());
    CHECK(tileAt(session, { 3, 6, 1 }) == TileType::Air);
    CHECK(tileAt(session, { 8, 6, 1 }) == TileType::Air);
    CHECK(session.redo());
    CHECK(tileAt(session, { 3, 6, 1 }) == TileType::Wall);
    CHECK(tileAt(session, { 8, 6, 1 }) == TileType::Wall);
    CHECK(session.editor().documentWidth() == 10);
}

void testBrushAndLayerChanges()
{
    TEST("makerBrushAndLayerChanges");
    MakerSession session;
    session.selectTile(TileType::ConveyorUp);
    session.rotateTile();
    CHECK(session.editor().selectedTile() == TileType::ConveyorRight);
    CHECK(session.beginStroke({ 0, 5 }));
    session.stepLayer(-1);
    CHECK(!session.strokeActive());
    CHECK(session.previewCell({ 0, 5 }) == GridPosition3({ 0, 5, 0 }));
    CHECK(tileAt(session, { 0, 5, 1 }) == TileType::ConveyorRight);
    CHECK(tileAt(session, { 0, 5, 0 }) == TileType::Ground);
    session.setTool(MakerSession::Tool::Erase);
    CHECK(session.beginStroke({ 0, 5 }));
    CHECK(session.endStroke());
    CHECK(tileAt(session, { 0, 5, 0 }) == TileType::Air);
    CHECK(tileAt(session, { 0, 5, 1 }) == TileType::ConveyorRight);
    CHECK(session.undo());
    CHECK(tileAt(session, { 0, 5, 0 }) == TileType::Ground);
    session.selectTile(TileType::Wall);
    CHECK(session.tool() == MakerSession::Tool::Paint);
}

void testEraseVisitsCoveredCellOnce()
{
    TEST("makerEraseVisitsCoveredCellOnce");
    MakerSession session;
    session.selectTile(TileType::PressurePlate);
    CHECK(session.beginStroke({ 8, 5 }));
    CHECK(session.endStroke());
    session.selectTile(TileType::Rock);
    CHECK(session.beginStroke({ 8, 5 }));
    CHECK(session.endStroke());
    CHECK(tileAt(session, { 8, 5, 1 }) == TileType::Rock);
    session.setTool(MakerSession::Tool::Erase);
    CHECK(session.beginStroke({ 8, 5 }));
    CHECK(!session.continueStroke(std::nullopt));
    CHECK(!session.continueStroke(GridPosition { 8, 5 }));
    CHECK(session.endStroke());
    CHECK(tileAt(session, { 8, 5, 1 }) == TileType::PressurePlate);
    CHECK(session.beginStroke({ 8, 5 }));
    CHECK(session.endStroke());
    CHECK(tileAt(session, { 8, 5, 1 }) == TileType::Air);
}

void testRejectedPaintReportsPlacementRule()
{
    TEST("makerRejectedPaintReportsPlacementRule");
    MakerSession session;
    CHECK(session.status().empty());
    session.selectTile(TileType::StreamUp);
    CHECK(session.beginStroke({ 0, 5 }));
    CHECK(!session.endStroke());
    CHECK(session.status() == "Water streams must be placed on existing water.");
    CHECK(!session.editor().canUndo());

    session.selectTile(TileType::Wall);
    CHECK(session.status().empty());
    CHECK(session.beginStroke({ 0, 5 }));
    CHECK(session.endStroke());
    CHECK(session.status().empty());
    CHECK(session.beginStroke({ 0, 5 }));
    CHECK(!session.endStroke());
    CHECK(session.status().empty());
}

void testPlaytestSnapshotsUnsavedDraft()
{
    TEST("makerPlaytestSnapshotsUnsavedDraft");
    MakerSession session;
    CHECK(session.beginStroke({ 0, 5 }));
    CHECK(session.continueStroke(GridPosition { 1, 5 }));
    const auto first = session.buildPlaytestLevel();
    CHECK(first.has_value());
    CHECK(!session.strokeActive());
    CHECK(session.editor().historyStats().undoRecords == 1);
    CHECK(session.editor().loadedDocumentPath().empty());
    if (first) {
        CHECK(first->tileAt(0, 5, 1) == TileType::Wall);
        CHECK(first->tileAt(1, 5, 1) == TileType::Wall);
    }

    CHECK(session.beginStroke({ 4, 5 }));
    CHECK(session.endStroke());
    CHECK(session.undo());
    session.selectTile(TileType::ConveyorUp);
    session.rotateTile();
    session.stepLayer(-1);
    const auto second = session.buildPlaytestLevel();
    CHECK(second.has_value());
    if (second) {
        CHECK(second->tileAt(0, 5, 1) == TileType::Wall);
        CHECK(second->tileAt(4, 5, 1) == TileType::Air);
    }
    CHECK(session.editor().activeLayer() == 0);
    CHECK(session.editor().selectedTile() == TileType::ConveyorRight);
    CHECK(session.editor().historyStats().undoRecords == 1);
    CHECK(session.editor().historyStats().redoRecords == 1);
    CHECK(session.redo());
    CHECK(tileAt(session, { 4, 5, 1 }) == TileType::Wall);
    CHECK(session.undo());
    CHECK(session.undo());
    CHECK(tileAt(session, { 0, 5, 1 }) == TileType::Air);
    if (first) {
        CHECK(first->tileAt(0, 5, 1) == TileType::Wall);
    }
}

void testPlaytestSolveKeepsEditorHistory()
{
    TEST("makerPlaytestSolveKeepsEditorHistory");
    MakerSession maker;
    maker.setTool(MakerSession::Tool::Erase);
    CHECK(maker.beginStroke({ 7, 3 }));
    CHECK(maker.endStroke());
    maker.selectTile(TileType::End);
    CHECK(maker.beginStroke({ 1, 4 }));
    CHECK(maker.endStroke());
    const auto level = maker.buildPlaytestLevel();
    CHECK(level.has_value());
    if (!level) return;

    GameplaySession playtest;
    playtest.reset(*level);
    GameplayPresentation presentation;
    presentation.resetEntities(playtest.state());
    const auto solve = GameplayLoop::update(*level, playtest, presentation,
        { .down = { .pressed = true } }, 1.0f, true);
    CHECK(solve.draftSolved);
    CHECK(!solve.screenSolved);
    CHECK(playtest.state().players.front().cell == GridPosition3({ 1, 4, 1 }));
    CHECK(tileAt(maker, { 1, 3, 1 }) == TileType::Rogue);
    CHECK(tileAt(maker, { 1, 4, 1 }) == TileType::End);
    CHECK(maker.editor().historyStats().undoRecords == 2);

    const auto replay = maker.buildPlaytestLevel();
    CHECK(replay.has_value());
    if (replay) {
        playtest.reset(*replay);
        CHECK(playtest.state().players.front().cell == GridPosition3({ 1, 3, 1 }));
        CHECK(playtest.undoCount() == 0);
    }
    CHECK(maker.undo());
    CHECK(tileAt(maker, { 1, 4, 1 }) == TileType::Air);
    CHECK(maker.redo());
    CHECK(tileAt(maker, { 1, 4, 1 }) == TileType::End);
}

void testPlaytestReportsInvalidDraft()
{
    TEST("makerPlaytestReportsInvalidDraft");
    MakerSession session;
    session.setTool(MakerSession::Tool::Erase);
    CHECK(session.beginStroke({ 1, 3 }));
    CHECK(!session.buildPlaytestLevel());
    CHECK(!session.strokeActive());
    CHECK(session.status().find("missing a hero start tile") != std::string_view::npos);
    CHECK(tileAt(session, { 1, 3, 1 }) == TileType::Air);
    CHECK(session.tool() == MakerSession::Tool::Erase);
    CHECK(session.undo());
    CHECK(session.buildPlaytestLevel().has_value());
    CHECK(session.status().empty());
    CHECK(session.editor().canRedo());

    // A creator can test traversal before putting an End on the board.
    CHECK(session.beginStroke({ 7, 3 }));
    const auto unfinished = session.buildPlaytestLevel();
    CHECK(unfinished.has_value());
    if (unfinished) {
        CHECK(unfinished->ends().empty());
    }
}

void testSaveAndLoadIncompleteDraft()
{
    TEST("makerSaveAndLoadIncompleteDraft");
    TemporaryDraftDirectory directory;
    {
        MakerSession maker;
        MakerDraftStore store(directory.root);
        CHECK(maker.draftName().empty());
        CHECK(!maker.hasUnsavedChanges());
        maker.selectTile(TileType::PressurePlate);
        CHECK(maker.beginStroke({ 8, 5 }));
        CHECK(maker.endStroke());
        maker.selectTile(TileType::Rock);
        CHECK(maker.beginStroke({ 8, 5 }));
        CHECK(maker.endStroke());

        maker.setTool(MakerSession::Tool::Erase);
        CHECK(maker.beginStroke({ 1, 3 }));
        CHECK(maker.hasUnsavedChanges());
        CHECK(maker.saveDraft(store, "Unfinished") == MakerSession::SaveDraftResult::Saved);
        CHECK(!maker.strokeActive());
        CHECK(!maker.hasUnsavedChanges());
        CHECK(maker.draftName() == "Unfinished");
        CHECK(maker.editor().historyStats().undoRecords == 3);
        CHECK(maker.undo());
        CHECK(maker.hasUnsavedChanges());
        CHECK(maker.redo());
        CHECK(!maker.hasUnsavedChanges());
    }

    MakerSession reopened;
    MakerDraftStore store(directory.root);
    CHECK(reopened.beginStroke({ 2, 5 }));
    CHECK(reopened.endStroke());
    CHECK(reopened.beginStroke({ 3, 5 }));
    CHECK(reopened.endStroke());
    CHECK(reopened.undo());
    CHECK(!reopened.loadDraft(store, "Missing"));
    CHECK(reopened.draftName().empty());
    CHECK(reopened.hasUnsavedChanges());
    CHECK(reopened.editor().canUndo());
    CHECK(reopened.editor().canRedo());
    CHECK(tileAt(reopened, { 2, 5, 1 }) == TileType::Wall);

    CHECK(reopened.loadDraft(store, "Unfinished"));
    CHECK(reopened.draftName() == "Unfinished");
    CHECK(!reopened.hasUnsavedChanges());
    CHECK(!reopened.editor().canUndo());
    CHECK(!reopened.editor().canRedo());
    CHECK(reopened.editor().loadedDocumentPath().empty());
    CHECK(reopened.editor().activeLayer() == 1);
    CHECK(tileAt(reopened, { 1, 3, 1 }) == TileType::Air);
    CHECK(tileAt(reopened, { 2, 5, 1 }) == TileType::Air);
    CHECK(tileAt(reopened, { 8, 5, 1 }) == TileType::Rock);
    CHECK(reopened.editor().documentPlateAt({ 8, 5, 1 }) == TileType::PressurePlate);
    CHECK(!reopened.buildPlaytestLevel());
}

void testSaveCollisionPreservesDraftAndHistory()
{
    TEST("makerSaveCollisionPreservesDraftAndHistory");
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.saveDraft(store, "First") == MakerSession::SaveDraftResult::Saved);
    CHECK(maker.saveDraft(store, "Second") == MakerSession::SaveDraftResult::Saved);
    CHECK(maker.beginStroke({ 1, 5 }));
    CHECK(maker.endStroke());
    CHECK(maker.saveDraft(store, "First") == MakerSession::SaveDraftResult::NameExists);
    CHECK(maker.draftName() == "Second");
    CHECK(maker.hasUnsavedChanges());
    CHECK(maker.editor().canUndo());

    MakerSession reopened;
    CHECK(reopened.loadDraft(store, "First"));
    CHECK(tileAt(reopened, { 1, 5, 1 }) == TileType::Air);
    CHECK(maker.saveDraft(store, "First", true) == MakerSession::SaveDraftResult::Saved);
    CHECK(reopened.loadDraft(store, "First"));
    CHECK(tileAt(reopened, { 1, 5, 1 }) == TileType::Wall);
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.editor().canUndo());
}

void testNewDraftStartsWithBlankBoardAndHistory()
{
    TEST("makerNewDraftStartsWithBlankBoardAndHistory");
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.beginStroke({ 2, 4 }));
    CHECK(maker.endStroke());
    CHECK(maker.saveDraft(store, "Original") == MakerSession::SaveDraftResult::Saved);
    CHECK(maker.beginStroke({ 1, 4 }));
    CHECK(maker.endStroke());
    CHECK(maker.undo());
    CHECK(maker.editor().canUndo());
    CHECK(maker.editor().canRedo());
    maker.selectTile(TileType::ConveyorRight);
    maker.setTool(MakerSession::Tool::Erase);
    maker.stepLayer(-1);
    CHECK(maker.beginStroke({ 0, 0 }));
    CHECK(maker.hasUnsavedChanges());

    CHECK(maker.newDraft(7, 3));
    CHECK(maker.draftName().empty());
    CHECK(!maker.hasUnsavedChanges());
    CHECK(!maker.strokeActive());
    CHECK(!maker.editor().canUndo());
    CHECK(!maker.editor().canRedo());
    CHECK(maker.editor().activeLayer() == 1);
    CHECK(maker.editor().selectedTile() == TileType::ConveyorRight);
    CHECK(maker.tool() == MakerSession::Tool::Erase);
    const std::vector<std::vector<std::string>> expected {
        std::vector<std::string>(3, std::string(7, tileTypeToChar(TileType::Ground))),
        std::vector<std::string>(3, std::string(7, tileTypeToChar(TileType::Air))),
    };
    CHECK(maker.editor().documentLayers() == expected);
    CHECK(maker.saveDraft(store, "Blank") == MakerSession::SaveDraftResult::Saved);

    MakerSession reopened;
    CHECK(reopened.loadDraft(store, "Blank"));
    CHECK(reopened.editor().documentLayers() == expected);
    CHECK(reopened.loadDraft(store, "Original"));
    CHECK(tileAt(reopened, { 2, 4, 1 }) == TileType::Wall);
}

void testNewDraftValidatesDimensionsBeforeReplacingBoard()
{
    TEST("makerNewDraftValidatesDimensionsBeforeReplacingBoard");
    MakerSession maker;
    CHECK(maker.beginStroke({ 0, 0 }));
    CHECK(!maker.newDraft(0, 4));
    CHECK(!maker.status().empty());
    CHECK(!maker.newDraft(5, MakerSession::maxDraftDimension + 1));
    CHECK(maker.strokeActive());
    CHECK(maker.hasUnsavedChanges());
    CHECK(maker.endStroke());
    CHECK(maker.editor().canUndo());
    CHECK(tileAt(maker, { 0, 0, 1 }) == TileType::Wall);

    const auto smallest = MakerSession::minDraftDimension;
    CHECK(maker.newDraft(smallest, smallest));
    CHECK(maker.editor().documentWidth() == smallest);
    CHECK(maker.editor().documentHeight() == smallest);
    CHECK(maker.previewCell({ 0, 0 }) == GridPosition3({ 0, 0, 1 }));
    CHECK(!maker.previewCell({ 1, 0 }));
    maker.selectTile(TileType::Wall);
    CHECK(maker.beginStroke({ 0, 0 }));
    CHECK(maker.endStroke());
    CHECK(maker.hasUnsavedChanges());
    CHECK(maker.undo());
    CHECK(!maker.hasUnsavedChanges());

    const auto largest = MakerSession::maxDraftDimension;
    CHECK(maker.newDraft(largest, largest));
    CHECK(maker.editor().documentWidth() == largest);
    CHECK(maker.editor().documentHeight() == largest);
    CHECK(maker.editor().documentDepth() == 2);
}

void testResizeDraftPreservesTilesAndHistory()
{
    TEST("makerResizeDraftPreservesTilesAndHistory");
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newDraft(3, 4));
    maker.selectTile(TileType::PressurePlate);
    CHECK(maker.beginStroke({ 2, 3 }));
    CHECK(maker.endStroke());
    maker.selectTile(TileType::Rock);
    CHECK(maker.beginStroke({ 2, 3 }));
    CHECK(maker.endStroke());
    maker.stepLayer(-1);
    CHECK(maker.saveDraft(store, "Board") == MakerSession::SaveDraftResult::Saved);
    const auto original = maker.editor().documentDefinition();
    const auto history = maker.editor().historyStats().undoRecords;

    constexpr auto maxSize = MakerSession::maxDraftDimension;
    CHECK(maker.resizeTileLossCount(maxSize, maxSize) == 0);
    CHECK(maker.resizeDraft(maxSize, maxSize));
    CHECK(maker.editor().documentWidth() == maxSize);
    CHECK(maker.editor().documentHeight() == maxSize);
    CHECK(maker.editor().activeLayer() == 0);
    CHECK(maker.editor().selectedTile() == TileType::Rock);
    CHECK(maker.draftName() == "Board");
    CHECK(maker.hasUnsavedChanges());
    CHECK(tileAt(maker, { 2, 3, 1 }) == TileType::Rock);
    CHECK(maker.editor().documentPlateAt({ 2, 3, 1 }) == TileType::PressurePlate);
    constexpr auto edge = static_cast<int>(maxSize - 1);
    CHECK(tileAt(maker, { edge, edge, 0 }) == TileType::Ground);
    CHECK(tileAt(maker, { edge, edge, 1 }) == TileType::Air);
    CHECK(maker.editor().historyStats().undoRecords == history + 1);
    CHECK(maker.undo());
    CHECK(maker.editor().documentDefinition() == original);
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.resizeDraft(3, 4));
    CHECK(maker.editor().canRedo());
    CHECK(maker.redo());
    CHECK(maker.hasUnsavedChanges());
    CHECK(store.load("Board").layers == original.layers);
}

void testResizeDraftReportsAndRestoresCroppedTiles()
{
    TEST("makerResizeDraftReportsAndRestoresCroppedTiles");
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    const GridPosition3 removedPlate { 3, 1, 1 };
    const GridPosition3 keptPlate { 0, 1, 1 };
    const GridPosition3 keptGate { 1, 0, 1 };
    Level::Definition draft {
        .layers = {
            std::vector<std::string>(2, std::string(4, tileTypeToChar(TileType::Ground))),
            std::vector<std::string>(2, std::string(4, tileTypeToChar(TileType::Air))),
        },
        .gates = { { .cell = keptGate, .pressurePlates = { keptPlate, removedPlate } } },
        .plates = { { .cell = removedPlate, .tile = TileType::PressurePlate } },
    };
    draft.layers[1][0][1] = tileTypeToChar(TileType::Gate);
    draft.layers[1][1][0] = tileTypeToChar(TileType::PressurePlate);
    draft.layers[1][1][3] = tileTypeToChar(TileType::Rock);
    store.save("Linked", draft);
    MakerSession maker;
    CHECK(maker.loadDraft(store, "Linked"));
    const auto original = maker.editor().documentDefinition();

    // Two ground tiles, a rock, and the plate under the rock are cropped.
    CHECK(maker.resizeTileLossCount(3, 2) == 4);
    CHECK(maker.resizeTileLossCount(4, 1) == 7);
    CHECK(maker.resizeTileLossCount(3, 1) == 8);
    CHECK(maker.editor().documentDefinition() == original);
    CHECK(!maker.resizeDraft(3, 2));
    CHECK(!maker.status().empty());
    CHECK(!maker.resizeDraft(0, 2, true));
    CHECK(maker.editor().documentDefinition() == original);
    CHECK(!maker.editor().canUndo());
    CHECK(!maker.hasUnsavedChanges());

    CHECK(maker.resizeDraft(3, 2, true));
    CHECK(maker.editor().documentWidth() == 3);
    CHECK(maker.editor().documentHeight() == 2);
    CHECK(maker.editor().coveredPlates().empty());
    CHECK(maker.editor().documentPlateAt(keptPlate) == TileType::PressurePlate);
    const auto resized = maker.editor().documentDefinition();
    CHECK(resized.gates.size() == 1);
    CHECK(resized.gates.at(0).cell == keptGate);
    CHECK(resized.gates.at(0).pressurePlates == std::vector<GridPosition3> { keptPlate });
    CHECK(maker.undo());
    CHECK(maker.editor().documentDefinition() == original);
    CHECK(maker.redo());
    CHECK(maker.editor().documentDefinition() == resized);
}

void testResizeDraftAcceptsEmptyEdgesAndExistingLargeBoards()
{
    TEST("makerResizeDraftAcceptsEmptyEdgesAndExistingLargeBoards");
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    constexpr auto wide = MakerSession::maxDraftDimension + 3;
    Level::Definition draft { .layers = {
        { std::string(wide, tileTypeToChar(TileType::Air)) },
        { std::string(wide, tileTypeToChar(TileType::Air)) },
    } };
    draft.layers[0][0][0] = tileTypeToChar(TileType::Ground);
    store.save("Wide", draft);
    MakerSession maker;
    CHECK(maker.loadDraft(store, "Wide"));
    CHECK(!maker.resizeDraft(wide + 1, 1, true));
    CHECK(maker.resizeDraft(wide, 2));
    CHECK(maker.editor().documentWidth() == wide);
    CHECK(maker.editor().documentHeight() == 2);
    CHECK(maker.undo());
    CHECK(maker.resizeTileLossCount(1, 1) == 0);
    CHECK(maker.resizeDraft(1, 1));
    CHECK(tileAt(maker, { 0, 0, 0 }) == TileType::Ground);
    CHECK(maker.editor().documentWidth() == 1);
}

} // namespace

int main()
{
    testStarterIsIndependent();
    testStrokeClipsAndGroups();
    testBrushAndLayerChanges();
    testEraseVisitsCoveredCellOnce();
    testRejectedPaintReportsPlacementRule();
    testPlaytestSnapshotsUnsavedDraft();
    testPlaytestSolveKeepsEditorHistory();
    testPlaytestReportsInvalidDraft();
    testSaveAndLoadIncompleteDraft();
    testSaveCollisionPreservesDraftAndHistory();
    testNewDraftStartsWithBlankBoardAndHistory();
    testNewDraftValidatesDimensionsBeforeReplacingBoard();
    testResizeDraftPreservesTilesAndHistory();
    testResizeDraftReportsAndRestoresCroppedTiles();
    testResizeDraftAcceptsEmptyEdgesAndExistingLargeBoards();
    if (failures != 0) {
        std::cerr << "MakerSessionTests: " << failures << " failures of " << checks << " checks\n";
        return 1;
    }
    std::cout << "MakerSessionTests: " << checks << " checks passed\n";
    return 0;
}
