#include "TestHarness.hpp"

#include "engine/GameplayLoop.hpp"
#include "engine/MakerDraftStore.hpp"
#include "engine/MakerRecoveryStore.hpp"
#include "engine/MakerSession.hpp"
#include "engine/Rules.hpp"

#include <algorithm>
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

TileType tileAt(const Level::Definition& definition, GridPosition3 cell)
{
    return charToTileType(definition.layers.at(static_cast<std::size_t>(cell.z))
        .at(static_cast<std::size_t>(cell.y)).at(static_cast<std::size_t>(cell.x))).value();
}

void testPaintingAcrossOverworldSeam()
{
    TEST("makerPaintingAcrossOverworldSeam");
    MakerSession maker;
    CHECK(maker.newOverworldDraft(9, 7));
    CHECK(maker.overworld() && maker.showNeighbors());
    CHECK(maker.currentScreenId() == 1);
    CHECK(!maker.hasUnsavedChanges());
    CHECK(!maker.canUndo());
    const auto layout = [&]() -> const OverworldLayout& { return maker.overworldLayout(); };
    maker.selectTile(TileType::Wall);
    CHECK(maker.beginStroke({ 8, 3 }));
    CHECK(maker.continueStroke(GridPosition { 10, 3 }));
    CHECK(maker.endStroke());
    CHECK(layout().screens.size() == 2);
    if (layout().screens.size() != 2) return;
    const auto eastern = layout().screens.back();
    CHECK((eastern.slot == OverworldSlot { 1, 0 }));
    CHECK(tileAt(maker, { 8, 3, 1 }) == TileType::Wall);
    std::vector<std::vector<std::string>> expectedLayers(
        2, std::vector<std::string>(7, std::string(9, tileTypeToChar(TileType::Air))));
    expectedLayers[1][3][0] = tileTypeToChar(TileType::Wall);
    expectedLayers[1][3][1] = tileTypeToChar(TileType::Wall);
    CHECK(maker.overworldDefinition(eastern.id)->layers == expectedLayers);
    CHECK(maker.undo());
    CHECK(layout().screens.size() == 1);
    CHECK(tileAt(maker, { 8, 3, 1 }) == TileType::Air);
    CHECK(!maker.canUndo() && maker.canRedo() && !maker.hasUnsavedChanges());
    CHECK(maker.redo());
    CHECK(layout().screens.size() == 2);
    CHECK(maker.overworldDefinition(eastern.id)->layers == expectedLayers);

    maker.toggleNeighbors();
    CHECK(!maker.showNeighbors());
    CHECK(!maker.beginStroke({ 9, 2 }));
    CHECK(!maker.pickTile({ 9, 3, 1 }));
    maker.toggleNeighbors();
    CHECK(maker.showNeighbors());
    CHECK(maker.pickTile({ 9, 3, 1 }));
    CHECK(maker.editor().selectedTile() == TileType::Wall);
    maker.setTool(MakerSession::Tool::Erase);
    CHECK(!maker.beginStroke({ -1, 2 }));
    CHECK(!maker.previewCell(GridPosition { 27, 0 }));
    CHECK(layout().screens.size() == 2);
    CHECK(maker.selectOverworldScreen(eastern.id));
    CHECK(maker.currentScreenSlot() == eastern.slot);
    CHECK(tileAt(maker, { 0, 3, 1 }) == TileType::Wall);
}

void testOverworldLorekeeperAndMetadata()
{
    TEST("makerOverworldLorekeeperAndMetadata");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newOverworldDraft(9, 7));
    CHECK(!maker.buildOverworldPlaytestMap());
    maker.selectTile(TileType::Bard);
    CHECK(maker.editor().selectedTile() == TileType::Player);
    CHECK(maker.beginStroke({ 2, 3 }));
    CHECK(maker.endStroke());
    CHECK(tileAt(maker, { 2, 3, 1 }) == TileType::Player);
    CHECK(maker.editor().character() == CharacterType::Lorekeeper);

    CHECK(maker.beginStroke({ -1, 3 }));
    CHECK(maker.endStroke());
    CHECK(maker.overworldLayout().screens.size() == 2);
    if (maker.overworldLayout().screens.size() != 2) return;
    const auto west = maker.overworldLayout().screens.back();
    CHECK((west.slot == OverworldSlot { -1, 0 }));
    CHECK(tileAt(*maker.overworldDefinition(west.id), { 8, 3, 1 }) == TileType::Player);
    CHECK(tileAt(maker, { 2, 3, 1 }) == TileType::Air);
    CHECK(maker.undo());
    CHECK(!maker.overworldDefinition(west.id));
    CHECK(tileAt(maker, { 2, 3, 1 }) == TileType::Player);
    CHECK(maker.redo());
    CHECK(tileAt(*maker.overworldDefinition(west.id), { 8, 3, 1 }) == TileType::Player);
    CHECK(tileAt(maker, { 2, 3, 1 }) == TileType::Air);

    maker.selectTile(TileType::WardrobeLorekeeper);
    CHECK(maker.beginStroke({ -2, 3 }));
    CHECK(maker.endStroke());
    CHECK(tileAt(*maker.overworldDefinition(west.id), { 7, 3, 1 }) == TileType::WardrobeLorekeeper);
    maker.selectTile(TileType::LecternNorth);
    CHECK(maker.beginStroke({ 4, 3 }));
    CHECK(maker.endStroke());
    CHECK(maker.editOverworldLectern({ 4, 3, 1 }, "Welcome to the overworld."));
    CHECK(maker.editor().lecterns().size() == 1);
    if (maker.editor().lecterns().size() != 1) return;
    CHECK(maker.editor().lecterns().front().text == "Welcome to the overworld.");

    maker.selectLevelSelectorBrush();
    CHECK(maker.placingSelector());
    CHECK(maker.beginStroke({ 5, 3 }));
    CHECK(maker.endStroke());
    CHECK(maker.editor().selectors().size() == 1);
    if (maker.editor().selectors().size() != 1) return;
    maker.setTool(MakerSession::Tool::Paint);
    CHECK(!maker.placingSelector());

    const auto map = maker.buildOverworldPlaytestMap();
    CHECK(map && map->startScreen() == west.id && map->selectors().size() == 1);
    CHECK(maker.saveDraft(store, "New Overworld") == MakerSession::SaveDraftResult::Saved);
    CHECK(!maker.hasUnsavedChanges());
    MakerSession reopened;
    CHECK(reopened.newOverworldDraft(9, 7));
    CHECK(reopened.loadDraft(store, "New Overworld"));
    CHECK(reopened.currentScreenId() == west.id);
    CHECK(reopened.selectOverworldScreen(1));
    CHECK(reopened.editor().lecterns().size() == 1);
    CHECK(reopened.editor().selectors().size() == 1);
    CHECK(!reopened.hasUnsavedChanges());
}

