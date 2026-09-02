#include "TestHarness.hpp"

#include "engine/CampaignSession.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/OverworldMap.hpp"
#include "engine/OverworldView.hpp"
#include "engine/render/IsoScenePreparer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sokoban;

void checkThrowsContaining(
    const std::function<void()>& operation,
    std::string_view expected)
{
    try {
        operation();
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK(std::string_view(error.what()).find(expected) !=
            std::string_view::npos);
    } catch (...) {
        CHECK(false);
    }
}

class TestProject {
public:
    explicit TestProject(std::string_view name)
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
            ("sokoban_overworld_map_" + std::string(name) + "_" +
                std::to_string(unique));
        std::filesystem::create_directories(root);
    }

    ~TestProject()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void writeScreen(
        OverworldScreenId id,
        const Level::Definition& definition) const
    {
        const std::filesystem::path path =
            root / ("screen" + std::to_string(id) + ".scr");
        std::ofstream file(path, std::ios::trunc);
        if (!file) {
            throw std::runtime_error("cannot write test screen");
        }
        for (const std::string& line :
             Level::serializeDefinition(definition)) {
            file << line << '\n';
        }
    }

    void writeLayout(const OverworldLayout& layout) const
    {
        writeOverworldLayout(root / "layout.json", layout);
    }

    std::filesystem::path root;
};

Level::Definition westDefinition(
    std::vector<Level::ScreenSelector> selectors = {})
{
    return {
        .layers = {
            { "...", "..." },
            { "  #", " C " },
        },
        .selectors = std::move(selectors),
    };
}

Level::Definition eastDefinition(
    std::vector<Level::ScreenSelector> selectors = {})
{
    return {
        .layers = {
            { "...", "..." },
            { "#  ", "   " },
        },
        .selectors = std::move(selectors),
    };
}

OverworldLayout eastWestLayout()
{
    return {
        .format = 3,
        .screenWidth = 3,
        .screenHeight = 2,
        .screens = {
            { .id = 1, .file = "screen1.scr", .slot = { 0, 0 } },
            { .id = 2, .file = "screen2.scr", .slot = { 1, 0 } },
        },
    };
}

void finishAction(GameplaySession& session)
{
    session.advanceActiveAction(session.activeActionDuration());
    CHECK(session.activeActionComplete());
    session.completeActiveAction();
}

void move(GameplaySession& session, const Level& level, MoveDirection direction)
{
    session.queueMove(direction);
    CHECK(session.tryStartNextAction(level, {}));
    finishAction(session);
}

void testLayoutRoundTripIsCanonical()
{
    TEST("layoutRoundTripIsCanonical");
    TestProject project("layout_round_trip");
    OverworldLayout authored = eastWestLayout();
    std::ranges::reverse(authored.screens);

    project.writeLayout(authored);
    const OverworldLayout loaded =
        loadOverworldLayout(project.root / "layout.json");
    CHECK(loaded.screens.size() == 2);
    CHECK(loaded.screens[0].id == 1);
    CHECK(loaded.screens[1].id == 2);

    project.writeLayout(loaded);
    CHECK(loadOverworldLayout(project.root / "layout.json") == loaded);
}

