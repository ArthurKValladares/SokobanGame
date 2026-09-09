// Headless tests for the save-slot lifecycle: marker, settings sharing,
// summaries, switching, and deletion.

#include "engine/SaveSlotManager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

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

[[nodiscard]] std::string futureProfileContents()
{
    std::string contents = sokoban::PlayerProfile {}.serialize();
    const std::string current =
        "\"format\": " + std::to_string(sokoban::currentPlayerProfileFormat);
    const std::size_t position = contents.find(current);
    if (position == std::string::npos) {
        throw std::runtime_error("serialized profile has no format field");
    }
    contents.replace(
        position,
        current.size(),
        "\"format\": " +
            std::to_string(sokoban::currentPlayerProfileFormat + 1));
    return contents;
}

void testFreshInstallWritesNothing()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    check(manager.activeSlot() == 0, "fresh install defaults to slot 1");

    const sokoban::PlayerProfile profile = manager.loadActiveProfile();
    check(profile == sokoban::PlayerProfile {}, "fresh profile is default");
    check(manager.flush().allPersisted(),
        "fresh profile flush reports both channels settled");
    check(directoryEmpty(directory.path()), "fresh install writes no files");
}

void testUnsupportedActiveDataStopsLoading()
{
    TemporaryDirectory directory;
    const std::string future = futureProfileContents();
    {
        std::ofstream stream(
            directory.path() / "profile.json",
            std::ios::binary | std::ios::trunc);
        stream << future;
    }

    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    bool threw = false;
    try {
        (void)manager.loadActiveProfile();
    } catch (const std::runtime_error& error) {
        threw = std::string_view(error.what()).find("unsupported") !=
            std::string_view::npos;
    }
    check(threw, "unsupported active profile stops loading with an accurate error");

    std::ifstream stream(directory.path() / "profile.json", std::ios::binary);
    const std::string preserved {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>() };
    check(preserved == future,
        "stopped active-profile load preserves the future-version file");
    check(!std::filesystem::exists(directory.path() / "settings.json"),
        "stopped active-profile load does not create replacement settings");

    TemporaryDirectory inactiveDirectory;
    sokoban::SaveSlotManager inactiveManager(
        inactiveDirectory.path(), instantWrites);
    const sokoban::PlayerProfile active = inactiveManager.loadActiveProfile();
    const std::filesystem::path inactivePath =
        inactiveDirectory.path() / "profile-slot2.json";
    {
        std::ofstream inactiveStream(
            inactivePath, std::ios::binary | std::ios::trunc);
        inactiveStream << future;
    }
    check(inactiveManager.slotSummaries(active, 4)[1].state ==
            sokoban::SaveSlotState::Unavailable,
        "unsupported inactive profile is presented as unavailable");
    bool switchThrew = false;
    try {
        (void)inactiveManager.switchTo(1, active);
    } catch (const std::runtime_error& error) {
        switchThrew = std::string_view(error.what()).find("unsupported") !=
            std::string_view::npos;
    }
    check(switchThrew,
        "switching to an unsupported profile stops with an accurate error");
    std::ifstream inactiveStream(inactivePath, std::ios::binary);
    const std::string inactivePreserved {
        std::istreambuf_iterator<char>(inactiveStream),
        std::istreambuf_iterator<char>() };
    check(inactivePreserved == future,
        "rejected slot switch preserves the future-version profile");
}

