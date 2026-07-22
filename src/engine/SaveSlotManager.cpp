#include "engine/SaveSlotManager.hpp"

#include <fstream>
#include <iterator>
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

} // namespace

SaveSlotManager::SaveSlotManager(
    std::filesystem::path directory,
    std::chrono::milliseconds writeDelay)
    : directory_(std::move(directory))
    , writeDelay_(writeDelay)
    , activeSlot_(readActiveSlotMarker())
    , settingsStore_(std::make_unique<AsyncSaveStore>(
          directory_, writeDelay_, "settings", ProfileSections::SettingsOnly))
    , progressStore_(std::make_unique<AsyncSaveStore>(
          directory_, writeDelay_, slotFileStem(activeSlot_),
          ProfileSections::ProgressOnly))
{
}

PlayerProfile SaveSlotManager::loadActiveProfile()
{
    const SaveStore::LoadResult slot = progressStore_->load();
    PlayerProfile profile = slot.profile;
    const SaveStore::LoadResult settings = settingsStore_->load();
    if (settings.disposition == SaveStore::LoadDisposition::CreatedDefault) {
        // Migrate a pre-split combined save's settings into the shared file;
        // a genuinely fresh install writes nothing anywhere.
        if (slot.disposition != SaveStore::LoadDisposition::CreatedDefault) {
            settingsStore_->requestSave(
                profile.settingsOnly(), AsyncSaveStore::Urgency::Immediate);
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
    // Empty means "no progress", not "no file": a deleted or reset slot must
    // present as empty even while its profile object exists.
    summary.empty = profile.progressEmpty();
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

std::vector<SaveSlotManager::SlotSummary> SaveSlotManager::slotSummaries(
    const PlayerProfile& activeProfile,
    int levelCount) const
{
    std::vector<SlotSummary> slots;
    slots.reserve(slotCount);
    for (int slot = 0; slot < slotCount; ++slot) {
        if (slot == activeSlot_) {
            slots.push_back(summarize(activeProfile, levelCount));
            continue;
        }
        const SaveStore store(directory_, slotFileStem(slot));
        SlotSummary summary;
        try {
            if (std::filesystem::is_regular_file(store.primaryPath())) {
                std::ifstream stream(store.primaryPath(), std::ios::binary);
                const std::string contents(
                    (std::istreambuf_iterator<char>(stream)),
                    std::istreambuf_iterator<char>());
                summary = summarize(decodePlayerProfile(contents).profile, levelCount);
            }
        } catch (const std::exception&) {
            // Unreadable slots present as empty; switching to one runs the
            // store's full backup/archival recovery.
            summary = {};
        }
        slots.push_back(summary);
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

    progressStore_->flush();
    activeSlot_ = slot;
    writeActiveSlotMarker();
    progressStore_ = std::make_unique<AsyncSaveStore>(
        directory_, writeDelay_, slotFileStem(activeSlot_),
        ProfileSections::ProgressOnly);

    // Settings are shared across slots: carry the live ones over the
    // incoming slot's stale copies.
    PlayerProfile profile = progressStore_->load().profile;
    profile.adoptSettingsFrom(currentProfile);
    return profile;
}

void SaveSlotManager::deleteSlot(int slot)
{
    if (slot < 0 || slot >= slotCount) {
        return;
    }
    if (slot == activeSlot_) {
        // Drain pending writes so an in-flight save cannot resurrect the
        // files after removal.
        progressStore_->flush();
    }
    const SaveStore store(directory_, slotFileStem(slot));
    std::error_code error;
    std::filesystem::remove(store.primaryPath(), error);
    std::filesystem::remove(store.backupPath(), error);
}

void SaveSlotManager::saveProgress(const PlayerProfile& profile, bool immediate)
{
    progressStore_->requestSave(
        profile,
        immediate
            ? AsyncSaveStore::Urgency::Immediate
            : AsyncSaveStore::Urgency::Deferred);
}

void SaveSlotManager::saveSettings(const PlayerProfile& profile, bool immediate)
{
    settingsStore_->requestSave(
        profile.settingsOnly(),
        immediate
            ? AsyncSaveStore::Urgency::Immediate
            : AsyncSaveStore::Urgency::Deferred);
}

void SaveSlotManager::flush()
{
    progressStore_->flush();
    settingsStore_->flush();
}

std::string SaveSlotManager::progressStatus() const
{
    return progressStore_->status();
}

AsyncSaveStore::Diagnostics SaveSlotManager::progressDiagnostics() const
{
    return progressStore_->diagnostics();
}

std::string SaveSlotManager::settingsStatus() const
{
    return settingsStore_->status();
}

AsyncSaveStore::Diagnostics SaveSlotManager::settingsDiagnostics() const
{
    return settingsStore_->diagnostics();
}

int SaveSlotManager::readActiveSlotMarker() const
{
    std::ifstream stream(directory_ / activeSlotMarkerName);
    int slot = 1;
    stream >> slot;
    return (slot >= 1 && slot <= slotCount) ? slot - 1 : 0;
}

void SaveSlotManager::writeActiveSlotMarker() const
{
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    std::ofstream stream(
        directory_ / activeSlotMarkerName, std::ios::binary | std::ios::trunc);
    stream << (activeSlot_ + 1) << '\n';
}

} // namespace sokoban
