#include "TestHarness.hpp"

#include "engine/CampaignSession.hpp"

#include <array>
#include <iostream>
#include <utility>

namespace {

using namespace sokoban;

CampaignSession configuredCampaign()
{
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1 });
    campaign.setOverworldTargets({
        { 0, 0 },
        { 1, 0 },
        { 0, 1 },
        { 0, 1 },
    });
    return campaign;
}

void testCatalogAndProfileRestore()
{
    TEST("catalogAndProfileRestore");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;

    CHECK(campaign.levelCount() == 2);
    CHECK(campaign.screenCount(0) == 2);
    CHECK(campaign.overworldTargets().size() == 3);
    CHECK(campaign.restoreProfileLocation(profile));
    CHECK(campaign.inOverworld());
    CHECK(!campaign.startPuzzle(profile, { 2, 0 }));
    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    CHECK(!campaign.inOverworld());
    CHECK(campaign.location() == (LevelLocation { 0, 1 }));

    PlayerProfile invalidPuzzle;
    invalidPuzzle.worldContext = PlayerProfile::WorldContext::Puzzle;
    invalidPuzzle.currentLevel = 9;
    invalidPuzzle.currentScreen = 4;
    CHECK(!campaign.restoreProfileLocation(invalidPuzzle));
    CHECK(campaign.inOverworld());
    CHECK(invalidPuzzle.worldContext == PlayerProfile::WorldContext::Overworld);
}

void testIncompleteOverworldTargetsRemainRepairable()
{
    TEST("incompleteOverworldTargetsRemainRepairable");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1 });
    campaign.setOverworldTargets({ { 0, 0 }, { 1, 0 } });
    CHECK(campaign.overworldTargets().size() == 2);

    PlayerProfile profile;
    profile.recordScreenCompletion({ 0, 0 }, 3, 1.0);
    profile.recordScreenCompletion({ 1, 0 }, 3, 1.0);
    CHECK(!campaign.allTargetsCompleted(profile));

    bool rejected = false;
    try {
        campaign.setOverworldTargets(
            { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 9, 0 } });
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(campaign.overworldTargets().size() == 2);

    campaign.setOverworldTargets({ { 0, 0 }, { 0, 1 }, { 1, 0 } });
    profile.recordScreenCompletion({ 0, 1 }, 3, 1.0);
    CHECK(campaign.allTargetsCompleted(profile));
}

void testSelectorInteractionRequiresAllLivingPlayersTogether()
{
    TEST("selectorInteractionRequiresAllLivingPlayersTogether");
    const Level level = Level::loadFromLayers(
        {
            { "..." },
            { "C  " },
        },
        "selector interaction",
        std::nullopt,
        {},
        {
            { .id = 1, .cell = { 0, 0, 1 }, .target = LevelLocation { 0, 0 } },
            { .id = 2, .cell = { 1, 0, 1 }, .target = LevelLocation { 0, 1 } },
        });

    GameState state;
    CHECK(CampaignSession::selectorForInteraction(level, state) == nullptr);
    state.players.push_back({ .cell = { 0, 0, 1 } });
    const Level::ScreenSelector* selector =
        CampaignSession::selectorForInteraction(level, state);
    CHECK(selector != nullptr && selector->id == 1);

    state.players.push_back({ .cell = { 0, 0, 1 } });
    selector = CampaignSession::selectorForInteraction(level, state);
    CHECK(selector != nullptr && selector->id == 1);
    state.players[1].cell = { 1, 0, 1 };
    CHECK(CampaignSession::selectorForInteraction(level, state) == nullptr);
    state.players[1].cell = { 0, 0, 1 };
    state.players[1].dead = true;
    CHECK(CampaignSession::selectorForInteraction(level, state) == nullptr);
    state.players[1].dead = false;
    state.players[1].cell = { 2, 0, 1 };
    CHECK(CampaignSession::selectorForInteraction(level, state) == nullptr);
}