void testCompositionAndGameplayCrossASeam()
{
    TEST("compositionAndGameplayCrossASeam");
    TestProject project("composition");
    project.writeScreen(1, westDefinition({
        {
            .id = 7,
            .cell = { 1, 0, 1 },
            .target = LevelLocation { .level = 0, .screen = 0 },
        },
    }));
    project.writeScreen(2, eastDefinition());
    project.writeLayout(eastWestLayout());

    const OverworldMap map = OverworldMap::load(project.root);
    CHECK(map.level().width() == 6);
    CHECK(map.level().height() == 2);
    CHECK(map.level().depth() == 2);
    CHECK(map.level().playerStart() == GridPosition3({ 1, 1, 1 }));
    CHECK(map.screenAt({ 2, 1, 1 }) ==
        std::optional<OverworldScreenId> { 1 });
    CHECK(map.screenAt({ 3, 1, 1 }) ==
        std::optional<OverworldScreenId> { 2 });
    CHECK(map.toGlobal(2, { 0, 1, 1 }) ==
        std::optional<GridPosition3>({ 3, 1, 1 }));
    CHECK(map.toLocal(2, { 3, 1, 1 }) ==
        std::optional<GridPosition3>({ 0, 1, 1 }));
    CHECK(map.selectors().size() == 1);
    CHECK(map.selectors()[0].screen == 1);
    CHECK(map.selectors()[0].localId == 7);
    CHECK(map.selectors()[0].runtimeId == 1);
    CHECK(map.selectors()[0].globalCell == GridPosition3({ 1, 0, 1 }));
    CHECK(map.visibleNeighborhood(1) ==
        std::vector<OverworldScreenId>({ 1, 2 }));
    CHECK(map.fingerprint() != 0);
    CHECK(OverworldMap::load(project.root).fingerprint() == map.fingerprint());

    GameplaySession session;
    session.reset(map.level());
    session.setStepDurationSeconds(0.01f);
    move(session, map.level(), MoveDirection::Right);
    CHECK(session.state().players[0].cell == GridPosition3({ 2, 1, 1 }));
    move(session, map.level(), MoveDirection::Right);
    CHECK(session.state().players[0].cell == GridPosition3({ 3, 1, 1 }));
    CHECK(map.screenAt(session.state().players[0].cell) ==
        std::optional<OverworldScreenId> { 2 });

    CampaignSession campaign;
    campaign.setOverworldTopology(
        map.fingerprint(), { 1, 2 }, map.startScreen());
    PlayerProfile profile;
    campaign.startNewGame(profile);
    CHECK(CampaignSession::sharedPlayerScreen(map, session.state()) ==
        std::optional<OverworldScreenId> { 2 });
    CHECK(campaign.transitionOverworldScreen(2));
    CHECK(campaign.activeOverworldScreen() == 2);
    GameState splitPlayers = session.state();
    splitPlayers.players.push_back(splitPlayers.players.front());
    splitPlayers.players.back().cell = { 2, 1, 1 };
    CHECK(!CampaignSession::sharedPlayerScreen(map, splitPlayers));

    session.queueUndo();
    CHECK(session.tryStartNextAction(map.level(), {}));
    finishAction(session);
    CHECK(session.state().players[0].cell == GridPosition3({ 2, 1, 1 }));
    CHECK(map.screenAt(session.state().players[0].cell) ==
        std::optional<OverworldScreenId> { 1 });
}

void testNegativeSlotsNormalizeWithoutChangingIdentity()
{
    TEST("negativeSlotsNormalizeWithoutChangingIdentity");
    TestProject project("negative_slots");
    project.writeScreen(1, westDefinition());
    project.writeScreen(2, eastDefinition());
    OverworldLayout layout = eastWestLayout();
    layout.screens[0].slot = { -5, -3 };
    layout.screens[1].slot = { -4, -3 };
    project.writeLayout(layout);

    const OverworldMap map = OverworldMap::load(project.root);
    CHECK(map.normalizationOffset() == GridPosition({ 15, 6 }));
    CHECK(map.screen(1)->origin == GridPosition({ 0, 0 }));
    CHECK(map.screen(2)->origin == GridPosition({ 3, 0 }));
    CHECK(map.level().playerStart() == GridPosition3({ 1, 1, 1 }));
}