void testInterruptedActiveSlotMarkerRecovery()
{
    TemporaryDirectory temporary;
    const std::filesystem::path marker = temporary.path() / "active-slot.txt";
    {
        std::ofstream stream(marker.string() + ".tmp");
        stream << "2\n";
    }

    sokoban::SaveSlotManager temporaryRecovered(temporary.path(), instantWrites);
    check(temporaryRecovered.activeSlot() == 1,
        "temporary active-slot marker is promoted at startup");
    check(!std::filesystem::exists(marker.string() + ".tmp"),
        "promoted active-slot temporary is removed");

    TemporaryDirectory displacedDirectory;
    const std::filesystem::path displacedMarker =
        displacedDirectory.path() / "active-slot.txt";
    {
        std::ofstream stream(displacedMarker.string() + ".tmp");
        stream << "invalid";
    }
    {
        std::ofstream stream(displacedMarker.string() + ".replace-old");
        stream << "3\n";
    }

    sokoban::SaveSlotManager displacedRecovered(
        displacedDirectory.path(), instantWrites);
    check(displacedRecovered.activeSlot() == 2,
        "displaced active-slot marker restores after a corrupt temporary");
    check(!std::filesystem::exists(displacedMarker.string() + ".tmp") &&
            !std::filesystem::exists(
                displacedMarker.string() + ".replace-old"),
        "active-slot fallback cleans both artifacts");
}

void testOverworldTargetSummaries()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    sokoban::PlayerProfile profile = manager.loadActiveProfile();
    const std::vector<sokoban::LevelLocation> targets {
        { 0, 1 },
        { 1, 0 },
    };

    profile.recordScreenCompletion({ 0, 1 }, 4, 3.0);
    auto summaries = manager.slotSummaries(profile, targets);
    check(summaries[0].state == sokoban::SaveSlotState::Ready,
        "screen completion makes overworld slot ready");
    check(summaries[0].completedLevels == 1,
        "summary counts completed selector targets");
    check(!summaries[0].completed,
        "one unsolved target keeps slot incomplete");
    check(summaries[0].currentLevel == -1,
        "overworld slot has no current puzzle level");

    profile.recordScreenCompletion({ 1, 0 }, 2, 1.0);
    summaries = manager.slotSummaries(profile, targets);
    check(summaries[0].completedLevels == 2,
        "all solved selector targets are counted");
    check(summaries[0].completed,
        "all selector targets complete the slot");
}