void testSelectorWorksDuringAnUnrelatedPatrol()
{
    TEST("selectorWorksDuringAnUnrelatedPatrol");
    const Level level = Level::loadFromLayers(
        { { ".......", ".......", "......." },
            { " }     ", "       ", " C     " } },
        "selector with robot patrol", std::nullopt, {},
        {
            { 1, { 1, 2, 1 }, LevelLocation { 1, 0 } },
            { 2, { 2, 2, 1 }, LevelLocation { 0, 0 } },
            { 3, { 3, 2, 1 }, LevelLocation { 0, 1 } },
        });

    GameplaySession gameplay;
    gameplay.reset(level);
    CHECK(rules::hasPendingMotion(level, gameplay.state()));
    CHECK(CampaignSession::selectorForInteraction(level, gameplay) ==
        &level.selectors()[0]);
    CHECK(gameplay.tryStartNextAction(level, {}));
    CHECK(gameplay.moving());
    CHECK(CampaignSession::selectorForInteraction(level, gameplay) ==
        &level.selectors()[0]);

    gameplay.queueMove(MoveDirection::Right);
    CHECK(gameplay.tryStartNextAction(level, {}));
    CHECK(CampaignSession::selectorForInteraction(level, gameplay) == nullptr);
    gameplay.advanceActiveAction(gameplay.stepDurationSeconds());
    gameplay.completeActiveAction();
    CHECK(gameplay.moving());
    CHECK(CampaignSession::selectorForInteraction(level, gameplay) ==
        &level.selectors()[1]);

    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    campaign.startNewGame(profile);
    campaign.finishWorldLoad(profile);
    CHECK(campaign.enterSelector(profile, { 0, 0 }, gameplay.snapshot()));
    CHECK(campaign.location() == (LevelLocation { 0, 0 }));
    const auto completed = campaign.completePuzzle(profile, 3, &level);
    CHECK(completed.nextPuzzle == (LevelLocation { 0, 1 }));
    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    const auto last = campaign.completePuzzle(profile, 3, &level);
    CHECK(!last.nextPuzzle);
    const auto returned = campaign.prepareWorldLoad(profile).snapshot;
    CHECK(returned.has_value());
    if (returned) {
        GameplaySession restored;
        CHECK(restored.restore(level, *returned, true));
        CHECK(CampaignSession::selectorForInteraction(level, restored) ==
            &level.selectors()[2]);
    }
}

void testSelectorWaitsForPartyMotion()
{
    TEST("selectorWaitsForPartyMotion");
    const Level conveyor = Level::loadFromLayers(
        { { "..." }, { "C> " } }, "selector on conveyor", std::nullopt, {},
        { { 1, { 1, 0, 1 }, LevelLocation { 0, 0 } } });
    GameplaySession gameplay;
    gameplay.reset(conveyor);
    gameplay.queueMove(MoveDirection::Right);
    CHECK(gameplay.tryStartNextAction(conveyor, {}));
    gameplay.advanceActiveAction(gameplay.activeActionRemainingSeconds());
    gameplay.completeActiveAction();
    CHECK(CampaignSession::selectorForInteraction(conveyor, gameplay.state()) ==
        &conveyor.selectors()[0]);
    CHECK(rules::hasPendingMotion(conveyor, gameplay.state()));
    CHECK(CampaignSession::selectorForInteraction(conveyor, gameplay) == nullptr);

    const Level platform = Level::loadFromLayers(
        { { "......" }, { "  }   " }, { "  C   " } },
        "robot carrying a hero");
    gameplay.reset(platform);
    CHECK(gameplay.readyForScreenSelection(platform));
    CHECK(gameplay.tryStartNextAction(platform, {}));
    CHECK(!gameplay.readyForScreenSelection(platform));
}

void testSelectorEntryAndBothCheckpointKinds()
{
    TEST("selectorEntryAndBothCheckpointKinds");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    campaign.startNewGame(profile);
    campaign.finishWorldLoad(profile);

    GameplaySession::Snapshot overworld;
    overworld.playerMoveCount = 6;
    CHECK(campaign.enterSelector(profile, { 1, 0 }, overworld));
    CHECK(!campaign.inOverworld());
    CHECK(profile.overworldCheckpoint.has_value());
    CHECK(profile.overworldCheckpoint->topologyFingerprint == 0);
    CHECK(profile.overworldCheckpoint->activeScreen == 1);
    CHECK(profile.overworldCheckpoint->session.playerMoveCount == 6);
    CHECK(profile.worldContext == PlayerProfile::WorldContext::Puzzle);

    campaign.finishWorldLoad(profile);
    campaign.addElapsedTime(1.25f);
    GameplaySession::Snapshot puzzle;
    puzzle.playerMoveCount = 7;
    puzzle.undoStack.resize(1);
    const GameplaySession::Action* undoStorage = puzzle.undoStack.data();
    campaign.writeCheckpoint(profile, std::move(puzzle));
    CHECK(profile.activeScreen.has_value());
    CHECK(profile.activeScreen->session.playerMoveCount == 7);
    CHECK(profile.activeScreen->session.undoStack.data() == undoStorage);
    CHECK(profile.activeScreen->levelElapsedSeconds == 1.25);

    CampaignSession resumed = configuredCampaign();
    CHECK(resumed.restoreProfileLocation(profile));
    CHECK(!resumed.inOverworld());
    const CampaignSession::WorldRestore puzzleRestore =
        resumed.prepareWorldLoad(profile);
    CHECK(puzzleRestore.checkpointMatched);
    CHECK(puzzleRestore.snapshot.has_value());
    CHECK(puzzleRestore.snapshot->playerMoveCount == 7);

    const CampaignSession::PuzzleCompleted completed =
        resumed.completePuzzle(profile, 7);
    CHECK(completed.location == (LevelLocation { 1, 0 }));
    CHECK(resumed.inOverworld());
    const CampaignSession::WorldRestore overworldRestore =
        resumed.prepareWorldLoad(profile);
    CHECK(overworldRestore.checkpointMatched);
    CHECK(overworldRestore.snapshot.has_value());
    CHECK(overworldRestore.snapshot->playerMoveCount == 6);
}

