#include "TestHarness.hpp"

#include "engine/GameplayLoop.hpp"
#include "engine/MakerDraftStore.hpp"
#include "engine/MakerSession.hpp"
#include "engine/Rules.hpp"

#include <array>
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

void testPlacementHintsMatchPainting()
{
    TEST("makerPlacementHintsMatchPainting");
    MakerSession maker;
    const GridPosition cell { 0, 5 };
    const auto checkRejected = [&](TileType tile) {
        maker.selectTile(tile);
        const auto target = maker.previewCell(cell);
        CHECK(target.has_value());
        const auto before = maker.editor().documentDefinition();
        const std::string_view hint = maker.editor().tilePlacementError(*target, tile);
        CHECK(!hint.empty());
        CHECK(maker.editor().documentDefinition() == before);
        CHECK(maker.beginStroke(cell));
        CHECK(!maker.endStroke());
        CHECK(maker.status() == hint);
        CHECK(!maker.editor().canUndo());
    };
    checkRejected(TileType::StreamUp);
    checkRejected(TileType::LilyPad);
    checkRejected(TileType::Ladder);
    checkRejected(TileType::Minecart);

    const auto paint = [&](TileType tile) {
        maker.selectTile(tile);
        CHECK(maker.editor().tilePlacementError(*maker.previewCell(cell), tile).empty());
        CHECK(maker.beginStroke(cell));
        CHECK(maker.endStroke());
    };
    paint(TileType::RailStopNorthSouth);
    paint(TileType::Minecart);
    CHECK(maker.editor().documentPlateAt(*maker.previewCell(cell)) == TileType::RailStopNorthSouth);

    maker.stepLayer(-1);
    paint(TileType::Ladder);
    paint(TileType::Water);
    paint(TileType::StreamRight);
    CHECK(!maker.editor().tilePlacementError({ cell.x, cell.y, 1 }, TileType::LilyPad).empty());
    maker.stepLayer(1);
    maker.setTool(MakerSession::Tool::Erase);
    CHECK(maker.beginStroke(cell));
    CHECK(maker.endStroke());
    CHECK(maker.beginStroke(cell));
    CHECK(maker.endStroke());
    paint(TileType::LilyPad);
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
    CHECK(maker.saveDraft(store, "Cropped") == MakerSession::SaveDraftResult::Saved);
    CHECK(store.load("Cropped").gates.at(0).pressurePlates == std::vector<GridPosition3> { keptPlate });
}

void paintLinkTile(MakerSession& maker, GridPosition3 cell, TileType tile)
{
    maker.selectTile(tile);
    CHECK(maker.beginStroke({ cell.x, cell.y }));
    CHECK(maker.endStroke());
}