void testLockedOverworldLayerDoesNotFallBackInNeighbors()
{
    TEST("makerLockedOverworldLayerDoesNotFallBackInNeighbors");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    store.saveOverworld("Uneven", {
        .layout = OverworldLayout { .screenWidth = 2, .screenHeight = 2, .screens = {
            { .id = 1, .file = "screen1.scr" },
            { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
        } },
        .definitions = {
            { .screen = 1, .definition = { .layers = { { "..", ".." }, { "  ", "  " }, { "  ", "  " } } } },
            { .screen = 2, .definition = { .layers = { { "..", ".." }, { "# ", "  " } } } },
        },
    });
    MakerSession maker;
    CHECK(maker.newOverworldDraft(9, 7));
    CHECK(maker.loadDraft(store, "Uneven"));
    maker.stepLayer(1);
    CHECK(maker.editor().activeLayer() == 2);
    maker.toggleLayerLock();
    constexpr GridPosition3 neighbor { 2, 0, 1 };
    CHECK(!maker.pickTile(neighbor));
    CHECK(!maker.previewCell(neighbor, MakerSession::StrokeMode::Delete));
    CHECK(!maker.previewCell(neighbor, MakerSession::StrokeMode::Replace));
    CHECK(!maker.hasUnsavedChanges());
    maker.toggleLayerLock();
    CHECK(maker.pickTile(neighbor));
    CHECK(maker.previewCell(neighbor, MakerSession::StrokeMode::Delete) == neighbor);

    // The editor loaded for a neighbour's preview is kept between hovers. It
    // must still follow the lock, and an edit to that screen.
    CHECK(maker.previewCell(neighbor, MakerSession::StrokeMode::Delete) == neighbor);
    maker.toggleLayerLock();
    CHECK(!maker.previewCell(neighbor, MakerSession::StrokeMode::Delete));
    maker.toggleLayerLock();
    CHECK(maker.beginStroke(neighbor, MakerSession::StrokeMode::Delete));
    CHECK(maker.endStroke());
    CHECK(maker.previewCell(neighbor, MakerSession::StrokeMode::Delete) == (GridPosition3 { 2, 0, 0 }));
}

void testLevelSelectorAttachmentsAndUnlockLinks()
{
    TEST("makerLevelSelectorAttachmentsAndUnlockLinks");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession puzzle;
    CHECK(puzzle.newDraft(3, 3));
    CHECK(puzzle.saveDraft(store, "First Puzzle") == MakerSession::SaveDraftResult::Saved);
    CHECK(puzzle.saveDraft(store, "Second Puzzle") == MakerSession::SaveDraftResult::Saved);
    CHECK(puzzle.saveDraft(store, "Final Puzzle") == MakerSession::SaveDraftResult::Saved);
    const auto finalPuzzleId = puzzle.draftId();
    CHECK(finalPuzzleId.has_value());
    if (!finalPuzzleId) return;

    MakerSession maker;
    CHECK(maker.newOverworldDraft(9, 7));
    const auto paint = [&](GridPosition3 cell) {
        CHECK(maker.beginStroke(cell, MakerSession::StrokeMode::Brush));
        CHECK(maker.endStroke());
    };
    maker.selectTile(TileType::Player);
    paint({ 1, 1, 1 });
    constexpr GridPosition3 first { 7, 3, 1 };
    constexpr GridPosition3 second { 8, 3, 1 };
    constexpr GridPosition3 final { 9, 3, 1 };
    maker.stepLayer(-1);
    maker.selectTile(TileType::Ground);
    paint({ final.x, final.y, 0 });
    maker.stepLayer(1);
    maker.selectLevelSelectorBrush();
    paint(first);
    paint(second);
    paint(final);

    CHECK(maker.attachSelectorLevel(first, std::string("First Puzzle")));
    CHECK(maker.attachSelectorLevel(second, std::string("Second Puzzle")));
    CHECK(maker.attachSelectorLevel(final, std::string("Final Puzzle"), finalPuzzleId));
    CHECK(maker.undo());
    const auto* selector = maker.levelSelectorAt(final);
    CHECK(selector && !selector->draftName);
    CHECK(maker.redo());
    selector = maker.levelSelectorAt(final);
    CHECK(selector && selector->draftName == "Final Puzzle" && selector->draftId == finalPuzzleId);

    maker.setTool(MakerSession::Tool::ConnectSelectors);
    CHECK(maker.clickLevelSelectorConnection(first));
    CHECK(maker.clickLevelSelectorConnection(final));
    maker.clearLevelSelectorSource();
    CHECK(maker.clickLevelSelectorConnection(second));
    CHECK(maker.clickLevelSelectorConnection(final));
    CHECK(maker.clickLevelSelectorConnection(final));
    CHECK(maker.levelSelectorConnections().size() == 1);
    CHECK(maker.clickLevelSelectorConnection(final));
    maker.clearLevelSelectorSource();
    CHECK(maker.clickLevelSelectorConnection(final));
    CHECK(!maker.clickLevelSelectorConnection(first));
    CHECK(!maker.clickLevelSelectorConnection(final));
    auto connections = maker.levelSelectorConnections();
    CHECK(connections.size() == 2);
    CHECK(std::ranges::any_of(connections, [&](const auto& link) {
        return link.source == first && link.destination == final;
    }));
    CHECK(std::ranges::any_of(connections, [&](const auto& link) {
        return link.source == second && link.destination == final;
    }));
    maker.toggleNeighbors();
    CHECK(!maker.levelSelectorAt(final));
    CHECK(maker.levelSelectorConnections().empty());
    maker.toggleNeighbors();

    CHECK(maker.saveDraft(store, "Puzzle Tour") == MakerSession::SaveDraftResult::Saved);
    const auto renamed = store.renameLevel(*finalPuzzleId, "Renamed Finale");
    CHECK(renamed.id == *finalPuzzleId);
    MakerSession reopened;
    CHECK(reopened.newOverworldDraft(9, 7));
    CHECK(reopened.loadDraft(store, "Puzzle Tour"));
    const auto map = reopened.buildOverworldPlaytestMap();
    CHECK(map && map->selectors().size() == 3);
    if (!map || map->selectors().size() != 3) return;
    const auto& selectors = map->selectors();
    const auto firstRuntime = std::ranges::find(selectors, first, &OverworldSelectorRuntime::globalCell);
    const auto secondRuntime = std::ranges::find(selectors, second, &OverworldSelectorRuntime::globalCell);
    const auto finalRuntime = std::ranges::find(selectors, final, &OverworldSelectorRuntime::globalCell);
    CHECK(firstRuntime != selectors.end() && secondRuntime != selectors.end() && finalRuntime != selectors.end());
    if (firstRuntime == selectors.end() || secondRuntime == selectors.end() || finalRuntime == selectors.end()) return;
    CHECK(firstRuntime->draftName == "First Puzzle" && firstRuntime->prerequisites.empty());
    CHECK(finalRuntime->draftName == "Final Puzzle" && finalRuntime->draftId == finalPuzzleId);
    const auto linked = store.resolveLevel(finalRuntime->draftId, finalRuntime->draftName);
    CHECK(linked && linked->name == "Renamed Finale");
    const auto validatesName = [&](std::string_view name) { return store.findLevelByName(name).has_value(); };
    const auto validatesId = [&](std::string_view id) { return store.findLevelById(id).has_value(); };
    CHECK(reopened.problems(validatesName, validatesId).empty());
    CHECK(finalRuntime->prerequisites.size() == 2);
    CHECK(std::ranges::find(finalRuntime->prerequisites, firstRuntime->runtimeId) != finalRuntime->prerequisites.end());
    CHECK(std::ranges::find(finalRuntime->prerequisites, secondRuntime->runtimeId) != finalRuntime->prerequisites.end());

    reopened.setTool(MakerSession::Tool::Erase);
    CHECK(reopened.beginStroke(first, MakerSession::StrokeMode::Delete));
    CHECK(reopened.endStroke());
    CHECK(!reopened.levelSelectorAt(first));
    CHECK(reopened.levelSelectorConnections().size() == 1);
    CHECK(reopened.buildOverworldPlaytestMap().has_value());
    CHECK(reopened.undo());
    CHECK(reopened.levelSelectorConnections().size() == 2);

    const auto eastId = reopened.overworldLayout().screens.back().id;
    CHECK(reopened.moveOverworldScreen(eastId, { 0, 1 }));
    constexpr GridPosition3 movedFinal { 0, 10, 1 };
    const auto* movedSelector = reopened.levelSelectorAt(movedFinal);
    CHECK(movedSelector && movedSelector->draftName == "Final Puzzle" && movedSelector->prerequisites.size() == 2);
    CHECK(reopened.selectOverworldScreen(eastId));
    CHECK(reopened.deleteOverworldScreen(1));
    const auto* remainingSelector = reopened.levelSelectorAt({ 0, 3, 1 });
    CHECK(remainingSelector && remainingSelector->prerequisites.empty());
    CHECK(reopened.undo());
    CHECK(reopened.levelSelectorConnections().size() == 2);
}

void testOverworldPortalsPairAcrossScreens()
{
    TEST("makerOverworldPortalsPairAcrossScreens");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newOverworldDraft(9, 7));
    const auto noStart = maker.problems();
    CHECK(noStart.size() == 1 && noStart.front().kind == MakerSession::ProblemKind::MissingPlayerStart &&
        noStart.front().screen == 1 && !noStart.front().cell);
    constexpr GridPosition3 first { 4, 3, 1 };
    constexpr GridPosition3 second { 2, -3, 1 };
    const auto paint = [&](GridPosition3 cell, TileType tile) {
        maker.selectTile(tile);
        CHECK(maker.beginStroke(cell, MakerSession::StrokeMode::Brush));
        CHECK(maker.endStroke());
    };
    paint({ 1, 2, 1 }, TileType::Player);
    paint(first, TileType::PortalEast);
    paint(second, TileType::PortalSouth);
    const auto secondScreen = std::ranges::find(
        maker.overworldLayout().screens, OverworldSlot { 0, -1 }, &OverworldScreenSpec::slot);
    CHECK(secondScreen != maker.overworldLayout().screens.end());
    if (secondScreen == maker.overworldLayout().screens.end()) return;
    const auto northId = secondScreen->id;
    const auto unpaired = maker.problems();
    CHECK(unpaired.size() == 2);
    CHECK(std::ranges::any_of(unpaired, [&](const auto& problem) {
        return problem.kind == MakerSession::ProblemKind::UnpairedPortal && problem.screen == northId &&
            problem.cell == GridPosition3 { second.x, second.y + 7, second.z };
    }));

    const auto checkRuntime = [&](MakerSession& session, bool paired) {
        const auto world = session.buildOverworldPlaytestMap();
        CHECK(world.has_value());
        if (!world) return;
        const auto from = world->toGlobal(1, first);
        const auto to = world->toGlobal(northId, { second.x, second.y + 7, second.z });
        CHECK(from && to);
        if (!from || !to) return;
        CHECK(world->level().portalExit(*from) == (paired ? to : std::nullopt));
        CHECK(world->level().portalExit(*to) == (paired ? from : std::nullopt));
    };
    checkRuntime(maker, false);
    maker.setTool(MakerSession::Tool::Connect);
    CHECK(maker.connectCell(first));
    CHECK(maker.linkTargetAt(second).has_value());
    CHECK(maker.connectCell(second));
    CHECK(maker.overworldPortalPeers(first) == (std::vector<GridPosition3> { second }));
    CHECK(maker.overworldPortalPeers(second) == (std::vector<GridPosition3> { first }));
    const auto* firstPortal = maker.overworldPortalAt(first);
    const auto* secondPortal = maker.overworldPortalAt(second);
    CHECK(firstPortal && secondPortal && firstPortal->overworldPair != 0 &&
        firstPortal->overworldPair == secondPortal->overworldPair);
    checkRuntime(maker, true);
    CHECK(maker.problems().empty());

    constexpr Vec3 color { 0.9f, 0.15f, 0.23f };
    CHECK(maker.recolorSelectedLinkGroup(color));
    const auto checkColor = [&](MakerSession& session, GridPosition3 cell) {
        const auto* portal = session.overworldPortalAt(cell);
        CHECK(portal && LevelEditor::sameLinkColor(portal->color, color));
    };
    checkColor(maker, first);
    checkColor(maker, second);
    CHECK(maker.saveDraft(store, "Across the seam") == MakerSession::SaveDraftResult::Saved);
    CHECK(maker.disconnectSelectedLink());
    CHECK(maker.overworldPortalPeers(first).empty());
    CHECK(maker.overworldPortalPeers(second).empty());
    checkRuntime(maker, false);
    CHECK(maker.undo());
    checkRuntime(maker, true);
    CHECK(!maker.hasUnsavedChanges());

    MakerSession reopened;
    CHECK(reopened.newOverworldDraft(9, 7));
    CHECK(reopened.loadDraft(store, "Across the seam"));
    CHECK(reopened.currentScreenId() == 1);
    CHECK(reopened.overworldPortalPeers(first) == (std::vector<GridPosition3> { second }));
    checkColor(reopened, second);
    checkRuntime(reopened, true);
    reopened.setTool(MakerSession::Tool::Connect);
    CHECK(reopened.connectCell(first));
    CHECK(reopened.selectOverworldScreen(northId));
    CHECK(reopened.selectedLinkEndpoint() == (LevelEditor::LinkEndpoint {
        { first.x, first.y + 7, first.z }, LevelEditor::LinkEndpointKind::Portal }));
    CHECK(!reopened.moveOverworldScreen(northId, { 0, 0 }));
    CHECK(!reopened.moveOverworldScreen(northId, { 128, 0 }));
    CHECK(!reopened.canUndo());
    CHECK(!reopened.hasUnsavedChanges());
    CHECK(reopened.moveOverworldScreen(northId, { 1, 0 }));
    CHECK(reopened.currentScreenId() == northId);
    CHECK((reopened.currentScreenSlot() == OverworldSlot { 1, 0 }));
    checkRuntime(reopened, true);
    CHECK(reopened.undo());
    CHECK((reopened.currentScreenSlot() == OverworldSlot { 0, -1 }));
    CHECK(!reopened.canUndo());
    CHECK(reopened.redo());
    CHECK(reopened.deleteOverworldScreen(northId));
    CHECK(reopened.currentScreenId() == 1);
    CHECK(reopened.overworldLayout().screens.size() == 1);
    const auto* orphan = reopened.overworldPortalAt(first);
    CHECK(orphan && orphan->overworldPair == 0 && reopened.overworldPortalPeers(first).empty());
    CHECK(!reopened.deleteOverworldScreen(1));
    const auto orphaned = reopened.problems();
    CHECK(orphaned.size() == 1 && orphaned.front().kind == MakerSession::ProblemKind::UnpairedPortal &&
        orphaned.front().screen == 1 && orphaned.front().cell == first);
    CHECK(reopened.undo());
    CHECK(reopened.problems().empty());
    checkRuntime(reopened, true);
    CHECK(reopened.redo());
    CHECK(!reopened.overworldDefinition(northId));
}

