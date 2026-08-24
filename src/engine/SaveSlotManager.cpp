#include "engine/SaveSlotManager.hpp"

#include "engine/AtomicFile.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace sokoban {
namespace {

// Slot 1 keeps the historical stem so pre-slot saves remain valid.
std::string slotFileStem(int slot)
{
    return slot == 0 ? "profile" : "profile-slot" + std::to_string(slot + 1);
}

constexpr const char* activeSlotMarkerName = "active-slot.txt";

std::optional<int> validMarkerSlot(const std::filesystem::path& path)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        throw std::system_error(error, "cannot inspect active-slot marker " + path.string());
    }
    if (!exists) {
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(path, error)) {
        if (error) {
            throw std::system_error(
                error, "cannot inspect active-slot marker " + path.string());
        }
        return std::nullopt;
    }

    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open active-slot marker " + path.string());
    }
    int slot = 0;
    stream >> slot;
    if (stream.bad()) {
        throw std::runtime_error("cannot read active-slot marker " + path.string());
    }
    if (slot < 1 || slot > SaveSlotManager::slotCount) {
        return std::nullopt;
    }
    return slot - 1;
}

void removeMarkerArtifact(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        throw std::system_error(
            error, "cannot remove stale active-slot artifact " + path.string());
    }
}

void recoverActiveSlotMarker(const std::filesystem::path& marker)
{
    const std::filesystem::path temporary = marker.string() + ".tmp";
    const std::filesystem::path displaced = marker.string() + ".replace-old";
    const std::optional<int> liveSlot = validMarkerSlot(marker);
    const std::optional<int> temporarySlot = validMarkerSlot(temporary);
    const std::optional<int> displacedSlot = validMarkerSlot(displaced);

    if (liveSlot) {
        removeMarkerArtifact(temporary);
        removeMarkerArtifact(displaced);
        return;
    }

    if (temporarySlot) {
        atomicFile::replace(marker, temporary);
    } else if (displacedSlot) {
        atomicFile::replace(marker, displaced);
    } else {
        return;
    }
    removeMarkerArtifact(temporary);
    removeMarkerArtifact(displaced);
}

} // namespace

SaveSlotManager::SaveSlotManager(
    std::filesystem::path directory,
    std::chrono::milliseconds writeDelay)
    : directory_(std::move(directory))
    , writeDelay_(writeDelay)
    , activeSlot_(readActiveSlotMarker())
    , store_(std::make_unique<AsyncSaveStore>(
          directory_, writeDelay_, "settings", ProfileSections::SettingsOnly))
{
    progressChannel_ = store_->addChannel(
        directory_, slotFileStem(activeSlot_), ProfileSections::ProgressOnly);
}

PlayerProfile SaveSlotManager::loadActiveProfile()
{
    const SaveStore::LoadResult slot = store_->load(progressChannel_);
    PlayerProfile profile = slot.profile;
    const SaveStore::LoadResult settings = store_->load(kSettingsChannel);
    if (settings.disposition == SaveStore::LoadDisposition::CreatedDefault) {
        // Migrate a pre-split combined save's settings into the shared file;
        // a genuinely fresh install writes nothing anywhere.
        if (slot.disposition != SaveStore::LoadDisposition::CreatedDefault) {
            store_->requestSave(
                kSettingsChannel, profile.settingsOnly(),
                AsyncSaveStore::Urgency::Immediate);
        }
    } else {
        profile.adoptSettingsFrom(settings.profile);
    }
    return profile;
}

SaveSlotManager::SlotSummary SaveSlotManager::summarize(
    const PlayerProfile& profile,
    int levelCount)
{
    SlotSummary summary;
    summary.state = profile.progressEmpty()
        ? SaveSlotState::Empty
        : SaveSlotState::Ready;
    summary.currentLevel = profile.currentLevel;
    for (const PlayerProfile::LevelProgress& level : profile.levels) {
        summary.completedLevels += level.completed ? 1 : 0;
    }
    summary.completed = levelCount > 0 && [&] {
        for (int level = 0; level < levelCount; ++level) {
            const PlayerProfile::LevelProgress* progress =
                profile.progressForLevel(level);
            if (progress == nullptr || !progress->completed) {
                return false;
            }
        }
        return true;
    }();
    return summary;
}

SaveSlotManager::SlotSummary SaveSlotManager::summarize(
    const PlayerProfile& profile,
    std::span<const LevelLocation> targets)
{
    SlotSummary summary;
    summary.state = profile.progressEmpty()
        ? SaveSlotState::Empty
        : SaveSlotState::Ready;
    summary.currentLevel = profile.worldContext ==
            PlayerProfile::WorldContext::Puzzle
        ? profile.currentLevel
        : -1;
    for (LevelLocation target : targets) {
        summary.completedLevels += profile.screenCompleted(target) ? 1 : 0;
    }
    summary.completed = !targets.empty() &&
        summary.completedLevels == static_cast<int>(targets.size());
    return summary;
}