void testConnectDeviceAndPreserveExistingDrivers()
{
    TEST("makerConnectDeviceAndPreserveExistingDrivers");
    using Kind = LevelEditor::LinkEndpointKind;
    using Group = Level::LinkGroupKind;
    MakerSession maker;
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    const GridPosition3 gate { 0, 4, 1 }, otherGate { 1, 4, 1 };
    const GridPosition3 source { 0, 5, 1 }, extraSource { 1, 5, 1 }, otherSource { 2, 5, 1 };
    paintLinkTile(maker, gate, TileType::Gate);
    paintLinkTile(maker, otherGate, TileType::Gate);
    paintLinkTile(maker, source, TileType::PressurePlate);
    paintLinkTile(maker, source, TileType::Rock);
    paintLinkTile(maker, extraSource, TileType::ButtonNorth);
    paintLinkTile(maker, otherSource, TileType::LeverNorth);
    const auto& editor = maker.editor();
    CHECK(editor.linkEndpointAt(source)->kind == Kind::Object);
    CHECK(editor.linkSourcesForDevice(gate).empty());
    CHECK(editor.linkSourcesForDevice(otherGate).empty());
    CHECK(!LevelEditor::sameLinkColor(*editor.linkEndpointColor({ gate, Kind::Device }),
        *editor.linkEndpointColor({ source, Kind::Source })));
    CHECK(maker.saveDraft(store, "Wiring") == MakerSession::SaveDraftResult::Saved);
    maker.setTool(MakerSession::Tool::Connect);
    CHECK(!maker.beginStroke({ gate.x, gate.y }));
    CHECK(maker.connectCell(gate));
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.connectCell(source));
    CHECK(editor.linkSourcesForDevice(gate) == std::vector<GridPosition3> { source });
    CHECK(maker.hasUnsavedChanges());
    const Vec3 group = *editor.linkEndpointColor({ gate, Kind::Device });
    CHECK(editor.linkGroupName(Group::Device, group) == "Device group 1");
    CHECK(maker.undo());
    CHECK(editor.linkSourcesForDevice(gate).empty());
    CHECK(editor.linkGroupName(Group::Device, group).empty());
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.redo());
    CHECK(editor.linkSourcesForDevice(gate) == std::vector<GridPosition3> { source });

    CHECK(maker.connectCell(extraSource));
    const std::vector<GridPosition3> twoSources { source, extraSource };
    CHECK(editor.linkSourcesForDevice(gate) == twoSources);
    maker.clearLinkSelection();
    CHECK(maker.connectCell(otherGate));
    CHECK(maker.connectCell(otherSource));
    maker.clearLinkSelection();
    CHECK(maker.connectCell(gate));
    CHECK(maker.connectCell(otherSource));
    const std::vector<GridPosition3> threeSources { source, extraSource, otherSource };
    CHECK(editor.linkSourcesForDevice(gate) == threeSources);
    CHECK(editor.linkSourcesForDevice(otherGate) == std::vector<GridPosition3> { otherSource });
    CHECK(LevelEditor::sameLinkColor(group, *editor.linkEndpointColor({ gate, Kind::Device })));
    CHECK(maker.renameLinkGroup(Group::Device, group, "North gate"));
    CHECK(!maker.createSelectedLinkGroup("north gate"));
    CHECK(!maker.createSelectedLinkGroup("bad\nname"));
    CHECK(maker.saveDraft(store, "Wiring", true) == MakerSession::SaveDraftResult::Saved);
    MakerSession loaded;
    CHECK(loaded.loadDraft(store, "Wiring"));
    CHECK(loaded.editor().linkGroupName(Group::Device, group) == "North gate");
    CHECK(loaded.editor().linkSourcesForDevice(gate) == threeSources);
    CHECK(loaded.editor().linkSourcesForDevice(otherGate) == std::vector<GridPosition3> { otherSource });
    CHECK(loaded.buildPlaytestLevel()->gateAt(gate)->pressurePlates == threeSources);
    CHECK(maker.disconnectSelectedLink());
    CHECK(editor.linkSourcesForDevice(gate).empty());
    CHECK(editor.linkSourcesForDevice(otherGate) == std::vector<GridPosition3> { otherSource });
    CHECK(maker.assignSelectedLinkGroup(group));
    CHECK(editor.linkSourcesForDevice(gate) == twoSources);
}