void testOverworldTopologyCheckpointValidation()
{
    TEST("overworldTopologyCheckpointValidation");
    CampaignSession campaign = configuredCampaign();
    campaign.setOverworldTopology(1234, { 9, 4 }, 4);
    PlayerProfile profile;
    campaign.startNewGame(profile);
    CHECK(campaign.activeOverworldScreen() == 4);
    CHECK(campaign.overworldScreenDiscovered(profile, 4));
    CHECK(!campaign.overworldScreenDiscovered(profile, 9));
    CHECK(!campaign.transitionOverworldScreen(profile, 7));
    CHECK(campaign.transitionOverworldScreen(profile, 9));
    CHECK(campaign.overworldScreenDiscovered(profile, 4));
    CHECK(campaign.overworldScreenDiscovered(profile, 9));
    CHECK(profile.overworldDiscovery.screens ==
        std::vector<OverworldScreenId>({ 4, 9 }));

    GameplaySession::Snapshot snapshot;
    snapshot.playerMoveCount = 12;
    campaign.writeCheckpoint(profile, snapshot);
    CHECK(profile.overworldCheckpoint.has_value());
    CHECK(profile.overworldCheckpoint->topologyFingerprint == 1234);
    CHECK(profile.overworldCheckpoint->activeScreen == 9);

    CampaignSession resumed = configuredCampaign();
    resumed.setOverworldTopology(1234, { 4, 9 }, 4);
    CHECK(resumed.restoreProfileLocation(profile));
    CHECK(resumed.activeOverworldScreen() == 9);
    CHECK(resumed.overworldScreenDiscovered(profile, 4));
    CHECK(resumed.overworldScreenDiscovered(profile, 9));
    const CampaignSession::WorldRestore restore =
        resumed.prepareWorldLoad(profile);
    CHECK(restore.checkpointMatched);
    CHECK(restore.snapshot.has_value());
    CHECK(restore.snapshot->playerMoveCount == 12);

    CampaignSession changed = configuredCampaign();
    changed.setOverworldTopology(5678, { 4, 9 }, 4);
    CHECK(!changed.restoreProfileLocation(profile));
    CHECK(changed.activeOverworldScreen() == 4);
    CHECK(!profile.overworldCheckpoint.has_value());
    CHECK(profile.overworldDiscovery.topologyFingerprint == 5678);
    CHECK(profile.overworldDiscovery.screens ==
        std::vector<OverworldScreenId>({ 4 }));
    CHECK(profile.worldContext == PlayerProfile::WorldContext::Overworld);
}