void testActionAdmissionAndCameraTransition()
{
    TEST("actionAdmissionAndCameraTransition");
    TestProject project("view_transition");
    project.writeScreen(1, westDefinition());
    project.writeScreen(2, eastDefinition());
    project.writeLayout(eastWestLayout());
    const OverworldMap map = OverworldMap::load(project.root);

    GameState committed = rules::initialState(map.level());
    committed.players.front().cell = { 2, 1, 1 };
    GameState projected = committed;
    projected.players.front().cell = { 3, 1, 1 };

    const OverworldView halfway = calculateOverworldView(
        map, 1, committed, projected, { 2.5f, 1.0f, 1.0f });
    CHECK(halfway.sourceScreen == 1);
    CHECK(halfway.destinationScreen ==
        std::optional<OverworldScreenId> { 2 });
    CHECK(std::abs(halfway.transitionProgress - 0.5f) < 0.0001f);
    CHECK(halfway.cameraExtent.originX == 0);
    CHECK(halfway.cameraExtent.originY == 0);
    CHECK(halfway.cameraExtent.width == 3);
    CHECK(halfway.cameraExtent.height == 2);
    CHECK(halfway.cameraExtent.depth == map.screen(1)->depth);
    CHECK(std::abs(halfway.cameraOffset.x - 1.5f) < 0.0001f);
    CHECK(std::abs(halfway.cameraOffset.y) < 0.0001f);
    CHECK(halfway.visibleScreens ==
        std::vector<OverworldScreenId>({ 1, 2 }));

    const OverworldView overview = calculateOverworldView(
        map, 1, committed, committed, { 2.0f, 1.0f, 1.0f }, 1.0f);
    CHECK(overview.overviewProgress == 1.0f);
    CHECK(overview.overviewCameraExtent.originX == 0);
    CHECK(overview.overviewCameraExtent.originY == 0);
    CHECK(overview.overviewCameraExtent.width == 6);
    CHECK(overview.overviewCameraExtent.height == 2);
    CHECK(overview.visibleScreens ==
        std::vector<OverworldScreenId>({ 1, 2 }));

    RenderFrameData settledFrame;
    settledFrame.viewMode = RenderViewMode::Isometric3D;
    settledFrame.levelWidth = map.level().width();
    settledFrame.levelHeight = map.level().height();
    settledFrame.levelDepth = map.level().depth();
    settledFrame.cameraExtent = halfway.cameraExtent;
    RenderFrameData movingFrame = settledFrame;
    movingFrame.cameraOffset = halfway.cameraOffset;
    RenderFrameData overviewFrame = settledFrame;
    overviewFrame.cameraExtentTransitionTarget =
        overview.overviewCameraExtent;
    overviewFrame.cameraExtentTransitionProgress = 1.0f;
    IsoScenePreparer preparer;
    PreparedRenderScene settledScene;
    PreparedRenderScene movingScene;
    PreparedRenderScene overviewScene;
    preparer.prepare(settledFrame, { 1280.0f, 720.0f }, settledScene);
    preparer.prepare(movingFrame, { 1280.0f, 720.0f }, movingScene);
    preparer.prepare(overviewFrame, { 1280.0f, 720.0f }, overviewScene);
    CHECK(std::abs(
        movingScene.isoLayout.cameraPosition.x -
        settledScene.isoLayout.cameraPosition.x - 1.5f) < 0.0001f);
    CHECK(std::abs(
        movingScene.isoLayout.cameraPosition.y -
        settledScene.isoLayout.cameraPosition.y) < 0.0001f);
    CHECK(std::abs(overviewScene.isoLayout.cameraPosition.x - 3.0f) < 0.0001f);
    CHECK(overviewScene.isoLayout.cameraPosition.z >
        settledScene.isoLayout.cameraPosition.z);

    GameState split = projected;
    split.players.push_back(split.players.front());
    split.players.back().cell = { 2, 1, 1 };
    CHECK(!overworldActionStateAllowed(map, split));
    split.players.back().dead = true;
    CHECK(overworldActionStateAllowed(map, split));

    GameplaySession session;
    session.reset(map.level());
    session.setActionAdmissionPolicy(
        [](const GameState&) { return false; });
    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(map.level(), {}));
    CHECK(!session.moving());
    CHECK(session.state().players.front().cell == GridPosition3({ 1, 1, 1 }));
    session.clearActionAdmissionPolicy();
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(map.level(), {}));
    finishAction(session);
    CHECK(session.state().players.front().cell == GridPosition3({ 2, 1, 1 }));
}