void testPuzzleProblemsFollowLiveEdits()
{
    TEST("makerPuzzleProblemsFollowLiveEdits");
    using Kind = MakerSession::ProblemKind;
    MakerSession maker;
    CHECK(maker.newDraft(6, 6));
    const auto empty = maker.problems();
    CHECK(empty.size() == 1 && empty.front().kind == Kind::MissingPlayerStart &&
        !empty.front().screen && !empty.front().cell);
    constexpr GridPosition3 first { 1, 1, 1 }, second { 3, 1, 1 }, start { 1, 2, 1 };
    maker.selectTile(TileType::PortalEast);
    CHECK(maker.beginStroke(first, MakerSession::StrokeMode::Brush));
    const auto duringStroke = maker.problems();
    CHECK(maker.strokeActive());
    CHECK(duringStroke.size() == 2 && duringStroke.back().kind == Kind::UnpairedPortal &&
        duringStroke.back().cell == first && !duringStroke.back().screen);
    CHECK(maker.endStroke());
    maker.selectTile(TileType::CrystalDruid);
    CHECK(maker.beginStroke(start, MakerSession::StrokeMode::Brush));
    CHECK(maker.problems().size() == 1);
    CHECK(maker.endStroke());
    maker.selectTile(TileType::PortalWest);
    CHECK(maker.beginStroke(second, MakerSession::StrokeMode::Brush));
    CHECK(maker.endStroke());
    maker.setTool(MakerSession::Tool::Connect);
    CHECK(maker.connectCell(first));
    CHECK(maker.connectCell(second));
    CHECK(maker.problems().empty());
    CHECK(maker.undo());
    CHECK(maker.problems().size() == 2);
    CHECK(maker.redo());
    CHECK(maker.problems().empty());
}