SaveSlotManager::SlotSummary SaveSlotManager::inspectSlotSummary(
    int slot,
    int levelCount) const
{
    const SaveStore::InspectionResult inspection =
        SaveStore(directory_, slotFileStem(slot), ProfileSections::ProgressOnly)
            .inspect();
    if (inspection.profile) {
        SlotSummary summary = summarize(*inspection.profile, levelCount);
        if (inspection.disposition ==
            SaveStore::InspectionDisposition::BackupValid) {
            summary.state = SaveSlotState::Recoverable;
        }
        return summary;
    }

    SlotSummary summary;
    if (inspection.disposition == SaveStore::InspectionDisposition::Corrupt) {
        summary.state = SaveSlotState::Corrupt;
    } else if (inspection.disposition ==
        SaveStore::InspectionDisposition::StorageUnavailable) {
        summary.state = SaveSlotState::Unavailable;
    }
    return summary;
}

SaveSlotManager::SlotSummary SaveSlotManager::inspectSlotSummary(
    int slot,
    std::span<const LevelLocation> targets) const
{
    const SaveStore::InspectionResult inspection =
        SaveStore(directory_, slotFileStem(slot), ProfileSections::ProgressOnly)
            .inspect();
    if (inspection.profile) {
        SlotSummary summary = summarize(*inspection.profile, targets);
        if (inspection.disposition ==
            SaveStore::InspectionDisposition::BackupValid) {
            summary.state = SaveSlotState::Recoverable;
        }
        return summary;
    }

    SlotSummary summary;
    if (inspection.disposition == SaveStore::InspectionDisposition::Corrupt) {
        summary.state = SaveSlotState::Corrupt;
    } else if (inspection.disposition ==
        SaveStore::InspectionDisposition::StorageUnavailable) {
        summary.state = SaveSlotState::Unavailable;
    }
    return summary;
}

std::vector<SaveSlotManager::SlotSummary> SaveSlotManager::slotSummaries(
    const PlayerProfile& activeProfile,
    int levelCount) const
{
    if (summaryCacheLevelCount_ != levelCount) {
        // A different level set invalidates every cached completed flag.
        summaryCache_.assign(slotCount, std::nullopt);
        summaryCacheLevelCount_ = levelCount;
        summaryCacheUsesTargets_ = false;
        summaryCacheTargets_.clear();
    }

    std::vector<SlotSummary> slots;
    slots.reserve(slotCount);
    for (int slot = 0; slot < slotCount; ++slot) {
        if (slot == activeSlot_) {
            // The active slot's progress is live and unsaved; never cached.
            slots.push_back(summarize(activeProfile, levelCount));
            continue;
        }
        std::optional<SlotSummary>& cached =
            summaryCache_[static_cast<std::size_t>(slot)];
        if (!cached) {
            cached = inspectSlotSummary(slot, levelCount);
        }
        slots.push_back(*cached);
    }
    return slots;
}

std::vector<SaveSlotManager::SlotSummary> SaveSlotManager::slotSummaries(
    const PlayerProfile& activeProfile,
    std::span<const LevelLocation> targets) const
{
    const std::vector<LevelLocation> targetKey(targets.begin(), targets.end());
    if (!summaryCacheUsesTargets_ || summaryCacheTargets_ != targetKey) {
        summaryCache_.assign(slotCount, std::nullopt);
        summaryCacheTargets_ = targetKey;
        summaryCacheUsesTargets_ = true;
        summaryCacheLevelCount_ = -1;
    }

    std::vector<SlotSummary> slots;
    slots.reserve(slotCount);
    for (int slot = 0; slot < slotCount; ++slot) {
        if (slot == activeSlot_) {
            slots.push_back(summarize(activeProfile, targets));
            continue;
        }
        std::optional<SlotSummary>& cached =
            summaryCache_[static_cast<std::size_t>(slot)];
        if (!cached) {
            cached = inspectSlotSummary(slot, targets);
        }
        slots.push_back(*cached);
    }
    return slots;
}