void testConnectPortalsAndMovableObjects()
{
    TEST("makerConnectPortalsAndMovableObjects");
    using Kind = LevelEditor::LinkEndpointKind;
    using Group = Level::LinkGroupKind;
    MakerSession maker;
    const GridPosition3 first { 0, 4, 1 }, second { 1, 4, 1 }, third { 2, 4, 1 };
    const GridPosition3 rock { 0, 5, 1 }, ice { 1, 5, 1 }, lastRock { 2, 5, 1 };
    paintLinkTile(maker, first, TileType::PortalNorth);
    paintLinkTile(maker, second, TileType::PortalSouth);
    paintLinkTile(maker, third, TileType::PortalWest);
    paintLinkTile(maker, rock, TileType::Rock);
    paintLinkTile(maker, ice, TileType::Ice);
    paintLinkTile(maker, lastRock, TileType::Rock);
    CHECK(!maker.buildPlaytestLevel()->portalExit(first));
    maker.setTool(MakerSession::Tool::Connect);
    CHECK(maker.connectCell(first));
    CHECK(maker.connectCell(second));
    CHECK(maker.buildPlaytestLevel()->portalExit(first) == second);
    CHECK(maker.connectCell(third));
    CHECK(!maker.status().empty());
    CHECK(maker.buildPlaytestLevel()->portalExit(first) == second);
    CHECK(maker.disconnectSelectedLink());
    CHECK(!maker.buildPlaytestLevel()->portalExit(second));
    CHECK(maker.connectCell(third));
    CHECK(maker.buildPlaytestLevel()->portalExit(first) == third);
    CHECK(maker.undo());
    CHECK(!maker.buildPlaytestLevel()->portalExit(first));

    maker.clearLinkSelection();
    CHECK(maker.connectCell(rock));
    CHECK(maker.connectCell(ice));
    const auto& editor = maker.editor();
    CHECK(editor.objectLinks().size() == 2);
    const Vec3 group = *editor.linkEndpointColor({ rock, Kind::Object });
    CHECK(editor.linkGroupName(Group::Object, group) == "Sync group 1");
    CHECK(maker.selectLinkEndpoint({ lastRock, Kind::Object }));
    CHECK(maker.assignSelectedLinkGroup(group));
    CHECK(editor.objectLinks().size() == 3);
    CHECK(maker.createSelectedLinkGroup("Spare rocks"));
    const Vec3 spare = *editor.linkEndpointColor({ lastRock, Kind::Object });
    CHECK(!LevelEditor::sameLinkColor(group, spare));
    CHECK(maker.disconnectSelectedLink());
    CHECK(!editor.linkEndpointColor({ lastRock, Kind::Object }));
    CHECK(editor.linkGroupName(Group::Object, spare) == "Spare rocks");
    CHECK(maker.assignSelectedLinkGroup(spare));
    CHECK(editor.linkEndpointColor({ lastRock, Kind::Object }) == spare);
}

void testConnectKeepsLegacyExplicitEdges()
{
    TEST("makerConnectKeepsLegacyExplicitEdges");
    using Kind = LevelEditor::LinkEndpointKind;
    using Group = Level::LinkGroupKind;
    MakerSession maker;
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    auto definition = maker.editor().documentDefinition();
    const GridPosition3 first { 0, 4, 1 }, second { 1, 4, 1 };
    const GridPosition3 shared { 0, 5, 1 }, a { 1, 5, 1 }, b { 2, 5, 1 };
    const Vec3 color { 0.3f, 0.2f, 0.9f };
    const auto setTile = [&](GridPosition3 cell, TileType tile) {
        definition.layers[static_cast<std::size_t>(cell.z)][static_cast<std::size_t>(cell.y)]
            [static_cast<std::size_t>(cell.x)] = tileTypeToChar(tile);
    };
    for (const auto gate : { first, second }) setTile(gate, TileType::Gate);
    for (const auto source : { shared, a, b }) {
        setTile(source, TileType::PressurePlate);
    }
    const std::vector<GridPosition3> firstSources { shared, a };
    const std::vector<GridPosition3> secondSources { shared, b };
    definition.gates = { { .cell = first, .pressurePlates = firstSources, .color = color },
        { .cell = second, .pressurePlates = secondSources, .color = color } };
    store.save("Legacy", definition);
    CHECK(maker.loadDraft(store, "Legacy"));
    CHECK(maker.editor().linkSourcesForDevice(first) == firstSources);
    CHECK(maker.editor().linkSourcesForDevice(second) == secondSources);
    CHECK(maker.renameLinkGroup(Group::Device, color, "Legacy wiring"));
    CHECK(maker.saveDraft(store, "Legacy", true) == MakerSession::SaveDraftResult::Saved);
    CHECK(store.load("Legacy").gates.at(0).pressurePlates == firstSources);
    CHECK(store.load("Legacy").gates.at(1).pressurePlates == secondSources);
    CHECK(maker.selectLinkEndpoint({ shared, Kind::Source }));
    CHECK(maker.disconnectSelectedLink());
    CHECK(maker.editor().linkSourcesForDevice(first) == std::vector<GridPosition3> { a });
    CHECK(maker.editor().linkSourcesForDevice(second) == std::vector<GridPosition3> { b });
    CHECK(maker.undo());
    CHECK(maker.editor().linkSourcesForDevice(first) == firstSources);
    CHECK(maker.editor().linkSourcesForDevice(second) == secondSources);
}

template <auto FindDevice>
const std::vector<GridPosition3>* compiledLinkSources(const Level& level, GridPosition3 cell)
{
    const auto* device = (level.*FindDevice)(cell);
    return device ? &device->pressurePlates : nullptr;
}

