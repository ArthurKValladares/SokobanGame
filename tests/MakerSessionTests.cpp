#include "TestHarness.hpp"

#include "engine/MakerSession.hpp"

#include <cstddef>
#include <iostream>

namespace {

using namespace sokoban;

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

} // namespace

int main()
{
    testStarterIsIndependent();
    testStrokeClipsAndGroups();
    testBrushAndLayerChanges();
    testEraseVisitsCoveredCellOnce();
    testRejectedPaintReportsPlacementRule();
    if (failures != 0) {
        std::cerr << "MakerSessionTests: " << failures << " failures of " << checks << " checks\n";
        return 1;
    }
    std::cout << "MakerSessionTests: " << checks << " checks passed\n";
    return 0;
}