void testProblemsCheckPortalMetadataAndSelectorPrerequisites()
{
    TEST("makerProblemsCheckPortalMetadataAndSelectorPrerequisites");
    using Kind = MakerSession::ProblemKind;
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newOverworldDraft(4, 4));
    const auto original = *maker.overworldDefinition(1);
    const Vec3 sameColor { 0.31f, 0.4f, 0.2f };
    constexpr GridPosition3 start { 0, 0, 1 }, localPortal { 0, 1, 1 },
        coveredPortal { 1, 1, 1 }, unconfiguredPortal { 0, 2, 1 },
        legacySelector { 3, 3, 1 }, dependentSelector { 2, 3, 1 };
    auto west = original;
    west.layers[1][start.y][start.x] = tileTypeToChar(TileType::Player);
    west.layers[1][localPortal.y][localPortal.x] = tileTypeToChar(TileType::PortalWest);
    west.portals = { { .cell = localPortal, .color = sameColor } };
    west.selectors = { { .id = 1, .cell = legacySelector, .target = LevelLocation { 0, 0 } } };
    auto east = original;
    east.layers[1][coveredPortal.y][coveredPortal.x] = tileTypeToChar(TileType::Rock);
    east.plates = { { .cell = coveredPortal, .tile = TileType::PortalNorth } };
    east.portals = { { .cell = coveredPortal, .color = sameColor } };
    east.layers[1][unconfiguredPortal.y][unconfiguredPortal.x] = tileTypeToChar(TileType::PortalEast);
    east.selectors = {
        { .id = 1, .cell = dependentSelector, .draftName = "Final Puzzle", .prerequisites = { { 1, 1 } } },
        { .id = 2, .cell = { 3, 3, 1 }, .draftName = "Final Puzzle" },
    };
    const OverworldDraftOverride fixture {
        .layout = OverworldLayout {
            .screenWidth = 4,
            .screenHeight = 4,
            .screens = { { .id = 1, .file = "screen1.scr" }, { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } } },
        },
        .definitions = { { 1, west }, { 2, east } },
    };
    store.saveOverworld("Diagnostics", fixture);
    CHECK(maker.loadDraft(store, "Diagnostics"));
    maker.toggleNeighbors();
    int checksOfFinal = 0;
    const auto problems = maker.problems([&](std::string_view draft) {
        if (draft == "Final Puzzle") ++checksOfFinal;
        return draft == "Final Puzzle";
    });
    CHECK(checksOfFinal == 1);
    CHECK(problems.size() == 5);
    const auto hasProblem = [&](Kind kind, OverworldScreenId screen, GridPosition3 cell, std::string_view detail) {
        return std::ranges::any_of(problems, [&](const auto& problem) {
            return problem.kind == kind && problem.screen == screen && problem.cell == cell &&
                problem.message.find(detail) != std::string::npos;
        });
    };
    CHECK(hasProblem(Kind::UnpairedPortal, 1, localPortal, "Connect"));
    CHECK(hasProblem(Kind::UnpairedPortal, 2, coveredPortal, "Connect"));
    CHECK(hasProblem(Kind::UnpairedPortal, 2, unconfiguredPortal, "Connect"));
    CHECK(hasProblem(Kind::BrokenLevelSelector, 1, legacySelector, "legacy"));
    CHECK(hasProblem(Kind::BrokenLevelSelector, 2, dependentSelector, "unlock"));
    CHECK(std::ranges::count(maker.problems([](std::string_view) { return false; }),
        Kind::BrokenLevelSelector, &MakerSession::Problem::kind) == 4);
    CHECK(maker.attachSelectorLevel(legacySelector, std::string("First Puzzle")));
    CHECK(std::ranges::count(maker.problems([](std::string_view) { return true; }),
        Kind::BrokenLevelSelector, &MakerSession::Problem::kind) == 0);
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

void testRectangleEditingAndClipboard()
{
    TEST("makerRectangleEditingAndClipboard");
    MakerSession maker;
    CHECK(maker.newDraft(7, 5));
    maker.selectTile(TileType::Wall);
    maker.rotateTile();
    const auto rotation = maker.editor().selectedTileQuarterTurns();
    CHECK(rotation != 0);
    CHECK(maker.selectArea({ 2, 2, 1 }, { 1, 1, 1 }));
    auto area = maker.selection();
    CHECK(area && area->min == (GridPosition3 { 1, 1, 1 }) && area->max == (GridPosition3 { 2, 2, 1 }));
    CHECK(!maker.canUndo());
    CHECK(maker.fillSelection());
    for (const auto cell : { GridPosition3 { 1, 1, 1 }, GridPosition3 { 2, 1, 1 },
             GridPosition3 { 1, 2, 1 }, GridPosition3 { 2, 2, 1 } }) {
        CHECK(tileAt(maker, cell) == TileType::Wall);
        CHECK(maker.editor().tileRotationQuarterTurns(cell) == rotation);
        CHECK(tileAt(maker, { cell.x, cell.y, 0 }) == TileType::Ground);
    }
    CHECK(maker.undo());
    CHECK(tileAt(maker, { 1, 1, 1 }) == TileType::Air);
    CHECK(tileAt(maker, { 2, 2, 1 }) == TileType::Air);
    CHECK(!maker.canUndo());
    CHECK(maker.redo());

    CHECK(maker.selectArea({ 2, 2, 1 }, { 2, 2, 1 }));
    CHECK(maker.clearSelectionTiles());
    maker.setTool(MakerSession::Tool::Paint);
    CHECK(maker.beginStroke({ 5, 2 }));
    CHECK(maker.endStroke());
    CHECK(tileAt(maker, { 5, 2, 1 }) == TileType::Wall);
    CHECK(maker.selectArea({ 1, 1, 1 }, { 2, 2, 1 }));
    CHECK(maker.copySelection());
    CHECK(maker.hasClipboard());
    auto footprint = maker.pastePreview({ 4, 1, 1 });
    CHECK(footprint && footprint->min == (GridPosition3 { 4, 1, 1 }) &&
        footprint->max == (GridPosition3 { 5, 2, 1 }));
    const auto beforePaste = maker.editor().documentDefinition();
    CHECK(!maker.pasteSelection({ 6, 1, 1 }));
    CHECK(maker.editor().documentDefinition() == beforePaste);
    CHECK(maker.pasteSelection({ 4, 1, 1 }));
    CHECK(tileAt(maker, { 4, 1, 1 }) == TileType::Wall);
    CHECK(tileAt(maker, { 5, 2, 1 }) == TileType::Air);
    CHECK(maker.editor().tileRotationQuarterTurns({ 4, 1, 1 }) == rotation);
    CHECK(maker.undo());
    CHECK(maker.editor().documentDefinition() == beforePaste);

    CHECK(maker.selectArea({ 1, 1, 1 }, { 2, 2, 1 }));
    CHECK(maker.cutSelection());
    CHECK(tileAt(maker, { 1, 1, 1 }) == TileType::Air);
    CHECK(tileAt(maker, { 2, 1, 1 }) == TileType::Air);
    CHECK(maker.undo());
    CHECK(maker.editor().documentDefinition() == beforePaste);
}

void testFloodFillKeepsBoundariesAndRollsBackInvalidBrush()
{
    TEST("makerFloodFillKeepsBoundariesAndRollsBackInvalidBrush");
    MakerSession maker;
    CHECK(maker.newDraft(5, 4));
    maker.selectTile(TileType::Wall);
    CHECK(maker.selectArea({ 2, 0, 1 }, { 2, 3, 1 }));
    CHECK(maker.fillSelection());
    maker.selectTile(TileType::Ground);
    maker.setTool(MakerSession::Tool::Paint);
    CHECK(maker.beginStroke({ 0, 0 }));
    CHECK(maker.endStroke());
    const auto before = maker.editor().documentDefinition();
    const auto beforeHistory = maker.editor().historyStats();
    maker.selectTile(TileType::Ladder);
    CHECK(!maker.floodFill({ 0, 1, 1 }));
    CHECK(maker.editor().documentDefinition() == before);
    CHECK(maker.editor().historyStats().undoRecords == beforeHistory.undoRecords);
    maker.selectTile(TileType::Rock);
    CHECK(maker.floodFill({ 0, 1, 1 }));
    for (int y = 0; y < 4; ++y) {
        CHECK(tileAt(maker, { 0, y, 1 }) == (y == 0 ? TileType::Ground : TileType::Rock));
        CHECK(tileAt(maker, { 1, y, 1 }) == TileType::Rock);
        CHECK(tileAt(maker, { 2, y, 1 }) == TileType::Wall);
        CHECK(tileAt(maker, { 3, y, 1 }) == TileType::Air);
        CHECK(tileAt(maker, { 4, y, 1 }) == TileType::Air);
    }
    CHECK(maker.undo());
    CHECK(maker.editor().documentDefinition() == before);
}

void testAreaClipboardKeepsInternalDeviceWiring()
{
    TEST("makerAreaClipboardKeepsInternalDeviceWiring");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newDraft(8, 3));
    auto definition = maker.editor().documentDefinition();
    const GridPosition3 gate { 2, 1, 1 }, internalPlate { 1, 1, 1 }, outsidePlate { 0, 1, 1 };
    definition.layers[1][1][0] = tileTypeToChar(TileType::PressurePlate);
    definition.layers[1][1][1] = tileTypeToChar(TileType::PressurePlate);
    definition.layers[1][1][2] = tileTypeToChar(TileType::Gate);
    const std::vector<GridPosition3> originalSources { outsidePlate, internalPlate };
    definition.gates.push_back({ .cell = gate, .pressurePlates = originalSources, .startOpen = true });
    store.save("Signals", definition);
    CHECK(maker.loadDraft(store, "Signals"));
    CHECK(maker.selectArea(internalPlate, gate));
    CHECK(maker.copySelection());
    CHECK(maker.pasteSelection({ 5, 1, 1 }));
    const auto result = maker.editor().documentDefinition();
    CHECK(result.gates.size() == 2);
    if (result.gates.size() != 2) return;
    const GridPosition3 pastedGate { 6, 1, 1 }, pastedPlate { 5, 1, 1 };
    const auto cloned = std::ranges::find(result.gates, pastedGate, &Level::Gate::cell);
    CHECK(cloned != result.gates.end());
    if (cloned == result.gates.end()) return;
    CHECK(cloned->pressurePlates == (std::vector { pastedPlate }));
    CHECK(cloned->startOpen);
    CHECK(maker.editor().linkSourcesForDevice(gate) == originalSources);
    CHECK(maker.undo());
    CHECK(tileAt(maker, pastedGate) == TileType::Air);
    CHECK(maker.editor().linkSourcesForDevice(gate) == originalSources);
}