void testPreSplitSettingsMigration()
{
    TemporaryDirectory directory;
    {
        // A pre-split combined save: progress and tuned settings together.
        sokoban::SaveStore legacy(directory.path());
        sokoban::PlayerProfile combined = profileWithProgress(1);
        combined.settings.audio.musicVolume = 0.25f;
        check(legacy.save(combined), "legacy combined save written");
    }

    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    const sokoban::PlayerProfile profile = manager.loadActiveProfile();
    check(profile.settings.audio.musicVolume == 0.25f, "migrated settings adopted");
    check(profile.unlockedLevel == 1, "progress preserved through migration");
    check(manager.flush().allPersisted(),
        "settings migration reports durable completion");
    check(std::filesystem::is_regular_file(directory.path() / "settings.json"),
        "shared settings file bootstrapped");

    // The shared file is now authoritative over slot copies.
    sokoban::SaveStore settings(directory.path(), "settings");
    sokoban::PlayerProfile shared = settings.load().profile;
    check(shared.settings.audio.musicVolume == 0.25f, "bootstrapped settings persisted");
    shared.settings.audio.musicVolume = 0.9f;
    check(settings.save(shared), "shared settings updated");

    sokoban::SaveSlotManager reloaded(directory.path(), instantWrites);
    const sokoban::PlayerProfile merged = reloaded.loadActiveProfile();
    check(merged.settings.audio.musicVolume == 0.9f, "shared settings win over slot copy");
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
    check(summaries[0].state == sokoban::SaveSlotState::Empty,
        "live empty profile summarizes as empty");
    check(summaries[1].state == sokoban::SaveSlotState::Ready &&
            summaries[1].currentLevel == 2,
        "on-disk slot summarized from its file");
    check(summaries[2].state == sokoban::SaveSlotState::Corrupt,
        "corrupt slot is reported as corrupt");

    bool corruptSwitchRejected = false;
    try {
        (void)manager.switchTo(2, active);
    } catch (const std::runtime_error&) {
        corruptSwitchRejected = true;
    }
    check(corruptSwitchRejected, "corrupt slot cannot be switched into");
    check(manager.activeSlot() == 0,
        "rejected corrupt switch keeps active slot");
    check(std::filesystem::is_regular_file(
            directory.path() / "profile-slot3.json"),
        "rejected corrupt switch does not archive the save");

    // The regression that shipped: a reset live profile must read as empty.
    active = profileWithProgress(1);
    check(manager.slotSummaries(active, 4)[0].state ==
            sokoban::SaveSlotState::Ready,
        "live progress summarizes as non-empty");
    active.resetProgress();
    check(manager.slotSummaries(active, 4)[0].state ==
            sokoban::SaveSlotState::Empty,
        "reset live profile summarizes as empty again");

    // Switching: invalid and same-slot requests are rejected.
    check(!manager.switchTo(-1, active), "negative slot rejected");
    check(!manager.switchTo(3, active), "out-of-range slot rejected");
    check(!manager.switchTo(0, active), "same slot rejected");

    active.settings.audio.musicVolume = 0.33f;
    std::optional<sokoban::PlayerProfile> switched = manager.switchTo(1, active);
    check(switched.has_value(), "switch to slot 2 succeeds");
    check(manager.activeSlot() == 1, "active slot updated");
    check(switched->unlockedLevel == 2, "slot 2 progress loaded");
    check(switched->settings.audio.musicVolume == 0.33f, "live settings carried over");

    // The marker survives into a new manager instance.
    {
        sokoban::SaveSlotManager reopened(directory.path(), instantWrites);
        check(reopened.activeSlot() == 1, "marker remembers the active slot");
    }

    // Deletion removes files without touching neighbours; a pending write
    // must not resurrect the deleted save.
    manager.saveProgress(*switched, true);
    check(manager.deleteSlot(1).succeeded, "slot 2 deletion succeeds");
    check(!std::filesystem::exists(directory.path() / "profile-slot2.json"),
        "deleted slot primary removed");
    check(!std::filesystem::exists(directory.path() / "profile-slot2.backup.json"),
        "deleted slot backup removed");
    check(std::filesystem::is_regular_file(
            directory.path() / "profile-slot2.deleted"),
        "deleted slot retains its durable deletion marker");
    check(std::filesystem::exists(directory.path() / "active-slot.txt"),
        "marker untouched by deletion");

    // Settings-only saves never contain progress.
    manager.saveSettings(*switched, true);
    check(manager.flush().allPersisted(),
        "settings-only save reports durable completion");
    sokoban::SaveStore settings(directory.path(), "settings");
    const sokoban::PlayerProfile sharedSettings = settings.load().profile;
    check(sharedSettings.progressEmpty(), "settings file carries no progress");
    check(sharedSettings.settings.audio.musicVolume == 0.33f, "settings file has live values");
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
    active.settings.audio.musicVolume = 0.4f;
    manager.saveProgress(active, true);
    check(manager.flush().allPersisted(),
        "progress save reports durable completion before switching");
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
    check(manager.deleteSlot(2).succeeded, "slot 3 deletion succeeds");
    check(manager.slotSummaries(*switched, 4)[2].state ==
            sokoban::SaveSlotState::Empty,
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

void testSlotInspectionIsNonMutating()
{
    TemporaryDirectory directory;
    {
        sokoban::SaveStore recoverable(directory.path(), "profile-slot2");
        check(recoverable.save(profileWithProgress(1)),
            "recoverable slot initial profile saved");
        check(recoverable.save(profileWithProgress(2)),
            "recoverable slot backup created");
        std::ofstream(recoverable.primaryPath(), std::ios::binary | std::ios::trunc)
            << "corrupt primary";
    }
    const std::filesystem::path unavailable =
        directory.path() / "profile-slot3.json";
    std::filesystem::create_directories(unavailable);

    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    const sokoban::PlayerProfile active = manager.loadActiveProfile();
    const std::vector<sokoban::SaveSlotManager::SlotSummary> summaries =
        manager.slotSummaries(active, 4);

    check(summaries[1].state == sokoban::SaveSlotState::Recoverable,
        "valid backup is reported as recoverable");
    check(summaries[1].currentLevel == 1,
        "recoverable summary comes from backup");
    check(summaries[2].state == sokoban::SaveSlotState::Unavailable,
        "non-file save path is reported as unavailable");

    std::ifstream primary(directory.path() / "profile-slot2.json");
    std::string primaryContents;
    std::getline(primary, primaryContents);
    primary.close();
    check(primaryContents == "corrupt primary",
        "inspection does not archive corrupt primary");
    check(std::filesystem::is_regular_file(
            directory.path() / "profile-slot2.backup.json"),
        "inspection does not consume recoverable backup");

    const std::optional<sokoban::PlayerProfile> recovered =
        manager.switchTo(1, active);
    check(recovered.has_value() && recovered->currentLevel == 1,
        "explicit switch recovers backup profile");
    check(sokoban::SaveStore(directory.path(), "profile-slot2").inspect()
            .disposition ==
            sokoban::SaveStore::InspectionDisposition::PrimaryValid,
        "recovered slot has a valid primary after switch");
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
    check(manager.flush().allPersisted(),
        "post-rollback save reports durable completion");
    check(sokoban::SaveStore(directory.path(), "profile-slot3").load()
            .profile.currentLevel == 0,
        "post-failure saves still target previous slot");
    check(sokoban::SaveStore(directory.path(), "profile-slot2").load()
            .profile.currentLevel == 1,
        "failed destination is not used by later saves");
}

void testFailedOutgoingSavePreventsSlotSwitch()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), std::chrono::hours(1));
    (void)manager.loadActiveProfile();

    const sokoban::PlayerProfile committed = profileWithProgress(1);
    manager.saveProgress(committed, true);
    check(manager.flush().allPersisted(),
        "initial outgoing profile reports durable completion");

    const std::filesystem::path blockedTemporary =
        directory.path() / "profile.json.tmp";
    std::filesystem::create_directories(blockedTemporary);
    std::ofstream(blockedTemporary / "blocker.txt") << "blocked";

    const sokoban::PlayerProfile latest = profileWithProgress(3);
    manager.saveProgress(latest, true);
    bool threw = false;
    try {
        (void)manager.switchTo(1, latest);
    } catch (const std::runtime_error& error) {
        threw = std::string_view(error.what()).find(
                    "outgoing save slot could not be persisted") !=
            std::string_view::npos;
    }
    check(threw, "failed outgoing save prevents slot switching");
    check(manager.activeSlot() == 0,
        "failed outgoing save keeps the original slot active");
    check(manager.progressDiagnostics().pending &&
            !manager.progressDiagnostics().lastWriteSucceeded,
        "failed outgoing snapshot remains pending with failure diagnostics");
    check(manager.progressStatus().starts_with("Player profile save failed:"),
        "failed outgoing save status remains visible after rejected switch");

    std::ifstream committedStream(
        directory.path() / "profile.json", std::ios::binary);
    const std::string committedContents {
        std::istreambuf_iterator<char>(committedStream),
        std::istreambuf_iterator<char>() };
    committedStream.close();
    check(sokoban::decodePlayerProfile(committedContents).profile == committed,
        "rejected switch leaves the last committed outgoing profile intact");

    std::filesystem::remove_all(blockedTemporary);
    manager.saveProgress(latest, true);
    const std::optional<sokoban::PlayerProfile> switched =
        manager.switchTo(1, latest);
    check(switched.has_value() && manager.activeSlot() == 1,
        "resubmitting after storage recovery permits the switch");
    check(sokoban::SaveStore(directory.path()).load().profile == latest,
        "successful retry persists the newest outgoing profile");
}

