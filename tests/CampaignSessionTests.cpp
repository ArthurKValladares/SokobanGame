#include "engine/CampaignSession.hpp"

#include <iostream>

namespace {

using namespace sokoban;

int failures = 0;
int checks = 0;
const char* currentTest = "";

void checkImpl(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line
                  << ": " << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)
#define TEST(name) currentTest = name

CampaignSession configuredCampaign()
{
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1 });
    campaign.setOverworldTargets({
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
    CHECK(campaign.overworldTargets().size() == 2);
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

void testSelectorEntryAndBothCheckpointKinds()
{
    TEST("selectorEntryAndBothCheckpointKinds");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    campaign.startNewGame(profile);
    campaign.finishWorldLoad(profile);

    GameplaySession::Snapshot overworld;
    overworld.playerMoveCount = 6;
    CHECK(campaign.enterSelector(profile, { 0, 1 }, overworld));
    CHECK(!campaign.inOverworld());
    CHECK(profile.overworldSession.has_value());
    CHECK(profile.overworldSession->playerMoveCount == 6);
    CHECK(profile.worldContext == PlayerProfile::WorldContext::Puzzle);

    campaign.finishWorldLoad(profile);
    campaign.addElapsedTime(1.25f);
    GameplaySession::Snapshot puzzle;
    puzzle.playerMoveCount = 7;
    campaign.writeCheckpoint(profile, puzzle);
    CHECK(profile.activeScreen.has_value());
    CHECK(profile.activeScreen->session.playerMoveCount == 7);
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
    CHECK(completed.location == (LevelLocation { 0, 1 }));
    CHECK(resumed.inOverworld());
    const CampaignSession::WorldRestore overworldRestore =
        resumed.prepareWorldLoad(profile);
    CHECK(overworldRestore.checkpointMatched);
    CHECK(overworldRestore.snapshot.has_value());
    CHECK(overworldRestore.snapshot->playerMoveCount == 6);
}

void testIndependentCompletionAndAllTargets()
{
    TEST("independentCompletionAndAllTargets");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    campaign.startNewGame(profile);

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

    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    campaign.addElapsedTime(2.0f);
    const CampaignSession::PuzzleCompleted final =
        campaign.completePuzzle(profile, 3);
    CHECK(final.gameCompleted);
    CHECK(campaign.allTargetsCompleted(profile));
    CHECK(profile.screenCompleted({ 0, 1 }));
}

void testDebugCompletionDoesNotRecordBests()
{
    TEST("debugCompletionDoesNotRecordBests");
    CampaignSession campaign = configuredCampaign();
    PlayerProfile profile;
    CHECK(campaign.startPuzzle(profile, { 0, 1 }));
    campaign.addElapsedTime(4.0f);

    const CampaignSession::PuzzleCompleted completed =
        campaign.completePuzzle(profile, 5, false);
    CHECK(!completed.newBestMoves);
    CHECK(!completed.newBestTime);
    const PlayerProfile::ScreenProgress* progress =
        profile.progressForScreen({ 0, 1 });
    CHECK(progress != nullptr);
    CHECK(progress->completed);
    CHECK(!progress->bestMoves.has_value());
    CHECK(!progress->bestTimeSeconds.has_value());
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
    testSelectorInteractionRequiresAllLivingPlayersTogether();
    testSelectorEntryAndBothCheckpointKinds();
    testIndependentCompletionAndAllTargets();
    testDebugCompletionDoesNotRecordBests();
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