void testEverySignalSourceConnectsToEveryDeviceInEitherOrder()
{
    TEST("makerEverySignalSourceConnectsToEveryDeviceInEitherOrder");
    constexpr std::array sources {
        TileType::PressurePlate,
        TileType::ButtonNorth, TileType::ButtonEast, TileType::ButtonSouth, TileType::ButtonWest,
        TileType::LeverNorth, TileType::LeverEast, TileType::LeverSouth, TileType::LeverWest,
    };
    struct DeviceCase {
        TileType tile;
        const std::vector<GridPosition3>* (*compiledSources)(const Level&, GridPosition3);
    };
    constexpr std::array devices {
        DeviceCase { TileType::Gate, compiledLinkSources<&Level::gateAt> },
        DeviceCase { TileType::RotatorClockwise, compiledLinkSources<&Level::rotatorAt> },
        DeviceCase { TileType::RotatorCounterClockwise, compiledLinkSources<&Level::rotatorAt> },
        DeviceCase { TileType::LockPlate, compiledLinkSources<&Level::lockPlateAt> },
        DeviceCase { TileType::Elevator, compiledLinkSources<&Level::elevatorAt> },
        DeviceCase { TileType::Minecart, compiledLinkSources<&Level::minecartAt> },
    };
    constexpr GridPosition3 sourceCell { 1, 5, 1 };
    constexpr GridPosition3 deviceCell { 4, 5, 1 };
    const std::vector<GridPosition3> expectedSources { sourceCell };
    const std::vector<GridPosition3> expectedDevices { deviceCell };

    for (const auto source : sources) {
        for (const auto& device : devices) {
            for (bool sourceFirst : { true, false }) {
                const auto context = std::string(tileTypeName(source)) + " / " +
                    std::string(tileTypeName(device.tile)) +
                    (sourceFirst ? " (source first)" : " (device first)");
                MakerSession maker;
                paintLinkTile(maker, sourceCell, source);
                if (device.tile == TileType::Minecart) {
                    paintLinkTile(maker, deviceCell, TileType::RailStopEastWest);
                    paintLinkTile(maker, { deviceCell.x + 1, deviceCell.y, deviceCell.z },
                        TileType::RailStopEastWest);
                }
                paintLinkTile(maker, deviceCell, device.tile);
                const auto& editor = maker.editor();
                CHECK_MESSAGE(editor.linkSourcesForDevice(deviceCell).empty(), context.c_str());
                maker.setTool(MakerSession::Tool::Connect);
                CHECK_MESSAGE(maker.connectCell(sourceFirst ? sourceCell : deviceCell), context.c_str());
                CHECK_MESSAGE(editor.linkSourcesForDevice(deviceCell).empty(), context.c_str());
                CHECK_MESSAGE(maker.connectCell(sourceFirst ? deviceCell : sourceCell), context.c_str());
                CHECK_MESSAGE(editor.linkSourcesForDevice(deviceCell) == expectedSources, context.c_str());
                CHECK_MESSAGE(editor.devicesDrivenBy(sourceCell) == expectedDevices, context.c_str());

                const auto level = maker.buildPlaytestLevel();
                CHECK_MESSAGE(level.has_value(), context.c_str());
                if (level) {
                    const auto* compiled = device.compiledSources(*level, deviceCell);
                    CHECK_MESSAGE(compiled && *compiled == expectedSources, context.c_str());
                    CHECK_MESSAGE(level->plateAt(sourceCell) == source, context.c_str());
                    if (device.tile == TileType::Minecart) {
                        CHECK_MESSAGE(level->minecartRoutes().front().stops.size() == 2, context.c_str());
                    }
                }
            }
        }
    }
}