void testOverworldClipboardAndSelectorCleanup()
{
    TEST("makerOverworldClipboardAndSelectorCleanup");
    MakerSession maker;
    CHECK(maker.newOverworldDraft(5, 3));
    maker.stepLayer(-1);
    maker.selectTile(TileType::Ground);
    CHECK(maker.beginStroke({ 5, 1 }));
    CHECK(maker.continueStroke(GridPosition { 6, 1 }));
    CHECK(maker.endStroke());
    CHECK(maker.overworldLayout().screens.size() == 2);
    if (maker.overworldLayout().screens.size() != 2) return;
    const auto eastId = maker.overworldLayout().screens.back().id;

    constexpr GridPosition3 source { 3, 1, 1 }, dependent { 6, 1, 1 };
    maker.stepLayer(1);
    maker.selectLevelSelectorBrush();
    for (const auto cell : { source, dependent }) {
        CHECK(maker.beginStroke(cell, MakerSession::StrokeMode::Brush));
        CHECK(maker.endStroke());
    }
    CHECK(maker.attachSelectorLevel(source, std::string("Puzzle")));
    maker.setTool(MakerSession::Tool::ConnectSelectors);
    CHECK(maker.clickLevelSelectorConnection(source));
    CHECK(maker.clickLevelSelectorConnection(dependent));
    CHECK(maker.levelSelectorConnections().size() == 1);
    maker.setTool(MakerSession::Tool::SelectArea);
    CHECK(maker.selectArea(source, source));
    CHECK(maker.copySelection());
    CHECK(maker.clearSelectionTiles());
    CHECK(!maker.levelSelectorAt(source));
    CHECK(maker.levelSelectorConnections().empty());
    CHECK(maker.undo());
    CHECK(maker.levelSelectorConnections().size() == 1);

    CHECK(maker.selectOverworldScreen(eastId));
    CHECK(maker.hasClipboard());
    constexpr GridPosition3 paste { 0, 1, 1 };
    const auto* before = maker.levelSelectorAt({ 1, 1, 1 });
    CHECK(before);
    if (!before) return;
    const uint32_t existingId = before->id;
    CHECK(maker.pasteSelection(paste));
    const auto* copied = maker.levelSelectorAt(paste);
    CHECK(copied && copied->id != existingId && copied->draftName == "Puzzle");
    CHECK(maker.levelSelectorConnections().size() == 1);
    CHECK(maker.undo());
    CHECK(!maker.levelSelectorAt(paste));
    CHECK(maker.levelSelectorConnections().size() == 1);
}

void testShortcutStrokesUseThePickedLayerAndStayUndoable()
{
    TEST("makerShortcutStrokesUseThePickedLayerAndStayUndoable");
    using Mode = MakerSession::StrokeMode;
    MakerSession session;
    session.setTool(MakerSession::Tool::Connect);
    CHECK(session.previewCell({ 3, 3, 0 }, Mode::Delete) == (GridPosition3 { 3, 3, 1 }));
    CHECK(session.beginStroke({ 3, 3, 0 }, Mode::Delete));
    CHECK(session.strokeMode() == Mode::Delete);
    CHECK(session.endStroke());
    CHECK(tileAt(session, { 3, 3, 1 }) == TileType::Air);
    CHECK(tileAt(session, { 3, 3, 0 }) == TileType::Ground);
    CHECK(session.tool() == MakerSession::Tool::Connect);
    CHECK(session.undo());
    CHECK(tileAt(session, { 3, 3, 1 }) == TileType::Rock);

    session.stepLayer(-1);
    session.toggleLayerLock();
    CHECK(session.editor().layerLocked());
    CHECK(session.previewCell({ 3, 3, 1 }, Mode::Delete) == (GridPosition3 { 3, 3, 0 }));
    session.toggleLayerLock();
    CHECK(!session.editor().layerLocked());
    session.stepLayer(1);
    session.selectTile(TileType::Wall);
    CHECK(!session.beginStroke({ 0, 6, 2 }, Mode::Replace));
    CHECK(session.beginStroke({ 0, 6, 0 }, Mode::Replace));
    CHECK(session.continueStroke(GridPosition3 { 3, 7, 0 }, true));
    CHECK(session.endStroke());
    for (int x = 0; x <= 3; ++x) {
        CHECK(tileAt(session, { x, 6, 0 }) == TileType::Wall);
        CHECK(tileAt(session, { x, 6, 1 }) == TileType::Air);
        CHECK(tileAt(session, { x, 7, 0 }) == TileType::Ground);
    }
    CHECK(session.undo());
    CHECK(tileAt(session, { 0, 6, 0 }) == TileType::Ground);
    CHECK(tileAt(session, { 3, 6, 0 }) == TileType::Ground);
}

void testShortcutMoveValidatesBoundsAndCancelsWithoutChangingDraft()
{
    TEST("makerShortcutMoveValidatesBoundsAndCancelsWithoutChangingDraft");
    MakerSession session;
    session.setTool(MakerSession::Tool::Connect);
    const GridPosition3 source { 3, 3, 1 };
    const GridPosition3 destination { 2, 3, 1 };
    CHECK(session.moveCell({ 3, 3, 0 }));
    CHECK(session.editor().pendingMove().has_value());
    CHECK(session.moveTarget({ 2, 3, 0 }) == destination);
    CHECK(!session.moveCell({ 4, 2, 1 }));
    CHECK(!session.moveCell({ -1, 3, 0 }));
    CHECK(!session.hasUnsavedChanges());
    CHECK(session.editor().documentDepth() == 2);
    session.cancelMove();
    CHECK(!session.editor().pendingMove());
    CHECK(!session.editor().canUndo());
    CHECK(tileAt(session, source) == TileType::Rock);

    CHECK(session.moveCell(source));
    CHECK(session.moveCell({ 2, 3, 0 }));
    CHECK(!session.editor().pendingMove());
    CHECK(session.tool() == MakerSession::Tool::Connect);
    CHECK(session.hasUnsavedChanges());
    CHECK(tileAt(session, source) == TileType::Air);
    CHECK(tileAt(session, destination) == TileType::Rock);
    CHECK(session.undo());
    CHECK(!session.hasUnsavedChanges());
    CHECK(tileAt(session, source) == TileType::Rock);
    CHECK(tileAt(session, destination) == TileType::Air);
    CHECK(!session.editor().canUndo());
}