void testIndependentCompletionAndAllTargets()
{
    TEST("independentCompletionAndAllTargets");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    campaign.startNewGame(profile);

    CHECK(campaign.selectorViewState(profile, { 0, 0 }).status ==
        ScreenSelectorStatus::Playable);
    CHECK(campaign.selectorViewState(profile, { 0, 1 }).status ==
        ScreenSelectorStatus::Unavailable);
    CHECK(campaign.selectorViewState(profile, { 1, 0 }).status ==
        ScreenSelectorStatus::Playable);
    CHECK(campaign.selectorViewState(profile, { 0, 1 }).lastScreenInLevel);
    CHECK(campaign.selectorViewState(profile, { 1, 0 }).lastScreenInLevel);
    GameplaySession::Snapshot overworld;
    CHECK(!campaign.enterSelector(profile, { 0, 1 }, overworld));

    CHECK(campaign.startPuzzle(profile, { 1, 0 }));
    CHECK(profile.currentLevel == 1);
    CHECK(profile.unlockedLevel == 0);
    campaign.addElapsedTime(3.0f);
    CampaignSession::PuzzleCompleted first =
        campaign.completePuzzle(profile, 4);
    CHECK(first.location == (LevelLocation { 1, 0 }));
    CHECK(first.moves == 4);
    CHECK(first.timeSeconds == 3.0);
    CHECK(first.newBestMoves);
    CHECK(first.newBestTime);
    CHECK(!first.gameCompleted);
    CHECK(profile.screenCompleted({ 1, 0 }));
    CHECK(!campaign.allTargetsCompleted(profile));
    CHECK(campaign.selectorViewState(profile, { 0, 1 }).status ==
        ScreenSelectorStatus::Unavailable);

    CHECK(campaign.startPuzzle(profile, { 0, 0 }));
    campaign.addElapsedTime(2.0f);
    const CampaignSession::PuzzleCompleted middle =
        campaign.completePuzzle(profile, 3);
    CHECK(!middle.gameCompleted);
    CHECK(campaign.selectorViewState(profile, { 0, 0 }).status ==
        ScreenSelectorStatus::Solved);
    CHECK(campaign.selectorViewState(profile, { 0, 1 }).status ==
        ScreenSelectorStatus::Playable);

    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    const CampaignSession::PuzzleCompleted final =
        campaign.completePuzzle(profile, 2);
    CHECK(final.gameCompleted);
    CHECK(campaign.allTargetsCompleted(profile));
    CHECK(profile.screenCompleted({ 0, 1 }));
}

void testAdjacentPuzzlesChainFromSavedSelector()
{
    TEST("adjacentPuzzlesChainFromSavedSelector");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 5, 1 });
    const Level map = Level::loadFromLayers(
        { { ".....", ".....", "....." }, { "C    ", "     ", "     " } },
        "adjacent selectors", std::nullopt, {},
        {
            { 1, { 0, 0, 1 }, LevelLocation { 0, 0 } },
            { 2, { 1, 0, 1 }, LevelLocation { 0, 1 } },
            { 3, { 2, 0, 1 }, LevelLocation { 0, 2 } },
            { 4, { 2, 1, 1 }, LevelLocation { 0, 3 } },
            { 5, { 3, 2, 1 }, LevelLocation { 0, 4 } },
            { 6, { 3, 1, 1 }, LevelLocation { 1, 0 } },
            { 7, { 4, 2, 1 }, LevelLocation { 0, 3 } },
        });
    campaign.setOverworldSelectors(map.selectors());
    PlayerProfile profile;
    GameplaySession session;
    session.reset(map);
    const GameplaySession::Snapshot overworld = session.snapshot();
    CHECK(campaign.enterSelector(profile, { 0, 0 }, overworld));

    for (int screen = 0; screen < 4; ++screen) {
        CHECK(campaign.location() == (LevelLocation { 0, screen }));
        const auto completed = campaign.completePuzzle(profile, 2, &map);
        CHECK(profile.screenCompleted({ 0, screen }));
        if (screen < 3) {
            const LevelLocation next { 0, screen + 1 };
            CHECK(completed.nextPuzzle == next);
            CHECK(campaign.prepareWorldLoad(profile).snapshot == overworld);
            CHECK(campaign.startPuzzle(profile, next));
            campaign.writeCheckpoint(profile, {});
            CHECK(campaign.restoreProfileLocation(profile));
        } else {
            CHECK(!completed.nextPuzzle);
        }
    }
    CHECK(campaign.inOverworld());
    const auto saved = decodePlayerProfile(profile.serialize()).profile;
    const auto returned = campaign.prepareWorldLoad(saved).snapshot;
    CHECK(returned.has_value());
    CHECK(!session.restore(map, *returned));
    CHECK(session.restore(map, *returned, true));
    CHECK(session.state().players.front().cell == (GridPosition3 { 2, 1, 1 }));
    CHECK(session.playerMoveCount() == overworld.playerMoveCount);
    CHECK(session.undoCount() == 3);
}