void testLayoutValidationAndIndependentScreens()
{
    TEST("layoutValidationAndIndependentScreens");

    {
        TestProject project("duplicate_slot");
        project.writeScreen(1, westDefinition());
        project.writeScreen(2, eastDefinition());
        OverworldLayout layout = eastWestLayout();
        layout.screens[1].slot = layout.screens[0].slot;
        checkThrowsContaining(
            [&] { project.writeLayout(layout); },
            "duplicate slot");
    }

    {
        TestProject project("diagonal_screen");
        project.writeScreen(1, westDefinition());
        project.writeScreen(2, eastDefinition());
        OverworldLayout layout = eastWestLayout();
        layout.screens[1].slot = { 1, 1 };
        project.writeLayout(layout);
        const OverworldMap map = OverworldMap::load(project.root);
        CHECK(map.screens().size() == 2);
        CHECK(map.screen(2)->slot == OverworldSlot({ 1, 1 }));
    }

    {
        TestProject project("blocked_seam");
        Level::Definition blockedWest = westDefinition();
        Level::Definition blockedEast = eastDefinition();
        blockedEast.layers[1][1][0] = '#';
        project.writeScreen(1, blockedWest);
        project.writeScreen(2, blockedEast);
        project.writeLayout(eastWestLayout());
        const OverworldMap map = OverworldMap::load(project.root);
        CHECK(map.screens().size() == 2);

        GameplaySession session;
        session.reset(map.level());
        move(session, map.level(), MoveDirection::Right);
        CHECK(session.state().players[0].cell == GridPosition3({ 2, 1, 1 }));
        session.queueMove(MoveDirection::Right);
        CHECK(!session.tryStartNextAction(map.level(), {}));
        CHECK(session.state().players[0].cell == GridPosition3({ 2, 1, 1 }));
    }

    {
        TestProject project("component_end");
        Level::Definition invalid = westDefinition();
        invalid.layers[1][0][0] = 'E';
        project.writeScreen(1, invalid);
        project.writeScreen(2, eastDefinition());
        project.writeLayout(eastWestLayout());
        checkThrowsContaining(
            [&] { (void)OverworldMap::load(project.root); },
            "may not contain 'E'");
    }

    {
        TestProject project("missing_player");
        Level::Definition noPlayer = westDefinition();
        noPlayer.layers[1][1][1] = ' ';
        project.writeScreen(1, noPlayer);
        project.writeScreen(2, eastDefinition());
        project.writeLayout(eastWestLayout());
        checkThrowsContaining(
            [&] { (void)OverworldMap::load(project.root); },
            "exactly one Player tile; found 0");
    }

    {
        TestProject project("multiple_players");
        Level::Definition secondPlayer = eastDefinition();
        secondPlayer.layers[1][1][1] = 'C';
        project.writeScreen(1, westDefinition());
        project.writeScreen(2, secondPlayer);
        project.writeLayout(eastWestLayout());
        checkThrowsContaining(
            [&] { (void)OverworldMap::load(project.root); },
            "exactly one Player tile; found 2");
    }
}

void testSelectorOwnershipAndCoverage()
{
    TEST("selectorOwnershipAndCoverage");
    const std::vector<int> puzzleScreens { 2, 1 };

    {
        TestProject project("selector_valid");
        project.writeScreen(1, westDefinition({
            { .id = 1, .cell = { 0, 0, 1 },
              .target = LevelLocation { 0, 0 } },
            { .id = 2, .cell = { 1, 0, 1 },
              .target = LevelLocation { 0, 1 } },
            { .id = 3, .cell = { 1, 1, 1 },
              .target = LevelLocation { 1, 0 } },
            { .id = 4, .cell = { 0, 1, 1 }, .target = std::nullopt },
        }));
        project.writeScreen(2, eastDefinition());
        project.writeLayout(eastWestLayout());
        const OverworldMap map = OverworldMap::load(project.root);
        map.validatePuzzleSelectors(
            puzzleScreens, OverworldValidationMode::Production);
    }

    {
        TestProject project("selector_split");
        project.writeScreen(1, westDefinition({
            { .id = 1, .cell = { 0, 0, 1 },
              .target = LevelLocation { 0, 0 } },
        }));
        project.writeScreen(2, eastDefinition({
            { .id = 1, .cell = { 1, 0, 1 },
              .target = LevelLocation { 0, 1 } },
        }));
        project.writeLayout(eastWestLayout());
        const OverworldMap map = OverworldMap::load(project.root);
        checkThrowsContaining(
            [&] {
                map.validatePuzzleSelectors(
                    puzzleScreens,
                    OverworldValidationMode::Production);
            },
            "more than one overworld screen");
    }

    {
        TestProject project("selector_incomplete");
        project.writeScreen(1, westDefinition({
            { .id = 1, .cell = { 0, 0, 1 },
              .target = LevelLocation { 0, 0 } },
            { .id = 2, .cell = { 1, 0, 1 }, .target = std::nullopt },
        }));
        project.writeScreen(2, eastDefinition());
        project.writeLayout(eastWestLayout());
        const OverworldMap map = OverworldMap::load(project.root);
        map.validatePuzzleSelectors(
            puzzleScreens, OverworldValidationMode::Structural);
        checkThrowsContaining(
            [&] {
                map.validatePuzzleSelectors(
                    puzzleScreens,
                    OverworldValidationMode::Production);
            },
            "no selector for puzzle level 0 screen 1");
    }
}

