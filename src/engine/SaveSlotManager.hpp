#pragma once

#include "engine/AsyncSaveStore.hpp"
#include "engine/PlayerProfile.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace sokoban {

// Headless owner of the save-slot lifecycle: the per-slot progress stores,
// the shared settings store, the active-slot marker file, slot summaries,
// switching, and deletion. The caller (Application) owns the live
// PlayerProfile and all gameplay consequences; this class owns every disk
// decision. Fresh installs write nothing: files appear only through explicit
// save requests (first checkpoint or a settings change).
class SaveSlotManager {
public:
    static constexpr int slotCount = 3;

    struct SlotSummary {
        // "Empty" means no progress (fresh, reset, or deleted); a file may
        // still exist on disk.
        bool empty = true;
        bool completed = false; // every level 0..levelCount-1 completed
        int currentLevel = 0; // 0-based
        int completedLevels = 0;
    };

    explicit SaveSlotManager(
        std::filesystem::path directory,
        std::chrono::milliseconds writeDelay = std::chrono::seconds(2));

    // Loads the marker's slot merged with the shared settings file. When the
    // settings file does not exist yet, a pre-split combined save's settings
    // are adopted and written; a genuinely fresh install writes nothing.
    [[nodiscard]] PlayerProfile loadActiveProfile();

    [[nodiscard]] int activeSlot() const { return activeSlot_; }
    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

    // One summary per slot; the active slot summarizes `activeProfile`
    // (live, possibly unsaved), the others decode their files.
    [[nodiscard]] std::vector<SlotSummary> slotSummaries(
        const PlayerProfile& activeProfile,
        int levelCount) const;

    // Flushes pending writes, swaps stores and the marker to `slot`, and
    // returns its progress with `currentProfile`'s shared settings carried
    // over. Empty when the slot is invalid or already active. The caller
    // settles (checkpoints/saves) the outgoing profile first.
    [[nodiscard]] std::optional<PlayerProfile> switchTo(
        int slot,
        const PlayerProfile& currentProfile);

    // Removes the slot's save files (primary and backup). Deleting the
    // active slot drains pending writes first; the caller resets its live
    // profile's progress alongside.
    void deleteSlot(int slot);

    void saveProgress(const PlayerProfile& profile, bool immediate);
    // Persists only the settings sections into the shared settings file.
    void saveSettings(const PlayerProfile& profile, bool immediate);
    void flush();

    [[nodiscard]] std::string progressStatus() const;
    [[nodiscard]] AsyncSaveStore::Diagnostics progressDiagnostics() const;
    [[nodiscard]] std::string settingsStatus() const;
    [[nodiscard]] AsyncSaveStore::Diagnostics settingsDiagnostics() const;

private:
    [[nodiscard]] static SlotSummary summarize(
        const PlayerProfile& profile,
        int levelCount);
    [[nodiscard]] int readActiveSlotMarker() const;
    void writeActiveSlotMarker() const;

    std::filesystem::path directory_;
    std::chrono::milliseconds writeDelay_;
    int activeSlot_ = 0; // 0-based
    // Shared across slots: audio/video/input/accessibility settings.
    std::unique_ptr<AsyncSaveStore> settingsStore_;
    // Per-slot progress; recreated on slot switches.
    std::unique_ptr<AsyncSaveStore> progressStore_;
};

} // namespace sokoban