void testShortcutEyedropperAndRecentTileSelection()
{
    TEST("makerShortcutEyedropperAndRecentTileSelection");
    MakerSession session;
    session.selectTile(TileType::Wall);
    session.rotateTile();
    CHECK(session.beginStroke({ 0, 4 }));
    CHECK(session.endStroke());
    session.selectTile(TileType::Rock);
    session.setTool(MakerSession::Tool::Erase);
    CHECK(session.pickTile({ 0, 4, 0 }));
    CHECK(session.tool() == MakerSession::Tool::Paint);
    CHECK(session.editor().selectedTile() == TileType::Wall);
    CHECK(session.editor().selectedTileQuarterTurns() == 1);
    CHECK(session.selectRecentTile(1));
    CHECK(session.editor().selectedTile() == TileType::Wall);
    CHECK(session.editor().selectedTileQuarterTurns() == 1);
    CHECK(session.selectRecentTile(0));
    CHECK(session.editor().selectedTile() == TileType::Rock);
    CHECK(!session.selectRecentTile(LevelEditor::recentTileCapacity));
    CHECK(session.undo());
    CHECK(!session.hasUnsavedChanges());
    CHECK(!session.editor().canUndo());
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

void testRecoverDraftsKeepsSavedIdentityAndActiveScreen()
{
    TEST("makerRecoverDraftsKeepsSavedIdentityAndActiveScreen");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession puzzle;
    CHECK(puzzle.saveDraft(store, "Original") == MakerSession::SaveDraftResult::Saved);
    const auto savedId = puzzle.draftId();
    CHECK(savedId.has_value());
    if (!savedId) return;

    const auto savedRevision = puzzle.editRevision();
    CHECK(puzzle.beginStroke({ 1, 5 }));
    CHECK(puzzle.endStroke());
    CHECK(puzzle.editRevision() > savedRevision);
    auto autosave = puzzle.recoverySnapshot();
    CHECK(autosave.kind() == MakerRecoveryKind::Puzzle && autosave.draftId == savedId);
    CHECK(store.renameLevel(*savedId, "Renamed").id == *savedId);

    MakerSession recoveredPuzzle;
    CHECK(recoveredPuzzle.restoreRecovery(std::move(autosave)));
    CHECK(recoveredPuzzle.hasUnsavedChanges() && recoveredPuzzle.draftId() == savedId);
    CHECK(tileAt(recoveredPuzzle, { 1, 5, 1 }) == TileType::Wall);
    CHECK(recoveredPuzzle.beginStroke({ 2, 5 }));
    CHECK(recoveredPuzzle.endStroke());
    CHECK(recoveredPuzzle.undo() && recoveredPuzzle.hasUnsavedChanges());
    CHECK(recoveredPuzzle.saveDraft(store, recoveredPuzzle.draftName(), true) == MakerSession::SaveDraftResult::Saved);
    CHECK(!recoveredPuzzle.hasUnsavedChanges() && recoveredPuzzle.draftName() == "Renamed");
    CHECK(recoveredPuzzle.draftId() == savedId);
    CHECK(store.list() == std::vector<std::string> { "Renamed" });

    MakerSession overworld;
    CHECK(overworld.newOverworldDraft(3, 3));
    overworld.stepLayer(-1);
    overworld.selectTile(TileType::Ground);
    CHECK(overworld.beginStroke({ 3, 1 }));
    CHECK(overworld.endStroke());
    const auto east = overworld.overworldLayout().screens.back().id;
    CHECK(east != overworld.currentScreenId());
    CHECK(overworld.selectOverworldScreen(east));
    auto worldAutosave = overworld.recoverySnapshot();
    CHECK(worldAutosave.kind() == MakerRecoveryKind::Overworld && worldAutosave.activeScreen == east);
    MakerSession recoveredOverworld;
    CHECK(recoveredOverworld.restoreRecovery(std::move(worldAutosave)));
    CHECK(recoveredOverworld.hasUnsavedChanges() && recoveredOverworld.currentScreenId() == east);
    CHECK(recoveredOverworld.overworldLayout().screens.size() == 2);
    CHECK(tileAt(recoveredOverworld, { 0, 1, 0 }) == TileType::Ground);
}

void testScreenCameraAnglesFollowTheDraft()
{
    TEST("makerScreenCameraAnglesFollowTheDraft");
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    MakerSession maker;
    constexpr CameraAngles angles { .pitchDegrees = 42.5f, .yawDegrees = -110.0f };
    CHECK(!maker.editor().cameraAngles());
    CHECK(!maker.setCameraAngles(std::nullopt));
    CHECK(!maker.setCameraAngles(CameraAngles { .pitchDegrees = 90.0f }));
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.editor().historyStats().undoRecords == 0);

    CHECK(maker.setCameraAngles(angles));
    CHECK(!maker.setCameraAngles(angles));
    CHECK(maker.editor().cameraAngles() == angles);
    CHECK(maker.hasUnsavedChanges());
    CHECK(maker.editor().historyStats().undoRecords == 1);
    CHECK(maker.undo());
    CHECK(!maker.editor().cameraAngles());
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.redo());
    CHECK(maker.editor().cameraAngles() == angles);

    CHECK(maker.resizeDraft(12, 9));
    CHECK(maker.editor().cameraAngles() == angles);
    CHECK(maker.saveDraft(store, "Custom Camera") == MakerSession::SaveDraftResult::Saved);
    CHECK(maker.setCameraAngles(std::nullopt));
    CHECK(!maker.editor().cameraAngles());
    CHECK(maker.hasUnsavedChanges());
    CHECK(maker.undo());
    CHECK(maker.editor().cameraAngles() == angles);
    CHECK(!maker.hasUnsavedChanges());

    MakerSession reopened;
    CHECK(reopened.loadDraft(store, "Custom Camera"));
    CHECK(reopened.editor().cameraAngles() == angles);
    const auto playtest = reopened.buildPlaytestLevel();
    CHECK(playtest.has_value());
    if (playtest) {
        CHECK(playtest->cameraAngles() == angles);
    }
    CHECK(reopened.newDraft(7, 3));
    CHECK(!reopened.editor().cameraAngles());
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

void testNativeLayerEditingAndDeletion()
{
    TEST("makerNativeLayerEditingAndDeletion");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newDraft(4, 3));
    auto definition = maker.editor().documentDefinition();
    definition.layers[1][1][0] = tileTypeToChar(TileType::Wall);
    definition.decorations.push_back({ .model = "Stone", .position = { 0.5f, 1.5f, 1.25f } });
    definition.decorations.push_back({ .model = "Stone", .position = { 1.5f, 1.5f, 0.75f } });
    store.save("Layers", definition);
    CHECK(maker.loadDraft(store, "Layers"));
    if (maker.editor().activeLayer() != 1) CHECK(maker.selectLayer(1));
    CHECK(maker.canAddLayer());
    CHECK(maker.addLayer(true));
    CHECK(maker.editor().documentDepth() == 3);
    CHECK(maker.editor().activeLayer() == 2);
    CHECK(tileAt(maker, { 0, 1, 2 }) == TileType::Air);
    CHECK(maker.setWaterLayer(true));
    CHECK(maker.editor().waterLayer() == 2U);
    CHECK(maker.selectLayer(1));
    CHECK(maker.addLayer(false));
    CHECK(maker.editor().documentDepth() == 4);
    CHECK(maker.editor().activeLayer() == 1);
    CHECK(maker.editor().waterLayer() == 3U);
    CHECK(tileAt(maker, { 0, 1, 2 }) == TileType::Wall);
    CHECK(maker.editor().decorations().front().position.z == 2.25f);

    CHECK(maker.selectLayer(2));
    const auto before = maker.editor().documentDefinition();
    const auto loss = maker.layerLoss();
    CHECK(loss.tiles == 1 && loss.decorations == 1 && !loss.water);
    CHECK(!maker.deleteLayer());
    CHECK(maker.editor().documentDefinition() == before);
    CHECK(maker.deleteLayer(true));
    CHECK(maker.editor().documentDepth() == 3);
    CHECK(maker.editor().waterLayer() == 2U);
    CHECK(maker.editor().decorations().size() == 1);
    if (maker.editor().decorations().size() == 1) {
        CHECK(maker.editor().decorations().front().position.z == 0.75f);
    }
    CHECK(maker.undo());
    CHECK(maker.editor().documentDefinition() == before);
    CHECK(maker.redo());
    CHECK(maker.editor().documentDepth() == 3);
    CHECK(maker.setWaterLayer(false));
    CHECK(!maker.editor().waterLayer());
    CHECK(maker.undo());
    CHECK(maker.editor().waterLayer() == 2U);
}

void testNativeDeviceProperties()
{
    TEST("makerNativeDeviceProperties");
    TemporaryDraftDirectory directory;
    const MakerDraftStore store(directory.root);
    MakerSession maker;
    CHECK(maker.newDraft(7, 4));
    constexpr GridPosition3 gate { 0, 1, 1 }, plate { 1, 1, 1 }, cart { 3, 2, 1 }, elevator { 6, 1, 1 };
    paintLinkTile(maker, gate, TileType::Gate);
    paintLinkTile(maker, plate, TileType::LockPlate);
    paintLinkTile(maker, plate, TileType::Rock);
    for (int x = 2; x <= 4; ++x) paintLinkTile(maker, { x, 2, 1 }, TileType::RailStopEastWest);
    paintLinkTile(maker, cart, TileType::Minecart);

    CHECK(maker.inspectTile(gate));
    CHECK(maker.tileProperties().has_value());
    CHECK(maker.setInspectedGateStartOpen(true));
    CHECK(maker.editor().gates().front().startOpen);
    CHECK(!maker.setInspectedLockPlateStartEnabled(true));
    CHECK(maker.undo());
    CHECK(!maker.editor().gates().front().startOpen);
    CHECK(maker.redo());

    CHECK(maker.inspectTile(plate));
    CHECK(maker.setInspectedLockPlateStartEnabled(true));
    CHECK(maker.editor().lockPlates().front().startEnabled);
    CHECK(maker.inspectTile(cart));
    const auto startingDirection = maker.editor().minecarts().front().initialDirection;
    const auto before = maker.editor().documentDefinition();
    CHECK(!maker.setInspectedMinecartDirection(0));
    CHECK(maker.editor().documentDefinition() == before);
    const uint8_t newDirection = startingDirection == 1 ? 3 : 1;
    CHECK(maker.setInspectedMinecartDirection(newDirection));
    CHECK(maker.editor().minecarts().front().initialDirection == newDirection);
    CHECK(maker.undo());
    CHECK(maker.editor().minecarts().front().initialDirection == startingDirection);
    CHECK(maker.redo());
    paintLinkTile(maker, elevator, TileType::Elevator);
    CHECK(maker.addLayer(true));
    CHECK(maker.inspectTile(elevator));
    CHECK(maker.toggleInspectedElevatorStop());
    CHECK(maker.editor().elevators().front().levels == (std::vector<int> { 1, 2 }));
    CHECK(maker.saveDraft(store, "Devices") == MakerSession::SaveDraftResult::Saved);
    const auto saved = store.load("Devices");
    CHECK(saved.gates.front().startOpen);
    CHECK(saved.lockPlates.front().startEnabled);
    CHECK(saved.minecarts.front().initialDirection == newDirection);
    CHECK(saved.elevators.front().levels == (std::vector<int> { 1, 2 }));
}