void testChainedReturnStopsBeforeBlockedSelector()
{
    TEST("chainedReturnStopsBeforeBlockedSelector");
    constexpr GridPosition3 start { 0, 0, 1 };
    constexpr GridPosition3 safe { 1, 0, 1 };
    constexpr GridPosition3 blocked { 2, 0, 1 };
    for (const auto& [terrain, actors] : std::array {
             std::pair { "...", "C R" },
             std::pair { "...", "C N" },
             std::pair { "..W", "C  " },
         }) {
        const Level map = Level::loadFromLayers(
            { { terrain }, { actors } }, "blocked return selector", std::nullopt, {},
            {
                { 1, start, LevelLocation { 0, 0 } },
                { 2, safe, LevelLocation { 0, 1 } },
                { 3, blocked, LevelLocation { 0, 2 } },
            });
        CampaignSession campaign;
        campaign.setLevelScreenCounts({ 3 });
        campaign.setOverworldSelectors(map.selectors());
        PlayerProfile profile;
        GameplaySession session;
        session.reset(map);
        const auto original = session.snapshot();
        CHECK(campaign.enterSelector(profile, { 0, 0 }, original));
        for (int screen = 1; screen <= 2; ++screen) {
            const auto completed = campaign.completePuzzle(profile, 2, &map);
            const LevelLocation next { 0, screen };
            CHECK(completed.nextPuzzle == next);
            CHECK(campaign.startPuzzle(profile, next));
        }
        CHECK(!campaign.completePuzzle(profile, 2, &map).nextPuzzle);

        const auto returned = campaign.prepareWorldLoad(profile).snapshot;
        CHECK(returned.has_value());
        if (!returned) continue;
        CHECK(returned->state.players.front().cell == safe);
        CHECK(returned->state.movables == original.state.movables);
        CHECK(returned->state.enemies == original.state.enemies);
        CHECK(returned->playerMoveCount == original.playerMoveCount);
        CHECK(returned->undoStack.size() == 1);
        CHECK(session.restore(map, *returned, true));

        auto invalid = *returned;
        invalid.state.players.front().cell = blocked;
        invalid.undoStack.push_back({
            .before = returned->state,
            .after = invalid.state,
            .playerMoveCountBefore = returned->playerMoveCount,
            .playerMoveCountAfter = returned->playerMoveCount,
        });
        CHECK(!session.restore(map, invalid, true));
        CHECK(session.snapshot() == *returned);
    }
}

void testOverworldReturnMovesSharedParty()
{
    TEST("overworldReturnMovesSharedParty");
    constexpr GridPosition3 start { 0, 0, 1 };
    constexpr GridPosition3 destination { 1, 0, 1 };
    const Level map = Level::loadFromLayers(
        { { ".." }, { "C " } }, "party return", std::nullopt, {},
        {
            { 1, start, LevelLocation { 0, 0 } },
            { 2, destination, LevelLocation { 0, 1 } },
        });
    GameplaySession session;
    session.reset(map);
    auto party = session.snapshot();
    auto companion = party.state.players.front();
    ++companion.id;
    party.state.players.push_back(companion);
    CHECK(GameplaySession::appendOverworldReturnStep(
        map, party, map.selectors()[0], map.selectors()[1]));
    CHECK(party.state.players[0].cell == destination);
    CHECK(party.state.players[1].cell == destination);
}

void testPuzzleOnlyTimingAndDeferredCheckpoint()
{
    TEST("puzzleOnlyTimingAndDeferredCheckpoint");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    campaign.startNewGame(profile);
    campaign.addElapsedTime(5.0f);
    CHECK(campaign.puzzleElapsedSeconds() == 0.0);

    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    campaign.addElapsedTime(1.5f);
    CHECK(campaign.puzzleElapsedSeconds() == 1.5);
    CHECK(!campaign.deferCheckpoint());
    CHECK(!campaign.updateDeferredCheckpoint(1.9f, false, false));
    CHECK(campaign.updateDeferredCheckpoint(0.2f, false, false));
    CHECK(!campaign.updateDeferredCheckpoint(1.0f, false, true));
}

} // namespace

int main()
{
    try {
    testCatalogAndProfileRestore();
    testIncompleteOverworldTargetsRemainRepairable();
    testSelectorInteractionRequiresAllLivingPlayersTogether();
    testSelectorWorksDuringAnUnrelatedPatrol();
    testSelectorWaitsForPartyMotion();
    testSelectorEntryAndBothCheckpointKinds();
    testOverworldTopologyCheckpointValidation();
    testIndependentCompletionAndAllTargets();
    testAdjacentPuzzlesChainFromSavedSelector();
    testChainedReturnStopsBeforeBlockedSelector();
    testOverworldReturnMovesSharedParty();
    testPuzzleOnlyTimingAndDeferredCheckpoint();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

    if (failures == 0) {
        std::cout << "CampaignSessionTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "CampaignSessionTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