std::optional<PlayerProfile> SaveSlotManager::switchTo(
    int slot,
    const PlayerProfile& currentProfile)
{
    if (slot < 0 || slot >= slotCount || slot == activeSlot_) {
        return std::nullopt;
    }

    const int previousSlot = activeSlot_;
    const std::string incomingStem = slotFileStem(slot);

    // Prepare the incoming profile before changing any live state. Loading
    // may recover a backup or migrate an old profile, but cannot repoint the
    // active asynchronous save channel.
    SaveStore incomingStore(
        directory_, incomingStem, ProfileSections::ProgressOnly);
    const SaveStore::InspectionResult inspection = incomingStore.inspect();
    if (inspection.disposition ==
        SaveStore::InspectionDisposition::Corrupt) {
        throw std::runtime_error(
            "save slot " + std::to_string(slot + 1) + " is corrupt");
    }
    if (inspection.disposition ==
        SaveStore::InspectionDisposition::StorageUnavailable) {
        throw std::runtime_error(
            "save slot " + std::to_string(slot + 1) +
            " storage is unavailable: " + inspection.message);
    }
    SaveStore::LoadResult loaded = incomingStore.load();
    if (loaded.disposition == SaveStore::LoadDisposition::StorageUnavailable) {
        throw std::runtime_error(
            "save slot " + std::to_string(slot + 1) +
            " could not be loaded: " + loaded.message);
    }
    if (loaded.disposition == SaveStore::LoadDisposition::ResetCorrupt) {
        throw std::runtime_error(
            "save slot " + std::to_string(slot + 1) +
            " became corrupt while loading");
    }
    PlayerProfile profile = std::move(loaded.profile);
    profile.adoptSettingsFrom(currentProfile);

    store_->replaceChannel(
        progressChannel_, directory_, incomingStem,
        ProfileSections::ProgressOnly);

    try {
        // The atomic marker replacement is the commit point. If it cannot be
        // written, restore the outgoing channel before exposing the failure.
        writeActiveSlotMarker(slot);
    } catch (...) {
        store_->replaceChannel(
            progressChannel_, directory_, slotFileStem(previousSlot),
            ProfileSections::ProgressOnly);
        throw;
    }

    activeSlot_ = slot;
    // The old-active slot's on-disk summary now matters again, and the
    // new-active one becomes live; drop the whole non-active cache.
    summaryCache_.assign(slotCount, std::nullopt);
    return profile;
}

SaveSlotManager::DeleteResult SaveSlotManager::deleteSlot(int slot)
{
    if (slot < 0 || slot >= slotCount) {
        return {
            .message = "save slot selection is invalid",
        };
    }
    if (slot == activeSlot_) {
        // Drain pending writes so an in-flight save cannot resurrect the
        // files after removal.
        store_->flush();
    }
    const SaveStore store(directory_, slotFileStem(slot));
    const auto removeFile = [](const std::filesystem::path& path)
        -> std::optional<std::string> {
        std::error_code error;
        (void)std::filesystem::remove(path, error);
        if (error) {
            return "could not delete " + path.filename().string() + ": " +
                error.message();
        }
        return std::nullopt;
    };

    if (const std::optional<std::string> failure = removeFile(store.primaryPath())) {
        return { .message = *failure };
    }
    if (const std::optional<std::string> failure = removeFile(store.backupPath())) {
        return { .message = *failure };
    }

    summaryCache_[static_cast<std::size_t>(slot)] = SlotSummary {};
    return { .succeeded = true };
}

void SaveSlotManager::saveProgress(const PlayerProfile& profile, bool immediate)
{
    store_->requestSave(
        progressChannel_, profile,
        immediate
            ? AsyncSaveStore::Urgency::Immediate
            : AsyncSaveStore::Urgency::Deferred);
}

void SaveSlotManager::saveSettings(const PlayerProfile& profile, bool immediate)
{
    store_->requestSave(
        kSettingsChannel, profile.settingsOnly(),
        immediate
            ? AsyncSaveStore::Urgency::Immediate
            : AsyncSaveStore::Urgency::Deferred);
}

void SaveSlotManager::flush()
{
    store_->flush();
}

std::string SaveSlotManager::progressStatus() const
{
    return store_->status(progressChannel_);
}

AsyncSaveStore::Diagnostics SaveSlotManager::progressDiagnostics() const
{
    return store_->diagnostics(progressChannel_);
}

std::string SaveSlotManager::settingsStatus() const
{
    return store_->status(kSettingsChannel);
}

AsyncSaveStore::Diagnostics SaveSlotManager::settingsDiagnostics() const
{
    return store_->diagnostics(kSettingsChannel);
}

int SaveSlotManager::readActiveSlotMarker() const
{
    const std::filesystem::path marker = directory_ / activeSlotMarkerName;
    recoverActiveSlotMarker(marker);
    return validMarkerSlot(marker).value_or(0);
}

void SaveSlotManager::writeActiveSlotMarker(int slot) const
{
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        throw std::runtime_error(
            "cannot create save directory " + directory_.string() + ": " +
            error.message());
    }
    atomicFile::write(
        directory_ / activeSlotMarkerName,
        std::to_string(slot + 1) + '\n');
}

} // namespace sokoban