void testDeletionFailurePreservesSummaryAndFiles()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    const sokoban::PlayerProfile active = manager.loadActiveProfile();
    const std::filesystem::path primary = directory.path() / "profile-slot2.json";
    const std::filesystem::path backup =
        directory.path() / "profile-slot2.backup.json";

    {
        sokoban::SaveStore second(directory.path(), "profile-slot2");
        check(second.save(profileWithProgress(1)), "slot 2 seeded for delete failure");
    }
    check(manager.slotSummaries(active, 4)[1].state ==
            sokoban::SaveSlotState::Ready,
        "slot 2 summary is primed before deletion failure");

    // A non-empty directory cannot be removed by std::filesystem::remove.
    // It deterministically exercises the error path without relying on OS
    // file-lock or permission behavior.
    std::filesystem::remove(primary);
    std::filesystem::create_directories(primary);
    std::ofstream(primary / "blocker.txt") << "blocked";
    std::ofstream(backup) << "backup must remain";

    const sokoban::SaveSlotManager::DeleteResult result = manager.deleteSlot(1);
    check(result.succeeded, "deletion commits before artifact cleanup");
    check(result.cleanupPending, "partial cleanup is reported");
    check(result.message.find("profile-slot2.json") != std::string::npos,
        "cleanup warning identifies the path");
    check(std::filesystem::exists(primary / "blocker.txt"),
        "blocked primary artifact remains for later cleanup");
    check(!std::filesystem::exists(backup),
        "cleanup continues to remove other recovery candidates");
    check(std::filesystem::is_regular_file(
            directory.path() / "profile-slot2.deleted"),
        "deletion marker remains while cleanup is pending");
    check(manager.slotSummaries(active, 4)[1].state ==
            sokoban::SaveSlotState::Empty,
        "committed deletion immediately empties the cached summary");

    const std::optional<sokoban::PlayerProfile> deleted =
        manager.switchTo(1, active);
    check(deleted && deleted->progressEmpty(),
        "cleanup failure cannot resurrect the deleted slot");
}