void testOverworldLayersKeepComponentsAndReferencesAligned()
{
    TEST("makerOverworldLayersKeepComponentsAndReferencesAligned");
    MakerSession maker;
    CHECK(maker.newOverworldDraft(4, 3));
    paintLinkTile(maker, { 1, 1, 1 }, TileType::Player);
    CHECK(maker.selectLayer(0));
    paintLinkTile(maker, { 4, 1, 0 }, TileType::Ground);
    CHECK(maker.overworldLayout().screens.size() == 2);
    if (maker.overworldLayout().screens.size() != 2) return;
    const auto east = maker.overworldLayout().screens.back().id;
    CHECK(maker.selectLayer(1));
    CHECK(maker.addLayer(true));
    CHECK(maker.overworldDefinition(east)->layers.size() == 3);
    CHECK(maker.setWaterLayer(true));
    CHECK(maker.overworldDefinition(1)->waterLayer == 2U);
    CHECK(maker.overworldDefinition(east)->waterLayer == 2U);

    constexpr GridPosition3 prerequisite { 2, 1, 2 }, dependent { 4, 1, 1 };
    maker.selectLevelSelectorBrush();
    CHECK(maker.beginStroke(prerequisite, MakerSession::StrokeMode::Brush));
    CHECK(maker.endStroke());
    CHECK(maker.selectLayer(1));
    CHECK(maker.beginStroke(dependent, MakerSession::StrokeMode::Brush));
    CHECK(maker.endStroke());
    maker.setTool(MakerSession::Tool::ConnectSelectors);
    CHECK(maker.clickLevelSelectorConnection(prerequisite));
    CHECK(maker.clickLevelSelectorConnection(dependent));
    CHECK(maker.levelSelectorConnections().size() == 1);

    CHECK(maker.selectLayer(2));
    const auto loss = maker.layerLoss();
    CHECK(loss.selectors == 1 && loss.water);
    CHECK(!maker.deleteLayer());
    CHECK(maker.deleteLayer(true));
    CHECK(maker.overworldDefinition(east)->layers.size() == 2);
    CHECK(!maker.overworldDefinition(east)->waterLayer);
    CHECK(!maker.levelSelectorAt(prerequisite));
    CHECK(maker.levelSelectorAt(dependent));
    CHECK(maker.levelSelectorConnections().empty());
    CHECK(maker.buildOverworldPlaytestMap().has_value());
    CHECK(maker.undo());
    CHECK(maker.overworldDefinition(east)->layers.size() == 3);
    CHECK(maker.overworldDefinition(east)->waterLayer == 2U);
    CHECK(maker.levelSelectorConnections().size() == 1);
    CHECK(maker.selectLayer(0));
    CHECK(maker.addLayer(false));
    CHECK(maker.overworldDefinition(east)->layers.size() == 4);
    CHECK(maker.overworldDefinition(east)->waterLayer == 3U);
    const auto shifted = maker.levelSelectorConnections();
    CHECK(shifted.size() == 1);
    if (shifted.size() == 1) {
        CHECK(shifted.front().source == (GridPosition3 { prerequisite.x, prerequisite.y, 3 }));
        CHECK(shifted.front().destination == (GridPosition3 { dependent.x, dependent.y, 2 }));
    }
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
    const GridPosition3 movedPortal { 3, 4, 1 };
    CHECK(maker.moveCell(first));
    CHECK(maker.moveCell({ movedPortal.x, movedPortal.y, 0 }));
    CHECK(maker.buildPlaytestLevel()->portalExit(movedPortal) == second);
    CHECK(maker.undo());
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
    const GridPosition3 movedRock { 3, 5, 1 };
    CHECK(maker.moveCell(rock));
    CHECK(maker.moveCell({ movedRock.x, movedRock.y, 0 }));
    CHECK(editor.linkEndpointColor({ movedRock, Kind::Object }) == group);
    CHECK(editor.linkEndpointColor({ ice, Kind::Object }) == group);
    CHECK(!editor.linkEndpointColor({ rock, Kind::Object }));
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
    definition.gates = { { .cell = first, .pressurePlates = firstSources, .color = color, .startOpen = true },
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

    const GridPosition3 movedSource { 3, 5, 1 };
    const GridPosition3 movedGate { 3, 4, 1 };
    const std::vector<GridPosition3> movedFirstSources { shared, movedSource };
    CHECK(maker.moveCell(a));
    CHECK(maker.moveCell({ movedSource.x, movedSource.y, 0 }));
    CHECK(maker.editor().linkSourcesForDevice(first) == movedFirstSources);
    CHECK(maker.editor().linkSourcesForDevice(second) == secondSources);
    CHECK(maker.editor().linkEndpointColor({ movedSource, Kind::Source }) == color);
    CHECK(maker.moveCell(first));
    CHECK(maker.moveCell({ movedGate.x, movedGate.y, 0 }));
    CHECK(maker.editor().linkSourcesForDevice(movedGate) == movedFirstSources);
    CHECK(maker.buildPlaytestLevel()->gateAt(movedGate)->startOpen);
    CHECK(maker.undo());
    CHECK(maker.editor().linkSourcesForDevice(first) == movedFirstSources);
    CHECK(maker.undo());
    CHECK(maker.editor().linkSourcesForDevice(first) == firstSources);
    CHECK(maker.editor().linkSourcesForDevice(second) == secondSources);
}

void testRecolorLinkGroupsPreservesNamespacesAndExplicitWires()
{
    TEST("makerRecolorLinkGroupsPreservesNamespacesAndExplicitWires");
    using Kind = LevelEditor::LinkEndpointKind;
    using Group = Level::LinkGroupKind;
    MakerSession maker;
    TemporaryDraftDirectory directory;
    MakerDraftStore store(directory.root);
    auto definition = maker.editor().documentDefinition();
    constexpr Vec3 originalColor { 0.1f, 0.2f, 0.3f };
    constexpr Vec3 deviceColor { 0.3f, 0.4f, 0.5f };
    constexpr Vec3 otherColor { 0.5f, 0.6f, 0.7f };
    constexpr Vec3 reservedColor { 0.7f, 0.8f, 0.9f };
    const GridPosition3 firstGate { 0, 4, 1 }, secondGate { 1, 4, 1 }, enemyGate { 2, 4, 1 };
    const GridPosition3 shared { 0, 5, 1 }, firstPlate { 1, 5, 1 }, secondPlate { 2, 5, 1 };
    const GridPosition3 rock = shared;
    const GridPosition3 ice { 1, 6, 1 }, otherRock { 2, 6, 1 };
    const GridPosition3 firstPortal { 0, 7, 1 }, secondPortal { 1, 7, 1 }, otherPortal { 2, 7, 1 };
    const auto setTile = [&](GridPosition3 cell, TileType tile) {
        definition.layers[static_cast<std::size_t>(cell.z)][static_cast<std::size_t>(cell.y)]
            [static_cast<std::size_t>(cell.x)] = tileTypeToChar(tile);
    };
    for (const auto cell : { firstGate, secondGate }) setTile(cell, TileType::Gate);
    setTile(enemyGate, TileType::EnemyGate);
    for (const auto cell : { firstPlate, secondPlate }) setTile(cell, TileType::PressurePlate);
    setTile(rock, TileType::Rock);
    setTile(ice, TileType::Ice);
    setTile(otherRock, TileType::Rock);
    for (const auto cell : { firstPortal, secondPortal, otherPortal }) setTile(cell, TileType::PortalNorth);

    const std::vector<GridPosition3> firstSources { shared, firstPlate };
    const std::vector<GridPosition3> secondSources { shared, secondPlate };
    definition.gates = {
        { .cell = firstGate, .pressurePlates = firstSources, .color = originalColor },
        { .cell = secondGate, .pressurePlates = secondSources, .color = originalColor },
        { .cell = enemyGate, .color = originalColor, .enemyGate = true },
    };
    definition.plates = { { .cell = shared, .tile = TileType::PressurePlate } };
    definition.linkColors = {
        { .cell = shared, .color = originalColor },
        { .cell = firstPlate, .color = originalColor },
        { .cell = secondPlate, .color = otherColor },
    };
    definition.objectLinks = {
        { .cell = rock, .color = originalColor },
        { .cell = ice, .color = originalColor },
        { .cell = otherRock, .color = deviceColor },
    };
    definition.portals = {
        { .cell = firstPortal, .color = originalColor },
        { .cell = secondPortal, .color = originalColor },
        { .cell = otherPortal, .color = deviceColor },
    };
    definition.linkGroupNames = {
        { .kind = Group::Device, .color = originalColor, .name = "Shared gates" },
        { .kind = Group::Device, .color = otherColor, .name = "Other signal" },
        { .kind = Group::Object, .color = originalColor, .name = "Moving together" },
        { .kind = Group::Object, .color = reservedColor, .name = "Empty sync group" },
    };
    store.save("Group colors", definition);
    CHECK(maker.loadDraft(store, "Group colors"));
    const auto& editor = maker.editor();
    const auto before = editor.documentDefinition();
    CHECK(maker.selectLinkEndpoint({ shared, Kind::Source }));
    CHECK(!maker.recolorSelectedLinkGroup(otherColor));
    CHECK(maker.status().find("already used") != std::string_view::npos);
    CHECK(!maker.recolorSelectedLinkGroup({ -1.0f, 0.0f, 0.0f }));
    CHECK(!maker.recolorSelectedLinkGroup({ 0.101f, 0.2f, 0.3f }));
    CHECK(!maker.hasUnsavedChanges());
    CHECK(!editor.canUndo());
    CHECK(editor.documentDefinition() == before);

    CHECK(maker.recolorSelectedLinkGroup(deviceColor));
    CHECK(maker.hasUnsavedChanges());
    CHECK(editor.linkEndpointColor({ firstGate, Kind::Device }) == deviceColor);
    CHECK(editor.linkEndpointColor({ secondGate, Kind::Device }) == deviceColor);
    CHECK(editor.linkEndpointColor({ shared, Kind::Source }) == deviceColor);
    CHECK(editor.linkEndpointColor({ firstPlate, Kind::Source }) == deviceColor);
    CHECK(editor.linkEndpointColor({ secondPlate, Kind::Source }) == otherColor);
    CHECK(editor.linkSourcesForDevice(firstGate) == firstSources);
    CHECK(editor.linkSourcesForDevice(secondGate) == secondSources);
    CHECK(editor.gates().at(2) == before.gates.at(2));
    CHECK(editor.linkGroupName(Group::Device, deviceColor) == "Shared gates");
    CHECK(editor.linkGroupName(Group::Device, originalColor).empty());
    CHECK(editor.linkGroupName(Group::Object, originalColor) == "Moving together");
    const auto afterDevice = editor.documentDefinition();
    CHECK(afterDevice.objectLinks == before.objectLinks);
    CHECK(afterDevice.portals == before.portals);
    CHECK(maker.undo());
    CHECK(editor.documentDefinition() == before);
    CHECK(!maker.hasUnsavedChanges());
    CHECK(maker.redo());
    CHECK(editor.documentDefinition() == afterDevice);

    CHECK(maker.selectLinkEndpoint({ rock, Kind::Object }));
    CHECK(!maker.recolorSelectedLinkGroup(deviceColor));
    CHECK(!maker.recolorSelectedLinkGroup(reservedColor));
    CHECK(editor.documentDefinition() == afterDevice);
    CHECK(maker.recolorSelectedLinkGroup(otherColor));
    CHECK(editor.linkEndpointColor({ rock, Kind::Object }) == otherColor);
    CHECK(editor.linkEndpointColor({ ice, Kind::Object }) == otherColor);
    CHECK(editor.linkEndpointColor({ otherRock, Kind::Object }) == deviceColor);
    CHECK(editor.linkGroupName(Group::Object, otherColor) == "Moving together");
    CHECK(editor.linkGroupName(Group::Device, otherColor) == "Other signal");

    CHECK(maker.selectLinkEndpoint({ firstPortal, Kind::Portal }));
    const auto beforePortals = editor.documentDefinition();
    CHECK(!maker.recolorSelectedLinkGroup(deviceColor));
    CHECK(editor.documentDefinition() == beforePortals);
    CHECK(maker.recolorSelectedLinkGroup(otherColor));
    CHECK(editor.linkEndpointColor({ firstPortal, Kind::Portal }) == otherColor);
    CHECK(editor.linkEndpointColor({ secondPortal, Kind::Portal }) == otherColor);
    CHECK(editor.linkEndpointColor({ otherPortal, Kind::Portal }) == deviceColor);
    CHECK(maker.undo());
    CHECK(editor.documentDefinition() == beforePortals);
    CHECK(maker.redo());

    CHECK(maker.saveDraft(store, "Group colors", true) == MakerSession::SaveDraftResult::Saved);
    MakerSession reopened;
    CHECK(reopened.loadDraft(store, "Group colors"));
    CHECK(reopened.editor().linkGroupName(Group::Device, deviceColor) == "Shared gates");
    CHECK(reopened.editor().linkGroupName(Group::Object, otherColor) == "Moving together");
    CHECK(reopened.editor().linkSourcesForDevice(firstGate) == firstSources);
    CHECK(reopened.editor().linkSourcesForDevice(secondGate) == secondSources);
    const auto level = reopened.buildPlaytestLevel();
    CHECK(level && level->portalExit(firstPortal) == secondPortal);
    CHECK(level && !level->portalExit(otherPortal));
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
    testPaintingAcrossOverworldSeam();
    testOverworldLorekeeperAndMetadata();
    testLockedOverworldLayerDoesNotFallBackInNeighbors();
    testLevelSelectorAttachmentsAndUnlockLinks();
    testOverworldPortalsPairAcrossScreens();
    testPuzzleProblemsFollowLiveEdits();
    testProblemsCheckPortalMetadataAndSelectorPrerequisites();
    testStarterIsIndependent();
    testStrokeClipsAndGroups();
    testBrushAndLayerChanges();
    testRectangleEditingAndClipboard();
    testFloodFillKeepsBoundariesAndRollsBackInvalidBrush();
    testAreaClipboardKeepsInternalDeviceWiring();
    testOverworldClipboardAndSelectorCleanup();
    testShortcutStrokesUseThePickedLayerAndStayUndoable();
    testShortcutMoveValidatesBoundsAndCancelsWithoutChangingDraft();
    testShortcutEyedropperAndRecentTileSelection();
    testEraseVisitsCoveredCellOnce();
    testRejectedPaintReportsPlacementRule();
    testPlacementHintsMatchPainting();
    testPlaytestSnapshotsUnsavedDraft();
    testPlaytestSolveKeepsEditorHistory();
    testPlaytestReportsInvalidDraft();
    testSaveAndLoadIncompleteDraft();
    testRecoverDraftsKeepsSavedIdentityAndActiveScreen();
    testScreenCameraAnglesFollowTheDraft();
    testSaveCollisionPreservesDraftAndHistory();
    testNewDraftStartsWithBlankBoardAndHistory();
    testNewDraftValidatesDimensionsBeforeReplacingBoard();
    testResizeDraftPreservesTilesAndHistory();
    testResizeDraftReportsAndRestoresCroppedTiles();
    testResizeDraftAcceptsEmptyEdgesAndExistingLargeBoards();
    testNativeLayerEditingAndDeletion();
    testNativeDeviceProperties();
    testOverworldLayersKeepComponentsAndReferencesAligned();
    testConnectDeviceAndPreserveExistingDrivers();
    testConnectPortalsAndMovableObjects();
    testConnectKeepsLegacyExplicitEdges();
    testRecolorLinkGroupsPreservesNamespacesAndExplicitWires();
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