void testCoveredConnectionUsesTheRequestedEndpoint()
{
    TEST("makerCoveredConnectionUsesTheRequestedEndpoint");
    using Kind = LevelEditor::LinkEndpointKind;
    using Endpoint = LevelEditor::LinkEndpoint;
    constexpr GridPosition3 sourceCell { 1, 5, 1 };
    constexpr GridPosition3 deviceCell { 4, 5, 1 };
    constexpr Endpoint source { sourceCell, Kind::Source };
    constexpr Endpoint device { deviceCell, Kind::Device };
    constexpr Endpoint sourceRock { sourceCell, Kind::Object };
    const std::vector<GridPosition3> expectedSources { sourceCell };

    for (const auto sourceTile : { TileType::PressurePlate, TileType::ButtonEast, TileType::LeverWest }) {
        for (const auto deviceTile : { TileType::RotatorClockwise, TileType::LockPlate }) {
            for (bool sourceFirst : { true, false }) {
                MakerSession maker;
                paintLinkTile(maker, sourceCell, sourceTile);
                paintLinkTile(maker, sourceCell, TileType::RoundRock);
                paintLinkTile(maker, deviceCell, deviceTile);
                paintLinkTile(maker, deviceCell, TileType::Rock);
                maker.setTool(MakerSession::Tool::Connect);
                const auto& editor = maker.editor();

                CHECK(maker.connectCell(sourceCell));
                CHECK(maker.selectedLinkEndpoint() == sourceRock);
                CHECK(maker.connectCell(deviceCell));
                CHECK(editor.objectLinks().size() == 2);
                CHECK(editor.linkSourcesForDevice(deviceCell).empty());
                const auto objectsBefore = editor.objectLinks();
                maker.clearLinkSelection();

                CHECK(maker.selectLinkEndpoint(sourceFirst ? source : device));
                CHECK(maker.linkTargetAt(sourceFirst ? deviceCell : sourceCell) ==
                    (sourceFirst ? device : source));
                CHECK(maker.connectCell(sourceFirst ? deviceCell : sourceCell));
                CHECK(editor.linkSourcesForDevice(deviceCell) == expectedSources);
                CHECK(maker.selectedLinkEndpoint() == device);
                CHECK(editor.objectLinks() == objectsBefore);
                const auto level = maker.buildPlaytestLevel();
                CHECK(level.has_value());
                if (level) {
                    CHECK(level->plateAt(sourceCell) == sourceTile);
                    CHECK(level->plateAt(deviceCell) == deviceTile);
                    const auto* compiled = deviceTile == TileType::LockPlate
                        ? compiledLinkSources<&Level::lockPlateAt>(*level, deviceCell)
                        : compiledLinkSources<&Level::rotatorAt>(*level, deviceCell);
                    CHECK(compiled && *compiled == expectedSources);
                }
            }
        }
    }

    for (const auto deviceTile : { TileType::RotatorCounterClockwise, TileType::LockPlate }) {
        for (const auto occupant : { TileType::Knight, TileType::Enemy }) {
            MakerSession maker;
            paintLinkTile(maker, sourceCell, TileType::PressurePlate);
            paintLinkTile(maker, deviceCell, deviceTile);
            paintLinkTile(maker, deviceCell, occupant);
            maker.setTool(MakerSession::Tool::Connect);
            CHECK(maker.connectCell(deviceCell));
            CHECK(maker.selectedLinkEndpoint() == device);
            CHECK(maker.connectCell(sourceCell));
            CHECK(maker.editor().linkSourcesForDevice(deviceCell) == expectedSources);
        }
    }
}

