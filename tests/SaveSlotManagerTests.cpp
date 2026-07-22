// Headless tests for the save-slot lifecycle: marker, settings sharing,
// summaries, switching, and deletion.

#include "engine/SaveSlotManager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* label)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

struct TemporaryDirectory {
    TemporaryDirectory()
    {
        std::mt19937_64 random(std::random_device {}());
        path_ = std::filesystem::temp_directory_path() /
            ("sokoban-slots-" + std::to_string(random()));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

constexpr auto instantWrites = std::chrono::milliseconds(0);

[[nodiscard]] bool directoryEmpty(const std::filesystem::path& path)
{
    return std::filesystem::directory_iterator(path) ==
        std::filesystem::directory_iterator();
}

[[nodiscard]] sokoban::PlayerProfile profileWithProgress(int level)
{
    sokoban::PlayerProfile profile;
    profile.unlockedLevel = level;
    profile.setCurrentScreen(level, 0);
    profile.recordReachedScreen(level, 0);
    profile.normalize();
    return profile;
}

void testFreshInstallWritesNothing()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    check(manager.activeSlot() == 0, "fresh install defaults to slot 1");

    const sokoban::PlayerProfile profile = manager.loadActiveProfile();
    check(profile == sokoban::PlayerProfile {}, "fresh profile is default");
    manager.flush();
    check(directoryEmpty(directory.path()), "fresh install writes no files");
}

void testPreSplitSettingsMigration()
{
    TemporaryDirectory directory;
    {
        // A pre-split combined save: progress and tuned settings together.
        sokoban::SaveStore legacy(directory.path());
        sokoban::PlayerProfile combined = profileWithProgress(1);
        combined.audio.musicVolume = 0.25f;
        combined.accessibility.highContrast = true;
        check(legacy.save(combined), "legacy combined save written");
    }

    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    const sokoban::PlayerProfile profile = manager.loadActiveProfile();
    check(profile.audio.musicVolume == 0.25f, "migrated settings adopted");
    check(profile.unlockedLevel == 1, "progress preserved through migration");
    manager.flush();
    check(std::filesystem::is_regular_file(directory.path() / "settings.json"),
        "shared settings file bootstrapped");

    // The shared file is now authoritative over slot copies.
    sokoban::SaveStore settings(directory.path(), "settings");
    sokoban::PlayerProfile shared = settings.load().profile;
    check(shared.audio.musicVolume == 0.25f, "bootstrapped settings persisted");
    shared.audio.musicVolume = 0.9f;
    check(settings.save(shared), "shared settings updated");

    sokoban::SaveSlotManager reloaded(directory.path(), instantWrites);
    const sokoban::PlayerProfile merged = reloaded.loadActiveProfile();
    check(merged.audio.musicVolume == 0.9f, "shared settings win over slot copy");
    check(merged.unlockedLevel == 1, "slot progress still intact");
}

void testSummariesSwitchingAndDeletion()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    sokoban::PlayerProfile active = manager.loadActiveProfile();

    // Slot 2 on disk with progress; slot 3 corrupt.
    {
        sokoban::SaveStore second(directory.path(), "profile-slot2");
        check(second.save(profileWithProgress(2)), "slot 2 saved");
        std::ofstream corrupt(
            directory.path() / "profile-slot3.json", std::ios::binary);
        corrupt << "not json";
    }

    std::vector<sokoban::SaveSlotManager::SlotSummary> summaries =
        manager.slotSummaries(active, 4);
    check(summaries.size() == 3, "three slot summaries");
    check(summaries[0].empty, "live empty profile summarizes as empty");
    check(!summaries[1].empty && summaries[1].currentLevel == 2,
        "on-disk slot summarized from its file");
    check(summaries[2].empty, "corrupt slot presents as empty");

    // The regression that shipped: a reset live profile must read as empty.
    active = profileWithProgress(1);
    check(!manager.slotSummaries(active, 4)[0].empty,
        "live progress summarizes as non-empty");
    active.resetProgress();
    check(manager.slotSummaries(active, 4)[0].empty,
        "reset live profile summarizes as empty again");

    // Switching: invalid and same-slot requests are rejected.
    check(!manager.switchTo(-1, active), "negative slot rejected");
    check(!manager.switchTo(3, active), "out-of-range slot rejected");
    check(!manager.switchTo(0, active), "same slot rejected");

    active.audio.musicVolume = 0.33f;
    std::optional<sokoban::PlayerProfile> switched = manager.switchTo(1, active);
    check(switched.has_value(), "switch to slot 2 succeeds");
    check(manager.activeSlot() == 1, "active slot updated");
    check(switched->unlockedLevel == 2, "slot 2 progress loaded");
    check(switched->audio.musicVolume == 0.33f, "live settings carried over");

    // The marker survives into a new manager instance.
    {
        sokoban::SaveSlotManager reopened(directory.path(), instantWrites);
        check(reopened.activeSlot() == 1, "marker remembers the active slot");
    }

    // Deletion removes files without touching neighbours; a pending write
    // must not resurrect the deleted save.
    manager.saveProgress(*switched, true);
    manager.deleteSlot(1);
    check(!std::filesystem::exists(directory.path() / "profile-slot2.json"),
        "deleted slot primary removed");
    check(!std::filesystem::exists(directory.path() / "profile-slot2.backup.json"),
        "deleted slot backup removed");
    check(std::filesystem::exists(directory.path() / "active-slot.txt"),
        "marker untouched by deletion");

    // Settings-only saves never contain progress.
    manager.saveSettings(*switched, true);
    manager.flush();
    sokoban::SaveStore settings(directory.path(), "settings");
    const sokoban::PlayerProfile sharedSettings = settings.load().profile;
    check(sharedSettings.progressEmpty(), "settings file carries no progress");
    check(sharedSettings.audio.musicVolume == 0.33f, "settings file has live values");
}