void testDeletionRemovesEveryRecoverableArtifact()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    const sokoban::PlayerProfile active = manager.loadActiveProfile();
    const sokoban::SaveStore slot(directory.path(), "profile-slot2");
    const std::string profile = profileWithProgress(3).serialize(
        sokoban::ProfileSections::ProgressOnly);
    for (const std::filesystem::path& artifact :
         slot.recoverableArtifactPaths()) {
        std::ofstream(artifact, std::ios::binary) << profile;
    }
    const std::filesystem::path diagnostic =
        slot.primaryPath().string() + ".corrupt-diagnostic";
    std::ofstream(diagnostic, std::ios::binary) << profile;

    check(manager.deleteSlot(1).succeeded,
        "slot with every recovery artifact deletes successfully");
    for (const std::filesystem::path& artifact :
         slot.recoverableArtifactPaths()) {
        check(!std::filesystem::exists(artifact),
            "deleted slot has no eligible recovery artifact");
    }
    check(std::filesystem::is_regular_file(slot.deletionMarkerPath()),
        "complete deletion retains its commit marker");
    check(std::filesystem::is_regular_file(diagnostic),
        "non-recoverable corrupt diagnostics are preserved");

    sokoban::SaveSlotManager reopened(directory.path(), instantWrites);
    const std::optional<sokoban::PlayerProfile> loaded =
        reopened.switchTo(1, active);
    check(loaded && loaded->progressEmpty(),
        "reopening cannot recover deleted temporary or displaced data");
    reopened.saveProgress(profileWithProgress(1), true);
    check(reopened.flush().allPersisted(),
        "new save reports durable completion after deletion");
    check(!std::filesystem::exists(slot.deletionMarkerPath()),
        "a successful new save replaces the deletion marker");
    check(sokoban::SaveStore(directory.path(), "profile-slot2").load()
            .profile.currentLevel == 1,
        "the replacement slot loads its new progress");
}

void testDeletionMarkerFailurePreservesTheSlot()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), instantWrites);
    const sokoban::PlayerProfile active = manager.loadActiveProfile();
    sokoban::SaveStore slot(directory.path(), "profile-slot2");
    check(slot.save(profileWithProgress(2)),
        "slot is seeded before marker failure");
    const std::filesystem::path blockedTemporary =
        slot.deletionMarkerPath().string() + ".tmp";
    std::filesystem::create_directories(blockedTemporary);
    std::ofstream(blockedTemporary / "blocker.txt") << "blocked";

    const sokoban::SaveSlotManager::DeleteResult result = manager.deleteSlot(1);
    check(!result.succeeded, "uncommitted deletion reports failure");
    check(std::filesystem::is_regular_file(slot.primaryPath()),
        "marker failure leaves the primary save intact");
    check(manager.slotSummaries(active, 4)[1].state ==
            sokoban::SaveSlotState::Ready,
        "marker failure preserves the saved-slot summary");
}