void testConnectedStoneCombinationsMoveInPlaytest()
{
    TEST("makerConnectedStoneCombinationsMoveInPlaytest");
    using Kind = LevelEditor::LinkEndpointKind;
    using Endpoint = LevelEditor::LinkEndpoint;
    constexpr std::array stones {
        TileType::Rock, TileType::RoundRock, TileType::Crystal, TileType::SkippingCrystal,
        TileType::Ice,
    };
    constexpr GridPosition3 hero { 1, 0, 1 };
    constexpr GridPosition3 leader { 2, 0, 1 };
    constexpr GridPosition3 follower { 2, 1, 1 };
    constexpr GridPosition3 unrelated { 2, 2, 1 };

    for (const auto pushedTile : stones) {
        for (const auto followingTile : stones) {
            for (bool leaderFirst : { true, false }) {
                const auto context = std::string(tileTypeName(pushedTile)) + " / " +
                    std::string(tileTypeName(followingTile)) +
                    (leaderFirst ? " (leader first)" : " (follower first)");
                MakerSession maker;
                CHECK(maker.newDraft(8, 3));
                paintLinkTile(maker, hero, TileType::Rogue);
                paintLinkTile(maker, leader, pushedTile);
                paintLinkTile(maker, follower, followingTile);
                paintLinkTile(maker, unrelated, TileType::Rock);
                maker.setTool(MakerSession::Tool::Connect);
                const auto first = leaderFirst ? leader : follower;
                const auto second = leaderFirst ? follower : leader;
                CHECK_MESSAGE(maker.connectCell(first), context.c_str());
                CHECK_MESSAGE((maker.selectedLinkEndpoint() == Endpoint { first, Kind::Object }), context.c_str());
                CHECK_MESSAGE(maker.editor().objectLinks().empty(), context.c_str());
                CHECK_MESSAGE(maker.connectCell(second), context.c_str());

                const auto level = maker.buildPlaytestLevel();
                CHECK_MESSAGE(level.has_value(), context.c_str());
                if (!level) continue;
                CHECK_MESSAGE(level->movablesAreLinked(0, 1), context.c_str());
                CHECK_MESSAGE(!level->movablesAreLinked(0, 2), context.c_str());
                const auto initial = rules::initialState(*level);
                const auto after = rules::step(*level, initial, MoveDirection::Right);
                CHECK_MESSAGE(after.players.at(0).cell == leader, context.c_str());
                CHECK_MESSAGE(after.movables.at(0).cell.x > leader.x, context.c_str());
                CHECK_MESSAGE(after.movables.at(1).cell.x > follower.x, context.c_str());
                CHECK_MESSAGE(after.movables.at(2).cell == unrelated, context.c_str());
            }
        }
    }
}

void testWiredElevatorMovesBetweenMakerStops()
{
    TEST("makerWiredElevatorMovesBetweenMakerStops");
    MakerSession maker;
    CHECK(maker.newDraft(6, 4));
    constexpr GridPosition3 hero { 1, 1, 1 };
    constexpr GridPosition3 elevator { 4, 1, 0 };
    constexpr GridPosition3 upperStop { 4, 1, 1 };
    paintLinkTile(maker, hero, TileType::ButtonNorth);
    paintLinkTile(maker, hero, TileType::Rogue);
    maker.stepLayer(-1);
    paintLinkTile(maker, elevator, TileType::Elevator);

    maker.setTool(MakerSession::Tool::Connect);
    CHECK(maker.connectCell(elevator));
    CHECK(!maker.toggleSelectedElevatorStop());
    maker.toggleLinkLayer();
    CHECK(maker.linkLayerIsolated());
    maker.stepLayer(1);
    CHECK(maker.connectCell(hero));
    CHECK(maker.toggleSelectedElevatorStop());
    CHECK(maker.editor().elevators().front().levels == (std::vector<int> { 0, 1 }));
    CHECK(maker.undo());
    CHECK(maker.editor().elevators().front().levels == std::vector<int> { 0 });
    CHECK(maker.redo());
    const auto level = maker.buildPlaytestLevel();
    CHECK(level.has_value());
    if (!level) return;
    const auto moved = rules::activate(*level, rules::initialState(*level));
    CHECK(moved.has_value());
    if (moved) CHECK(moved->elevators.front().cell == upperStop);
    CHECK(maker.toggleSelectedElevatorStop());
    CHECK(maker.editor().elevators().front().levels == std::vector<int> { 0 });
}

