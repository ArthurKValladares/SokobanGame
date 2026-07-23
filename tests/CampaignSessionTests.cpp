#include "engine\CampaignSession.hpp"

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

void testLocationRestoreAndLevelStart()
{
    TEST("locationRestoreAndLevelStart");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1 });
    PlayerProfile profile;
    profile.currentLevel = 9;
    profile.currentScreen = 4;

    CHECK(!campaign.restoreProfileLocation(profile));
    CHECK(campaign.location() == LevelLocation {});
    CHECK(profile.currentLevel == 0);
    CHECK(profile.currentScreen == 0);
    CHECK(!campaign.startLevel(profile, 2, 0));
    CHECK(campaign.startLevel(profile, 0, 1));
    CHECK(campaign.location() == (LevelLocation { 0, 1 }));
    CHECK(profile.currentLevel == 0);
    CHECK(profile.currentScreen == 1);
}

void testCheckpointCadenceAndRestore()
{
    TEST("checkpointCadenceAndRestore");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 1 });
    PlayerProfile profile;
    GameplaySession::Snapshot snapshot;
    snapshot.playerMoveCount = 7;

    campaign.finishScreenLoad(profile);
    campaign.addElapsedTime(1.25f);
    campaign.writeCheckpoint(profile, snapshot);
    CHECK(profile.activeScreen.has_value());
    CHECK(profile.activeScreen->session.playerMoveCount == 7);
    CHECK(profile.activeScreen->levelElapsedSeconds == 1.25);

    const CampaignSession::ScreenRestore restore =
        campaign.prepareScreenLoad(profile);
    CHECK(restore.checkpointMatched);
    CHECK(restore.snapshot.has_value());
    CHECK(restore.snapshot->playerMoveCount == 7);

    CHECK(!campaign.deferCheckpoint());
    CHECK(!campaign.updateDeferredCheckpoint(1.9f, false, false));
    CHECK(campaign.updateDeferredCheckpoint(0.2f, false, false));
    CHECK(!campaign.updateDeferredCheckpoint(1.0f, false, true));
}

void testScreenAndLevelCompletion()
{
    TEST("screenAndLevelCompletion");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2, 1 });
    PlayerProfile profile;
    campaign.startNewGame(profile);
    campaign.finishScreenLoad(profile);
    campaign.addElapsedTime(3.0f);

    CampaignSession::AdvanceResult result =
        campaign.advanceScreen(profile, 4);
    CHECK(std::holds_alternative<CampaignSession::ScreenAdvanced>(result));
    CHECK(campaign.currentScreen() == 1);
    campaign.finishScreenLoad(profile);
    campaign.addElapsedTime(2.0f);

    result = campaign.advanceScreen(profile, 3);
    CHECK(std::holds_alternative<CampaignSession::LevelCompleted>(result));
    const CampaignSession::LevelCompleted& completed =
        std::get<CampaignSession::LevelCompleted>(result);
    CHECK(completed.moves == 7);
    CHECK(completed.timeSeconds == 5.0);
    CHECK(completed.newBestMoves);
    CHECK(completed.newBestTime);
    CHECK(completed.hasNextLevel);
    CHECK(profile.progressForLevel(0)->completed);

    campaign.resolveLevelComplete(profile);
    CHECK(campaign.location() == (LevelLocation { 1, 0 }));
    result = campaign.advanceScreen(profile, 2);
    CHECK(std::holds_alternative<CampaignSession::GameCompleted>(result));
    CHECK(campaign.allLevelsCompleted(profile));
}

void testLaterScreenRunDoesNotRecordBests()
{
    TEST("laterScreenRunDoesNotRecordBests");
    CampaignSession campaign;
    campaign.setLevelScreenCounts({ 2 });
    PlayerProfile profile;
    CHECK(campaign.startLevel(profile, 0, 1));
    campaign.finishScreenLoad(profile);

    const CampaignSession::AdvanceResult result =
        campaign.advanceScreen(profile, 1);
    CHECK(std::holds_alternative<CampaignSession::GameCompleted>(result));
    const CampaignSession::LevelCompleted& completed =
        std::get<CampaignSession::GameCompleted>(result).finalLevel;
    CHECK(!completed.newBestMoves);
    CHECK(!completed.newBestTime);
    const PlayerProfile::LevelProgress* progress = profile.progressForLevel(0);
    CHECK(progress != nullptr);
    CHECK(!progress->bestMoves.has_value());
    CHECK(!progress->bestTimeSeconds.has_value());
}

} // namespace

int main()
{
    testLocationRestoreAndLevelStart();
    testCheckpointCadenceAndRestore();
    testScreenAndLevelCompletion();
    testLaterScreenRunDoesNotRecordBests();

    if (failures == 0) {
        std::cout << "CampaignSessionTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "CampaignSessionTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