void testSummaryCacheInvalidation()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    sokoban::PlayerProfile active = manager.loadActiveProfile();

    // Seed the two non-active slots on disk before the first read.
    {
        sokoban::SaveStore s1(directory.path(), "profile-slot2");
        check(s1.save(profileWithProgress(2)), "slot 2 seeded");
        sokoban::SaveStore s2(directory.path(), "profile-slot3");
        check(s2.save(profileWithProgress(1)), "slot 3 seeded");
    }
    check(manager.slotSummaries(active, 4)[1].currentLevel == 2, "slot 2 decoded");
    check(manager.slotSummaries(active, 4)[2].currentLevel == 1, "slot 3 decoded");

    // An external overwrite of a cached non-active slot is intentionally not
    // reflected: only this process mutates slots during a run.
    {
        sokoban::SaveStore s1(directory.path(), "profile-slot2");
        check(s1.save(profileWithProgress(3)), "slot 2 overwritten externally");
    }
    check(manager.slotSummaries(active, 4)[1].currentLevel == 2, "cache reused");

    // The real update path: write the active slot, then switch away. The
    // switch invalidates the cache so the now-non-active slot 0 reflects its
    // saved progress (the behavior the pre-cache code had).
    active = profileWithProgress(1);
    active.audio.musicVolume = 0.4f;
    manager.saveProgress(active, true);
    manager.flush();
    std::optional<sokoban::PlayerProfile> switched = manager.switchTo(1, active);
    check(switched.has_value(), "switch to slot 2");
    check(manager.slotSummaries(*switched, 4)[0].currentLevel == 1,
        "switched-away slot reflects its saved progress");
    // Slot 2 (now the freshly-decoded on-disk value) shows the external write.
    check(manager.slotSummaries(*switched, 4)[1].currentLevel == 3,
        "switch invalidation re-decodes each non-active slot");

    // Deleting a non-active slot invalidates just that entry to empty.
    check(manager.slotSummaries(*switched, 4)[2].currentLevel == 1,
        "slot 3 primed before delete");
    manager.deleteSlot(2);
    check(manager.slotSummaries(*switched, 4)[2].empty,
        "delete invalidates the slot's cached summary");
    check(manager.slotSummaries(*switched, 4)[0].currentLevel == 1,
        "delete leaves other cached summaries intact");

    // A changed level count invalidates completed flags across the board.
    // Fresh directory so no active-slot marker bleeds in; slot 1 stays
    // non-active (the cached-decode path).
    TemporaryDirectory levelCountDir;
    sokoban::SaveSlotManager fresh(levelCountDir.path(), instantWrites);
    const sokoban::PlayerProfile freshActive = fresh.loadActiveProfile();
    {
        sokoban::PlayerProfile complete;
        complete.recordLevelCompletion(0, 5, 1.0, true);
        complete.recordLevelCompletion(1, 5, 1.0, true);
        complete.normalize();
        sokoban::SaveStore s(levelCountDir.path(), "profile-slot2");
        check(s.save(complete), "slot 2 completed 0 and 1");
    }
    check(fresh.slotSummaries(freshActive, 2)[1].completed,
        "2-level catalog marks the slot complete");
    check(!fresh.slotSummaries(freshActive, 5)[1].completed,
        "5-level catalog re-evaluates completion");
}

void testFailedMarkerCommitRollsBackSwitch()
{
    TemporaryDirectory directory;
    {
        sokoban::SaveStore second(directory.path(), "profile-slot2");
        check(second.save(profileWithProgress(1)), "slot 2 seeded for rollback");
        sokoban::SaveStore third(directory.path(), "profile-slot3");
        check(third.save(profileWithProgress(2)), "slot 3 seeded for rollback");
    }

    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    sokoban::PlayerProfile active = manager.loadActiveProfile();
    std::optional<sokoban::PlayerProfile> third = manager.switchTo(2, active);
    check(third.has_value() && manager.activeSlot() == 2,
        "precondition switch to slot 3 succeeds");

    // A non-empty directory at the temporary path reliably prevents the
    // marker's atomic writer from creating its temporary file.
    const std::filesystem::path blockedTemporary =
        directory.path() / "active-slot.txt.tmp";
    std::filesystem::create_directories(blockedTemporary);
    std::ofstream(blockedTemporary / "blocker.txt") << "blocked";

    bool threw = false;
    try {
        (void)manager.switchTo(1, *third);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "marker commit failure is reported");
    check(manager.activeSlot() == 2,
        "failed switch keeps previous active slot");

    std::ifstream marker(directory.path() / "active-slot.txt");
    int markedSlot = 0;
    marker >> markedSlot;
    check(markedSlot == 3, "failed switch preserves previous marker");

    sokoban::PlayerProfile replacement = profileWithProgress(0);
    manager.saveProgress(replacement, true);
    manager.flush();
    check(sokoban::SaveStore(directory.path(), "profile-slot3").load()
            .profile.currentLevel == 0,
        "post-failure saves still target previous slot");
    check(sokoban::SaveStore(directory.path(), "profile-slot2").load()
            .profile.currentLevel == 1,
        "failed destination is not used by later saves");
}

} // namespace

int main()
{
    testFreshInstallWritesNothing();
    testPreSplitSettingsMigration();
    testSummariesSwitchingAndDeletion();
    testSummaryCacheInvalidation();
    testFailedMarkerCommitRollsBackSwitch();

    if (failures == 0) {
        std::cout << "SaveSlotManagerTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SaveSlotManagerTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