void testActiveDeletionDiscardsPendingSnapshotWithCleanupFailure()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), std::chrono::hours(1));
    (void)manager.loadActiveProfile();
    const std::filesystem::path blockedTemporary =
        directory.path() / "profile.json.tmp";
    std::filesystem::create_directories(blockedTemporary);
    std::ofstream(blockedTemporary / "blocker.txt") << "blocked";
    manager.saveProgress(profileWithProgress(3), false);

    const sokoban::SaveSlotManager::DeleteResult result = manager.deleteSlot(0);
    check(result.succeeded && result.cleanupPending,
        "active deletion commits despite a blocked recovery artifact");
    check(!manager.progressDiagnostics().pending,
        "committed deletion discards the retained failed snapshot");
    const sokoban::SaveSlotManager::FlushResult afterDeletion = manager.flush();
    check(afterDeletion.progress.outcome ==
            sokoban::AsyncSaveStore::PersistenceOutcome::Persisted &&
            afterDeletion.progress.requestedRevision == 0 &&
            afterDeletion.progress.persistedRevision == 0,
        "committed deletion starts a fresh progress revision epoch");
    std::filesystem::remove_all(blockedTemporary);

    sokoban::SaveSlotManager reopened(directory.path(), instantWrites);
    check(reopened.loadActiveProfile().progressEmpty(),
        "discarded pending progress cannot return after restart");
}

void testFailedActiveDeletionRetainsItsPendingSnapshot()
{
    TemporaryDirectory directory;
    sokoban::SaveSlotManager manager(directory.path(), std::chrono::hours(1));
    (void)manager.loadActiveProfile();
    const sokoban::SaveStore slot(directory.path());
    const std::filesystem::path blockedTemporary =
        slot.deletionMarkerPath().string() + ".tmp";
    std::filesystem::create_directories(blockedTemporary);
    std::ofstream(blockedTemporary / "blocker.txt") << "blocked";
    const sokoban::PlayerProfile latest = profileWithProgress(3);
    manager.saveProgress(latest, false);

    const sokoban::SaveSlotManager::DeleteResult result = manager.deleteSlot(0);
    check(!result.succeeded, "active marker failure rejects deletion");
    check(manager.progressDiagnostics().pending,
        "rejected deletion retains the queued active snapshot");

    std::filesystem::remove_all(blockedTemporary);
    manager.saveProgress(latest, true);
    check(manager.flush().allPersisted(),
        "retained snapshot reports durable completion after retry");
    check(sokoban::SaveStore(directory.path()).load().profile == latest,
        "queued progress remains persistable after deletion fails");
}

} // namespace

int main()
{
    testFreshInstallWritesNothing();
    testUnsupportedActiveDataStopsLoading();
    testInterruptedActiveSlotMarkerRecovery();
    testOverworldTargetSummaries();
    testPreSplitSettingsMigration();
    testSummariesSwitchingAndDeletion();
    testSummaryCacheInvalidation();
    testSlotInspectionIsNonMutating();
    testFailedOutgoingSavePreventsSlotSwitch();
    testFailedMarkerCommitRollsBackSwitch();
    testDeletionFailurePreservesSummaryAndFiles();
    testDeletionRemovesEveryRecoverableArtifact();
    testDeletionMarkerFailurePreservesTheSlot();
    testActiveDeletionDiscardsPendingSnapshotWithCleanupFailure();
    testFailedActiveDeletionRetainsItsPendingSnapshot();

    if (failures == 0) {
        std::cout << "SaveSlotManagerTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "SaveSlotManagerTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