// The draft-override guard in load(): every override names a real screen, and
// names it once.
//
// This existed and threw for four cases, and nothing tested any of them. It
// was found by mutation: deleting the whole check left all 82 checks passing.
void testDraftOverridesMustNameEachScreenOnce()
{
    TEST("draftOverridesMustNameEachScreenOnce");
    TestProject project("draft-overrides");
    project.writeScreen(1, westDefinition());
    project.writeScreen(2, eastDefinition());
    project.writeLayout(eastWestLayout());

    // A screen id of zero is not a screen.
    checkThrowsContaining(
        [&] {
            (void)OverworldMap::load(
                project.root,
                OverworldDraftOverride {
                    .definitions = { { .screen = 0,
                        .definition = westDefinition() } },
                });
        },
        "missing or duplicate screen override");

    // The same screen twice: the second would silently win.
    checkThrowsContaining(
        [&] {
            (void)OverworldMap::load(
                project.root,
                OverworldDraftOverride {
                    .definitions = {
                        { .screen = 1, .definition = westDefinition() },
                        { .screen = 1, .definition = westDefinition() },
                    },
                });
        },
        "missing or duplicate screen override");

    // A duplicate anywhere in the list, not just adjacent to its twin.
    checkThrowsContaining(
        [&] {
            (void)OverworldMap::load(
                project.root,
                OverworldDraftOverride {
                    .definitions = {
                        { .screen = 1, .definition = westDefinition() },
                        { .screen = 2, .definition = eastDefinition() },
                        { .screen = 1, .definition = westDefinition() },
                    },
                });
        },
        "missing or duplicate screen override");

    // Distinct, non-zero ids are accepted, and the override is what loads:
    // the two screens swap shapes, so the composition has the same extent as
    // the file-backed one and a different identity.
    const OverworldMap overridden = OverworldMap::load(
        project.root,
        OverworldDraftOverride {
            .definitions = {
                { .screen = 1, .definition = eastDefinition() },
                { .screen = 2, .definition = westDefinition() },
            },
        });
    const OverworldMap plain = OverworldMap::load(project.root);
    CHECK(overridden.level().width() == plain.level().width());
    CHECK(overridden.level().height() == plain.level().height());
    CHECK(overridden.fingerprint() != plain.fingerprint());

    // An override naming a screen the layout does not contain is deliberately
    // *not* this guard's job, and says so with a different message: the guard
    // only rejects ids that are zero or repeated, and the screen loop is what
    // notices an override nothing consumed.
    checkThrowsContaining(
        [&] {
            (void)OverworldMap::load(
                project.root,
                OverworldDraftOverride {
                    .definitions = { { .screen = 9,
                        .definition = westDefinition() } },
                });
        },
        "references a missing screen");
}

} // namespace

int main()
{
    testLayoutRoundTripIsCanonical();
    testCompositionAndGameplayCrossASeam();
    testNegativeSlotsNormalizeWithoutChangingIdentity();
    testActionAdmissionAndCameraTransition();
    testLayoutValidationAndIndependentScreens();
    testSelectorOwnershipAndCoverage();
    testDraftOverridesMustNameEachScreenOnce();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