void testRemainingMovablesFollowConnectedRock()
{
    TEST("makerRemainingMovablesFollowConnectedRock");
    using Endpoint = LevelEditor::LinkEndpoint;
    using Kind = LevelEditor::LinkEndpointKind;
    constexpr GridPosition3 hero { 0, 0, 1 };
    constexpr GridPosition3 rock { 1, 0, 1 };
    constexpr GridPosition3 follower { 3, 2, 1 };
    constexpr GridPosition3 rockDestination { 2, 0, 1 };
    constexpr GridPosition3 followerDestination { 4, 2, 1 };
    constexpr std::array followers {
        TileType::RobotNorth, TileType::RobotEast, TileType::RobotSouth, TileType::RobotWest,
        TileType::TurretNorth, TileType::TurretEast, TileType::TurretSouth, TileType::TurretWest,
        TileType::MirrorNorthWest, TileType::MirrorNorthEast,
        TileType::MirrorSouthEast, TileType::MirrorSouthWest, TileType::LilyPad,
    };
    for (const auto tile : followers) {
        const std::string context(tileTypeName(tile));
        MakerSession maker;
        CHECK(maker.newDraft(6, 3));
        if (tile == TileType::LilyPad) {
            maker.stepLayer(-1);
            paintLinkTile(maker, { 3, 2, 0 }, TileType::Water);
            paintLinkTile(maker, { 4, 2, 0 }, TileType::Water);
            maker.stepLayer(1);
        }
        paintLinkTile(maker, hero, TileType::Rogue);
        paintLinkTile(maker, rock, TileType::Rock);
        paintLinkTile(maker, follower, tile);
        maker.setTool(MakerSession::Tool::Connect);
        CHECK(maker.connectCell(follower));
        CHECK((maker.selectedLinkEndpoint() == Endpoint { follower, Kind::Object }));
        CHECK(maker.connectCell(rock));
        const auto level = maker.buildPlaytestLevel();
        CHECK_MESSAGE(level.has_value(), context.c_str());
        if (!level) continue;
        CHECK_MESSAGE(level->movablesAreLinked(0, 1), context.c_str());
        const auto moved = rules::step(*level, rules::initialState(*level), MoveDirection::Right, { .robot = 0 });
        CHECK_MESSAGE(moved.movables.at(0).cell == rockDestination, context.c_str());
        CHECK_MESSAGE(moved.movables.at(1).cell == followerDestination, context.c_str());
    }
}

void testPortalFacingsPairAndCrossInEitherOrder()
{
    TEST("makerPortalFacingsPairAndCrossInEitherOrder");
    constexpr std::array facings {
        TileType::PortalNorth, TileType::PortalEast, TileType::PortalSouth, TileType::PortalWest,
    };
    constexpr GridPosition3 entrance { 1, 1, 1 };
    constexpr GridPosition3 exit { 4, 2, 1 };
    for (const auto entranceTile : facings) {
        for (const auto exitTile : facings) {
            for (const bool entranceFirst : { true, false }) {
                MakerSession maker;
                CHECK(maker.newDraft(6, 4));
                paintLinkTile(maker, { 0, 0, 1 }, TileType::Rogue);
                paintLinkTile(maker, entrance, entranceTile);
                paintLinkTile(maker, exit, exitTile);
                maker.setTool(MakerSession::Tool::Connect);
                CHECK(maker.connectCell(entranceFirst ? entrance : exit));
                CHECK(maker.connectCell(entranceFirst ? exit : entrance));
                const auto level = maker.buildPlaytestLevel();
                CHECK(level.has_value());
                if (!level) continue;
                const auto entranceEdge = portalEdgeOffset(entranceTile);
                const auto exitEdge = portalEdgeOffset(exitTile);
                const auto forward = level->portalCrossing(entrance, entranceEdge);
                const auto reverse = level->portalCrossing(exit, exitEdge);
                const GridPosition forwardDirection { -exitEdge.x, -exitEdge.y };
                const GridPosition reverseDirection { -entranceEdge.x, -entranceEdge.y };
                CHECK(forward && forward->exit == exit && forward->direction == forwardDirection);
                CHECK(reverse && reverse->exit == entrance && reverse->direction == reverseDirection);
            }
        }
    }
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
    testPlacementHintsMatchPainting();
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
    testConnectDeviceAndPreserveExistingDrivers();
    testConnectPortalsAndMovableObjects();
    testConnectKeepsLegacyExplicitEdges();
    testEverySignalSourceConnectsToEveryDeviceInEitherOrder();
    testCoveredConnectionUsesTheRequestedEndpoint();
    testConnectedStoneCombinationsMoveInPlaytest();
    testWiredElevatorMovesBetweenMakerStops();
    testRemainingMovablesFollowConnectedRock();
    testPortalFacingsPairAndCrossInEitherOrder();
    if (failures != 0) {
        std::cerr << "MakerSessionTests: " << failures << " failures of " << checks << " checks\n";
        return 1;
    }
    std::cout << "MakerSessionTests: " << checks << " checks passed\n";
    return 0;
}
